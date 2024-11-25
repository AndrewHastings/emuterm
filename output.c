/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * Translate output from emulated terminal into xterm control sequences.
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "emuterm.h"
#include "output.h"
#include "termcap.h"


#define ANSI_CLEAR	    "\e[H\e[2J"
#define ANSI_CLEAR_BELOW    "\e[J"
#define ANSI_HOME	    "\e[H"
#define ANSI_SET_ROW	    "\e[%dH"
#define ANSI_SCROLL_REGION  "\e[;%dr"
#define ANSI_SCROLL_RESET   "\e[r"
#define ANSI_RESIZE	    "\e[8;%d;%dt"
#define DEC_AUTOWRAP_ON	    "\e[?7h"
#define DEC_AUTOWRAP_OFF    "\e[?7l"
#define DEC_MARGINS_ON	    "\e[?69h"
#define DEC_MARGINS_OFF	    "\e[?69l"
#define DEC_MARGINS_SET	    "\e[1;%ds"

int term_set, term_am;
int term_cols, term_lines;


void omode(int raw)
{
	static struct termios otio;
	static struct winsize ows;

	if (raw) {
		struct termios ntio;

		/* save tty settings and enter raw mode */
		tcgetattr(STDIN_FILENO, &otio);
		ntio = otio;
		cfmakeraw(&ntio);
		tcsetattr(STDIN_FILENO, TCSANOW, &ntio);

		if (term_set) {
			ioctl(STDIN_FILENO, TIOCGWINSZ, &ows);

			/* resize user terminal */
			if (resize_win)
				dprintf(STDOUT_FILENO, ANSI_RESIZE,
					term_lines, term_cols);

			/* else change scroll region and margins */
			else {
				dprintf(STDOUT_FILENO,
					ANSI_SCROLL_REGION ANSI_CLEAR,
					term_lines);

				/* XXX doesn't seem to work */
				if (term_cols != ows.ws_col)
					dprintf(STDOUT_FILENO,
						DEC_MARGINS_ON DEC_MARGINS_SET,
						term_cols);
			}

			/* disable autowrap if needed */
			if (!term_am)
				dprintf(STDOUT_FILENO, DEC_AUTOWRAP_OFF);
		}
	} else {
		if (term_set) {

			/* restore user terminal size */
			if (resize_win)
				dprintf(STDOUT_FILENO, ANSI_RESIZE,
					ows.ws_row, ows.ws_col);

			/* else reset scroll region and margins */
			else {
				dprintf(STDOUT_FILENO,
					ANSI_SCROLL_RESET ANSI_SET_ROW,
					term_lines);
				if (term_cols != ows.ws_col)
					dprintf(STDOUT_FILENO, DEC_MARGINS_OFF);
			}

			/* re-enable autowrap if needed */
			if (!term_am)
				dprintf(STDOUT_FILENO, DEC_AUTOWRAP_ON);
		}

		/* restore tty settings */
		tcsetattr(STDIN_FILENO, TCSANOW, &otio);
	}
}


int savefd = -1;

void save_output(char *path)
{
	if (savefd >= 0) {
		if (!path || !path[0]) {
			dprintf(STDOUT_FILENO, "Recording stopped\r\n");
			close(savefd);
			savefd = -1;
			return;
		}

		dprintf(STDOUT_FILENO, "Recording already in progress, "
				       "use ~w to stop\r\n");
		return;
	}

	if (!path)
		return;

	if (path[0] == ' ')	/* skip optional space after "~w" */
		path++;
	if (!path[0]) {
		dprintf(STDOUT_FILENO, "No recording in progress, "
				       "use ~? for help\r\n");
		return;
	}

	if ((savefd = open(path, O_WRONLY|O_CREAT|O_APPEND, 0666)) < 0) {
		dprintf(STDOUT_FILENO, "%s: %s\r\n", path, strerror(errno));
		return;
	}
	dprintf(STDOUT_FILENO, "Recording to '%s'\r\n", path);
}


/* top-level parse table for output */
enum action {
	AC_IGNORE = 0,		  /* no action (default) */
	AC_PRINT,		  /* print as-is */
	AC_CONST		  /* replace w/constant string */
};

struct pentry {
	enum action	pt_action;
	char		*pt_arg;
} parsetab[128] = { AC_IGNORE, NULL };


/* validate terminal type and initialize parse table */
struct tcap {
	char	tc_name[4];	/* capability name */
	char	*tc_rep;	/* xterm replacement */
} tcaps[] = {
	"bc", "\b",		/* ^H */
	"bl", "\a",		/* ^G */
	"cd", ANSI_CLEAR_BELOW,
	"cr", "\r",		/* ^M */
	"do", "\n",		/* ^J */
	"ho", ANSI_HOME,
	"le", "\e[D",		/* ANSI left */
	"nd", "\e[C",		/* ANSI right */
	"sf", "\e[S",		/* ANSI scroll up */
	"ta", "\t",		/* ^I */
	"up", "\e[A",		/* ANSI up */
	"cm", NULL,		/* unsupported */
	"ll", NULL,		/* unsupported */
	"",   NULL
};


