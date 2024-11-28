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
#include <stdlib.h>
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
#define ANSI_LEFT	    "\e[D"	    /* acts differently than '\b'? */
#define ANSI_SCROLL_UP	    "\e[S"
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


/* parse table for output */
enum action {
	AC_IGNORE = 0,		  /* no action (default) */
	AC_PRINT = AC_IGNORE+1,	  /* print as-is */
	AC_NEXT = AC_PRINT+1,	  /* continue to next parse table */
	AC_FMT,			  /* print constant string */
	AC_FMT1 = AC_FMT+1,	  /* format string w/1 integer argument */
	AC_FMT2 = AC_FMT1+1,	  /* format string w/2 integer arguments */
	AC_FMT2_REV = AC_FMT2+1,  /* AC_FMT2 w/args swapped */
};

enum state {
	ST_NEXT = 0,		  /* continue to next step */
	ST_GET_1C,		  /* consume 1 char,   for %. and %+ */
	ST_GET_DIGITS,		  /* any # of digits,  for %d */
	ST_GET_3D,		  /* consume 3 digits, for %3 */
	ST_GET_2D = ST_GET_3D+1,  /* consume 2 digits, for %2 and %3 */
	ST_GET_1D = ST_GET_2D+1,  /* consume 1 digit,  for %2 and %3 */
};

struct pentry {
	struct step {
		short	    pt_inc;	/* termcap %i or %+X */
		enum state  pt_initial;
	} pt_steps[2];			/* arg parsing steps */
	short		pt_nsteps;
	enum action	pt_action;
	void		*pt_ptr;	/* fmt or next parsetab */
} parsetab[128] = { { .pt_action = AC_IGNORE, .pt_ptr = NULL } };


void dump_pt(struct pentry *pt, int indent)
{
	struct pentry *pp;
	int i, j;
	char *s;

	for (i = 0, pp = pt; i < 128; i++, pp++) {
		if (pp->pt_action == ((pt == parsetab && i >= 32) ? AC_PRINT
								  : AC_IGNORE))
			continue;
		if (indent)
			fprintf(stderr, "%*s",  indent, "");
		fprintf(stderr, i >= 32 && i < 127 ? "  %c=" : "%03o=", i);
		for (j = 0; j < pp->pt_nsteps; j++) {
			fprintf(stderr, "%2.2s",
			       "nx1cdd3d2d1d" + 2*pp->pt_steps[j].pt_initial);
			if (pp->pt_steps[j].pt_inc)
				fprintf(stderr, "+%d", pp->pt_steps[j].pt_inc);
			fputc(',', stderr);
		}
		switch (pp->pt_action) {
		    case AC_IGNORE:
		        fprintf(stderr, "ignore"); break;
			break;

		    case AC_PRINT:
		        fprintf(stderr, "print"); break;
			break;

		    case AC_NEXT:
			fprintf(stderr, "{\n");
			dump_pt(pp->pt_ptr, indent+4);
			fprintf(stderr, "%*s}",  indent+4, "");
			break;

		    default:  /* AC_FMT* */
			fputc('"', stderr);
			for (s = pp->pt_ptr; *s; s++)
				fprintf(stderr,
					*s >= 32 && *s < 127 ? "%c" : "\\%03o",
					*s);
			fputc('"', stderr);
			if (pp->pt_action > AC_FMT)
				fprintf(stderr, ",%c",
						" 12R"[pp->pt_action - AC_FMT]);
		}
		fputc('\n', stderr);
	}
}


char *add_parse(char *val, enum action action, char *rep)
{
	struct pentry *pt = parsetab, *ep = NULL;
	struct step *step;
	int nargs;		    /* required # args */
	int nfound = 0;		    /* total # '%' formats */
	int incr = 0;		    /* '%i' present */
	char c;

	if (action < AC_FMT || action > AC_FMT2)
		return "internal error: action";
	nargs = action - AC_FMT;

	while (c = *val++) {
		if (c > 127)
			return "non-ASCII character";

		/* not an argument format? */
		if (c != '%' || *val == '%') {
			if (c == '%')		/* advance past "%%" */
				val++;

			if (!pt) {
				pt = calloc(128, sizeof(struct pentry));
				if (!pt)
					return "out of memory";
				ep->pt_ptr = pt;
			}

			ep = pt + c;
			if (ep->pt_action > AC_NEXT)
				return "conflict with another capability";
			pt = ep->pt_ptr;
			ep->pt_action = AC_NEXT;
			continue;
		}

		if (!ep)
			return "first character is an argument";
		step = ep->pt_steps + ep->pt_nsteps;
		if (step->pt_initial)
			return "conflict with another capability";
		switch (c = *val++) {
		    case '\0':
			return "% at end of value";

		    case '+':
			if (!(c = *val++))
				return "%+ at end of value";
			step->pt_inc = c;
			step->pt_initial = ST_GET_1C;
			break;

		    case '.':
			step->pt_initial = ST_GET_1C;
			break;

		    case '2':
			step->pt_initial = ST_GET_2D;
			break;

		    case '3':
			step->pt_initial = ST_GET_3D;
			break;

		    case 'd':
			if (!*val || *val >= '0' && *val <= '9'
				  || *val == '%' && val[1] != '%')
				return "%d must be followed by non-digit";
			step->pt_initial = ST_GET_DIGITS;
			break;

		    case 'i':
			incr = 1;
			continue;

		    case 'r':
			if (action == AC_FMT2_REV)
				return "%r multiple times";
			if (action != AC_FMT2)
				return "%r is not relevant here";
			action = AC_FMT2_REV;
			continue;

		    default:
			return "unsupported % escape";
		}

		if (++nfound > nargs)
			return "too many arguments";
		step->pt_inc += incr;
		ep->pt_nsteps++;
	}

	if (nfound != nargs)
		return "incorrect # args";
	if (ep->pt_action != AC_NEXT)
		return "internal error: next";
	ep->pt_action = action;
	ep->pt_ptr = rep;

	return NULL;
}


/* validate terminal type and initialize parse table */
struct tcap {
	char		tc_name[3];	/* capability name */
	enum action 	tc_action;
	char		*tc_rep;	/* xterm replacement */
} tcaps[] = {
	"al", AC_FMT,    "\e[L",	/* ANSI insert line */
	"bc", AC_FMT,    "\b",		/* ^H */
	"bl", AC_FMT,    "\a",		/* ^G */
	"cd", AC_FMT,    ANSI_CLEAR_BELOW,
	"ce", AC_FMT,    "\e[K",	/* ANSI clear to right */
	"cl", AC_FMT,    ANSI_CLEAR,
	"cm", AC_FMT2,   "\e[%d;%dH",	/* ANSI position cursor */
	"cr", AC_FMT,    "\r",		/* ^M */
	"cs", AC_IGNORE, NULL,		/* unsupported */
	"dc", AC_FMT,    "\e[P",	/* ANSI delete character */
	"dl", AC_FMT,    "\e[M",	/* ANSI delete line */
	"do", AC_FMT,    "\n",		/* ^J */
	"ho", AC_FMT,    ANSI_HOME,
	"ic", AC_FMT,    "\e[@",	/* ANSI insert character */
	"ke", AC_FMT,	 "",		/* ignore */
	"ks", AC_FMT,	 "",		/* ignore */
	"le", AC_FMT,    ANSI_LEFT,
	"ll", AC_IGNORE, NULL,		/* unsupported */
	"nd", AC_FMT,    "\e[C",	/* ANSI right */
	"se", AC_FMT,    "\e[m",	/* ANSI normal */
	"so", AC_FMT,    "\e[7m",	/* ANSI inverse */
	"ta", AC_FMT,    "\t",		/* ^I */
	"up", AC_FMT,    "\e[A",	/* ANSI up */
	"",   0, NULL
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
	char *cp, *err;
	struct tcap *tp;
	int c, rv;

	rv = tgetent(tbuf, term);
	if (rv < 0)
		return "No termcap file found, try setting TERMPATH";
	if (rv == 0)
		return "Terminal type not found in termcap database";

	/* string capabilities */
	parsetab['\n'].pt_action = AC_PRINT;
	parsetab['\r'].pt_action = AC_PRINT;
	for (c = 32; c < 127; c++)	/* initialize printable chars */
		parsetab[c].pt_action = AC_PRINT;

	for (tp = tcaps; tp->tc_name[0]; tp++) {
		if (!(cp = get_strcap(tp->tc_name)))
			continue;
		if (!tp->tc_rep) {
			sprintf(errbuf,
				"Termcap '%s' capability is unsupported",
				tp->tc_name);
			return errbuf;
		}
		err = add_parse(cp, tp->tc_action, tp->tc_rep);
		if (err) {
			sprintf(errbuf,
				"Termcap '%s' capability unsupported: %s",
				tp->tc_name, err);
			return errbuf;
		}
	}
	if (cp = get_strcap("sf")) {
		char *down;

		/* if "sf" is not newline and not the same as "do", add it */
		if (strcmp(cp, "\n") != 0 &&
		    (down = get_strcap("do")) &&
		     strcmp(cp, down) != 0) {
			if (err = add_parse(cp, AC_FMT, ANSI_SCROLL_UP)) {
				sprintf(errbuf, "Termcap 'sf' capability "
						"unsupported: %s", err);
				return errbuf;
			}
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
	if (tgetnum("sg") > 0)
		return "Termcap 'sg' capability is unsupported";

	/* Boolean capabilities */
	term_am = tgetflag("am");
	if (tgetflag("bs")) {
		struct pentry *pp = parsetab + '\b';

		switch (pp->pt_action) {
		    case AC_IGNORE:
		    case AC_PRINT:
			pp->pt_action = AC_PRINT;
			break;

		    case AC_FMT:
			/* if "le" capability is already ^H, use ^H for both */
			if (strcmp(pp->pt_ptr, ANSI_LEFT) == 0) {
				pp->pt_action = AC_PRINT;
				break;
			}
			/* FALL THRU */
		    default:
			return "Termcap 'bs' capability: "
			       "conflict with another capability";
		}
	}
	if (tgetflag("os"))
		return "Termcap 'os' capability is unsupported";

	/* all done */
	term_set = 1;
	return NULL;
}


/* read output from slave pty, write to user */
int handle_output(int mfd)
{
	static struct pentry *pt = parsetab;
	static struct pentry *pp = NULL;
	static int nump = 0, p[2] = { 0, 0 };
	static enum state state;
	static int step;
	int i, t, rc, rv = 0;
	char c, buf[128];

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

next_level:
		if (!pp) {
			pp = pt + c;
			step = 0;
			state = pp->pt_steps[0].pt_initial;
			if (pp->pt_nsteps > 0)
				continue;    /* to next char & start steps */
			/* else, fall through to do action for this char */
		}

		/* do steps for this character */
		if (step < pp->pt_nsteps) {
			int v = c - '0';

			if (nump >= 2) {
				fprintf(stderr, "\r\ninternal error: params\n");
				dump_pt(pt, 0);
				return -1;
			}
			switch (state) {
			    case ST_NEXT:
				fprintf(stderr, "\r\ninternal error: state\n");
				dump_pt(pt, 0);
				return -1;

			    case ST_GET_DIGITS:
				if (v < 0 || v > 9)
					break;	/* end of step */
				p[nump] = p[nump]*10 + v;
				continue;	/* to process next char */

			    case ST_GET_1C:
				p[nump] = c;
				break;		/* end of step */

			    case ST_GET_3D:
			    case ST_GET_2D:
				if (v < 0 || v > 9)
					v = 0;
				p[nump] = p[nump]*10 + v;
				state++;
				continue;	/* to process next char */

			    case ST_GET_1D:
				if (v < 0 || v > 9)
					v = 0;
				p[nump] = p[nump]*10 + v;
				break;		/* end of step */
			}

			p[nump] -= pp->pt_steps[step].pt_inc;
			if (p[nump] < 0)
				p[nump] = 0;
			nump++;

			/* if more steps, start it */
			if (++step < pp->pt_nsteps) {
				state = pp->pt_steps[step].pt_initial;
				continue;
			}

			/*
			 * %d ends after reading a non-digit that follows.
			 * That non-digit can't be un-read, so proceed
			 * immediately to the parse table for that character.
			 */
			if (state == ST_GET_DIGITS) {
				if (pp->pt_action != AC_NEXT) {
					/* add_parse should have ensured this */
					fprintf(stderr, "\r\ninternal error: "
							"%%d\n");
					dump_pt(pt, 0);
					return -1;
				}
				pt = pp->pt_ptr;
				pp = NULL;
				goto next_level;
			}

			/* fall through to do action */
		}

#pragma GCC diagnostic ignored "-Wformat-security"
		switch (pp->pt_action) {
		    case AC_IGNORE:
			break;

		    case AC_PRINT:
			rv = write(STDOUT_FILENO, &c, 1);
			break;

		    case AC_FMT:
			rv = dprintf(STDOUT_FILENO, (char *)pp->pt_ptr);
			break;

		    case AC_FMT1:
			if (nump != 1) {
				fprintf(stderr, "\r\ninternal error: fmt1\n");
				dump_pt(pt, 0);
				return -1;
			}

			/* these are usually # rows, # cols, or # chars */
			rv = dprintf(STDOUT_FILENO, (char *)pp->pt_ptr, p[0]);
			break;

		    case AC_FMT2_REV:
			/* swap args */
			t = p[0];
			p[0] = p[1];
			p[1] = t;
			/* FALL THRU */
		    case AC_FMT2:
			if (nump != 2) {
				fprintf(stderr, "\r\ninternal error: fmt2\n");
				dump_pt(pt, 0);
				return -1;
			}

			/* ensure in range */
			p[0] %= term_lines;
			p[1] %= term_cols;

			/* termcap row, col are 0-based, ANSI is 1-based */
			rv = dprintf(STDOUT_FILENO, (char *)pp->pt_ptr,
						    p[0]+1, p[1]+1);
			break;

		    case AC_NEXT:
			pt = pp->pt_ptr;
			pp = NULL;
			continue;
		}
#pragma GCC diagnostic pop

		if (rv < 0)
			break;
		pt = parsetab;
		pp = NULL;
		nump = p[0] = p[1] = 0;
	}
	return rv;
}