/* returns capability value after skipping over padding */
char *get_strcap(char *cap)
{
	static char cbuf[2048];	/* extracted capability values */
	static char *cp = cbuf;
	char *rv;

	if (!(rv = tgetstr(cap, &cp)) || *rv < '0' || *rv > '9')
		return rv;
	while (*rv >= '0' && *rv <= '9')
		rv++;
	if (*rv == '.')
		rv++;
	if (*rv >= '0' && *rv <= '9')
		rv++;
	return rv;
}


char *set_termtype(char *term, struct winsize *ws, char *errbuf)
{
	static char tbuf[2048];	/* returned termcap entry */
	char *cp;
	struct pentry *p1, *p2;
	struct tcap *tp;
	int c, rv;

#define ERR_RET(fmt, val)			\
    do {					\
	sprintf(errbuf, fmt, val);		\
	return errbuf;				\
    } while (0)

	rv = tgetent(tbuf, term);
	if (rv < 0)
		return "No termcap file found, try setting TERMPATH";
	if (rv == 0)
		return "Terminal type not found in termcap database";

	/* string capabilities */
	parsetab['\n'].pt_action = AC_PRINT;
	parsetab['\r'].pt_action = AC_PRINT;
	for (c = 32; c < 127; c++)	/* initialize printables */
		parsetab[c].pt_action = AC_PRINT;

	for (tp = tcaps; tp->tc_name[0]; tp++) {
		if (!(cp = get_strcap(tp->tc_name)))
			continue;
		if (!tp->tc_rep)
			ERR_RET("Termcap '%s' capability is unsupported",
				tp->tc_name);
		if (cp[1] || !cp[0])	/* not a one-character value? */
			ERR_RET("Termcap '%s' capability has unsupported value",
				tp->tc_name);
		parsetab[cp[0]].pt_action = AC_CONST;
		parsetab[cp[0]].pt_arg = tp->tc_rep;
	}
	if (cp = get_strcap("cl")) {
		switch (strlen(cp)) {
		    case 1:		/* single character is OK */
			parsetab[cp[0]].pt_action = AC_CONST;
			parsetab[cp[0]].pt_arg = ANSI_CLEAR;
			break;

		    case 2:
			/* implemented as 'ho' plus 'cd'? */
			p1 = parsetab + cp[0];
			p2 = parsetab + cp[1];
			if (p1->pt_action == AC_CONST &&
			    strcmp(p1->pt_arg, ANSI_HOME) == 0 &&
			    p2->pt_action == AC_CONST &&
			    strcmp(p2->pt_arg, ANSI_CLEAR_BELOW) == 0)
				break;	/* yes, already handled */
			/* no, unsupported */
			/* FALL THRU */

		    default:
			return "Termcap 'cl' capability has unsupported value";
			break;
		}
	}

	/* numeric capabilities */
	term_cols = tgetnum("co");
	term_lines = tgetnum("li");
	if (term_cols <= 0)
		return "Columns not valid in termcap entry";
	if (term_lines <= 0)	/* not set, use current screen size */
		term_lines = ws->ws_row;
	ws->ws_row = term_lines;
	ws->ws_col = term_cols;

	/* Boolean capabilities */
	term_am = tgetflag("am");
	if (tgetflag("bs") && parsetab['\b'].pt_action == AC_IGNORE) {
		parsetab['\b'].pt_action = AC_CONST;
		parsetab['\b'].pt_arg = "\b";
	}
	if (tgetflag("os"))
		return "Termcap 'os' capability is unsupported";

	/* all done */
	term_set = 1;
	return NULL;

#undef ERR_RET
}


/* read output from slave pty, write to user */
int handle_output(int mfd)
{
	int i, rc, rv = 0;
	char c, buf[128];
	struct pentry *pp;

	if ((rc = read(mfd, buf, sizeof buf)) <= 0)
		return rc;
	if (savefd >= 0)
		write(savefd, buf, rc);
	for (i = 0; i < rc; i++) {
		if (odelay.tv_nsec)
			(void) nanosleep(&odelay, NULL);
		if (!term_set) {
			if ((rv = write(STDOUT_FILENO, buf+i, 1)) < 0)
				break;
			continue;
		}
		c = buf[i] & 0x7f;	/* strip parity bit */
		pp = parsetab + c;
		switch (pp->pt_action) {
		    case AC_IGNORE:
			break;

		    case AC_CONST:
			rv = dprintf(STDOUT_FILENO, "%s", pp->pt_arg);
			break;

		    default:	/* AC_PASSTHRU */
			rv = write(STDOUT_FILENO, &c, 1);
			break;
		}
		if (rv < 0)
			break;
	}
	return rv;
}
