/*
 * Copyright 2024, 2025 Andrew B. Hastings. All rights reserved.
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
#define ANSI_NORMAL	    "\e[m"
#define ANSI_BOLD	    "\e[1m"
#define ANSI_INVERSE	    "\e[7m"
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

int term_set, term_am, term_hz;
int term_cols, term_lines;

char *term_arrows[4] = {"", "", "", ""};    /* up, down, right, left */
char arrow_caps[] = "kukdkrkl";


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
	AC_NEXT,		  /* continue to next parse table */
	AC_PRINT = AC_NEXT+1,	  /* print as-is */
	AC_FMT,			  /* print constant string */
	AC_FMT1 = AC_FMT+1,	  /* format string w/1 integer argument */
	AC_FMT2 = AC_FMT1+1,	  /* format string w/2 integer arguments */
	AC_FMT2_REV = AC_FMT2+1,  /* AC_FMT2 w/args swapped */
	AC_LL = AC_FMT2_REV+1,    /* format string w/#lines */
	AC_STLINE = AC_LL+1,	  /* AC_FMT, optional argument ignored */
};

enum state {
	ST_UNSET = 0,
	ST_NEXT,		  /* continue to next step */
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
	char		pt_cap[2];	/* capability that this came from */
	enum action	pt_action;
	void		*pt_ptr;	/* fmt or next parsetab */
} parsetab[128] = { { .pt_action = AC_IGNORE, .pt_ptr = NULL } };


void dump_pt(struct pentry *pt, int indent)
{
	struct pentry *pp;
	int i, j;
	unsigned char *s;

	for (i = 0, pp = pt; i < 128; i++, pp++) {
		if (pp->pt_action == ((pt == parsetab && i >= 32) ? AC_PRINT
								  : AC_IGNORE))
			continue;
		if (indent)
			fprintf(stderr, "%*s",  indent, "");
		fprintf(stderr, i > 32 && i < 127 ? "  %c=" : "%03o=", i);
		for (j = 0; j < pp->pt_nsteps; j++) {
			fprintf(stderr, "%2.2s",
			       "--nx1cdd3d2d1d" + 2*pp->pt_steps[j].pt_initial);
			if (pp->pt_steps[j].pt_inc)
				fprintf(stderr, "+%d", pp->pt_steps[j].pt_inc);
			fputc(',', stderr);
		}
		switch (pp->pt_action) {
		    case AC_IGNORE:
		        fprintf(stderr, "ignore"); break;
			break;

		    case AC_NEXT:
			fprintf(stderr, "{\r\n");
			dump_pt(pp->pt_ptr, indent+4);
			fprintf(stderr, "%*s}",  indent+4, "");
			break;

		    case AC_PRINT:
		        fprintf(stderr, "print"); break;
			break;

		    default:  /* AC_FMT*, AC_LL, AC_STLINE */
			fputc('"', stderr);
			for (s = pp->pt_ptr; *s; s++)
				fprintf(stderr, *s == '\\' ? "\\%c" :
					*s >= 32 && *s < 127 ? "%c" : "\\%03o",
					*s);
			fputc('"', stderr);
			if (pp->pt_action > AC_FMT)
				fprintf(stderr, ",%c",
					      " 12RLS"[pp->pt_action - AC_FMT]);
		}
		fprintf(stderr, " [%2.2s]\r\n", pp->pt_cap);
	}
}


char *add_parse(char *cap, char *val, enum action action, char *rep)
{
	static char msg[128];
	struct pentry *pt = parsetab, *ep = NULL;
	struct step *step;
	int nargs = 0;		    /* required # args */
	int nfound = 0;		    /* total # '%' formats */
	int incr = 0;		    /* '%i' present */
	unsigned char c;

	if (debug > 1) {
		unsigned char *s;

		fprintf(stderr, "add %2.2s=", cap);
		for (s = val; *s; s++)
			fprintf(stderr,
				*s >= 32 && *s < 127 ? "%c" : "\\%03o", *s);
		fprintf(stderr, "\r\n");
		dump_pt(parsetab, 2);
	}

	/* ignore capabilities with empty values (typically 'im', 'ei') */
	if (!*val)
		return NULL;

	switch (action) {
	    case AC_FMT:
		if (val[0] == rep[0] && !val[1] && !rep[1]) {
			/* shortcut: print single char as-is */
			nargs = 0;
			action = AC_PRINT;
			rep = NULL;
			break;
		}
		/* FALL THRU */
	    case AC_FMT1:
	    case AC_FMT2:
		nargs = action - AC_FMT;
		break;

	    case AC_LL:
		break;

	    case AC_STLINE:
		nargs = 1;		/* max */
		break;

	    default:
		if (debug) {
			fprintf(stderr, "internal error: action");
			abort();
		}
		return "internal error: action";
	}

	while (c = *val++) {
		if (c == u'\200')	/* embedded NUL in control seq. */
			c = 0;
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
				ep->pt_cap[0] = cap[0];
				ep->pt_cap[1] = cap[1];
				ep->pt_ptr = pt;
				if (ep->pt_nsteps < 2)
					ep->pt_steps[ep->pt_nsteps].pt_initial
								     = ST_NEXT;
			}

			ep = pt + c;
			if (ep->pt_action > AC_NEXT &&
			    (ep->pt_action != action || ep->pt_ptr != rep)) {
				sprintf(msg, "conflict with '%2.2s' capability",
					     ep->pt_cap);;
				return msg;
			}
			pt = ep->pt_ptr;
			ep->pt_action = AC_NEXT;
			continue;
		}

		if (!ep)
			return "first character is an argument";
		step = ep->pt_steps + ep->pt_nsteps;
		if (step->pt_initial) {
			sprintf(msg, "conflict with '%2.2s' capability",
				     ep->pt_cap);
			return msg;
		}
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

	if (action != AC_STLINE && nfound != nargs)
		return "incorrect # args";
	if (ep->pt_action != AC_NEXT) {
		if (debug) {
			fprintf(stderr, "internal error: next");
			abort();
		}
		return "internal error: next";
	}
	if (!ep->pt_cap[0]) {
		ep->pt_cap[0] = cap[0];
		ep->pt_cap[1] = cap[1];
	}
	ep->pt_action = action;
	ep->pt_ptr = rep;

	return NULL;
}


/* validate terminal type and initialize parse table */
#define E(x) x, "»" x
#define N(x) x, x
#define S(x) x, x "«"
struct tcap {
	char		tc_name[3];	/* capability name */
	enum action 	tc_action;
	char		*tc_rep[2];	/* xterm replacement */
} tcaps[] = {
	"al", AC_FMT,    N("\e[L"),	/* ANSI insert line */
	"bc", AC_FMT,    N("\b"),	/* ^H */
	"bl", AC_FMT,    N("\a"),	/* ^G */
	"bt", AC_FMT,    N("\e[Z"),	/* ANSI reverse tab */
	"cd", AC_FMT,    N(ANSI_CLEAR_BELOW),
	"ce", AC_FMT,    N("\e[K"),	/* ANSI clear to right */
	"cl", AC_FMT,    N(ANSI_CLEAR),
	"cm", AC_FMT2,   N("\e[%d;%dH"),/* ANSI position cursor */
	"cr", AC_FMT,    N("\r"),	/* ^M */
	"cs", AC_IGNORE, N(NULL),	/* unsupported */
	"dc", AC_FMT,    N("\e[P"),	/* ANSI delete character */
	"dl", AC_FMT,    N("\e[M"),	/* ANSI delete line */
	"do", AC_FMT,    N("\n"),	/* ^J */
	"ds", AC_FMT,    N(""),		/* ignore */
	"ei", AC_FMT,    N("\e[4l"),	/* ANSI replace mode */
	"fs", AC_FMT,    N("\e\\"),	/* DEC string terminator */
	"ic", AC_FMT,    N("\e[@"),	/* ANSI insert character */
	"im", AC_FMT,    N("\e[4h"),	/* ANSI insert mode */
	"ke", AC_FMT,    N(""),		/* ignore */
	"ks", AC_FMT,    N(""),		/* ignore */
	"ll", AC_LL,     N(ANSI_SET_ROW),
	"mb", AC_FMT,    N("\e[5m"),	/* ANSI blink */
	"mh", AC_FMT,    N("\e[2m"),	/* ANSI faint */
	"me", AC_FMT,    E(ANSI_NORMAL),
	"mr", AC_FMT,    S(ANSI_INVERSE),
	"nd", AC_FMT,    N("\e[C"),	/* ANSI right */
	"rc", AC_FMT,    N("\e8"),	/* DEC restore cursor position */
	"sc", AC_FMT,    N("\e7"),	/* DEC save cursor position */
	"se", AC_FMT,    E(ANSI_NORMAL),
	"ta", AC_FMT,    N("\t"),	/* ^I */
	"ts", AC_STLINE, N("\e]0;"),	/* xterm set title */
	"ue", AC_FMT,    E(ANSI_NORMAL),
	"up", AC_FMT,    N("\e[A"),	/* ANSI up */
	"us", AC_FMT,    S("\e[4m"),	/* ANSI underscore */
	"ve", AC_FMT,    N(""),		/* ignore */
	"vi", AC_FMT,    N(""),		/* ignore */
	"vs", AC_FMT,    N(""),		/* ignore */
	"",   0,         N(NULL)
};
#undef E
#undef N
#undef S


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
	if (*rv == '*')
		rv++;
	return rv;
}


/* returns "cm" to row 0 col 0 without using "up" or "le" capabilities */
char *tgoto_home(void)
{
	static char buf[64];
	char *fmt, *s = buf;
	unsigned tmp, a1 = 0, a2 = 0;
	char c;

	if (!(fmt = get_strcap("cm")))		/* no "cm"? */
		return NULL;

	while (c = *fmt++) {
		/* not an argument format? */
		if (c != '%' || *fmt == '%') {
			*s++ = c;
			if (c == '%')		/* advance past "%%" */
				fmt++;
			continue;
		}

		switch (c = *fmt++) {
		    case '+':
			if (!(c = *fmt++))	/* premature end of format */
				return NULL;
			a1 += c;
			/* FALL THRU */
		    case '.':
			*s++ = a1 ? a1 : 0200;	/* use '\200' instead of '\0' */
			break;

		    case '2':
			s += sprintf(s, "%02u", a1);
			break;

		    case '3':
			s += sprintf(s, "%03u", a1);
			break;

		    case 'd':
			s += sprintf(s, "%u", a1);
			break;

		    case 'i':
			a1++;
			a2++;
			continue;

		    case 'r':
			tmp = a1;
			a1 = a2;
			a2 = tmp;
			continue;

		    default:			/* invalid format char */
			return NULL;
		}

		a1 = a2;			/* get next argument */
	}

	*s = '\0';
	return buf;
}


char *set_termtype(char *term, struct winsize *ws, char *errbuf)
{
	static char tbuf[2048];	/* returned termcap entry */
	char *cp, *err, *s;
	struct tcap *tp;
	int c, has_sg, rv;

	rv = tgetent(tbuf, term);
	if (rv < 0)
		return "No termcap file found, try setting TERMPATH";
	if (rv == 0)
		return "Terminal type not found in termcap database";

	parsetab['\n'].pt_action = AC_PRINT;
	parsetab['\r'].pt_action = AC_PRINT;
	for (c = 32; c < 127; c++)	/* initialize printable chars */
		parsetab[c].pt_action = AC_PRINT;

	/* Boolean capabilities */
	term_am = tgetflag("am");
	if (tgetflag("bs")) {
		struct pentry *pp = parsetab + '\b';

		pp->pt_action = AC_PRINT;
		pp->pt_cap[0] = 'b';
		pp->pt_cap[1] = 's';
	}
	if (tgetflag("hz")) {
		parsetab['~'].pt_action = AC_IGNORE;
		term_hz = 1;
	}
	if (tgetflag("os"))
		return "Termcap 'os' capability is unsupported";
	if (tgetflag("pt")) {
		struct pentry *pp = parsetab + '\t';

		pp->pt_action = AC_PRINT;
		pp->pt_cap[0] = 'p';
		pp->pt_cap[1] = 't';
	}
	if (tgetflag("x7")) {			/* CDC 713 glitch */
		struct pentry *pp;

		pp = parsetab + '\003';		/* ETX */
		pp->pt_action = AC_FMT;
		pp->pt_cap[0] = 'x';
		pp->pt_cap[1] = '7';
		pp->pt_ptr = "▲";

		pp = parsetab + '\177';		/* DEL */
		pp->pt_action = AC_FMT;
		pp->pt_cap[0] = 'x';
		pp->pt_cap[1] = '7';
		pp->pt_ptr = "■";
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
	has_sg = tgetnum("sg");
	if (has_sg > 1)
		return "Termcap 'sg' capability > 1 is unsupported";
	if (has_sg < 0)
		has_sg = 0;
	if (!has_sg && tgetnum("ug") > 0)
		return "Termcap 'ug' without 'sg' capability is unsupported";

	/* string capabilities */
	for (tp = tcaps; tp->tc_name[0]; tp++) {
		if (!(cp = get_strcap(tp->tc_name)))
			continue;
		if (!tp->tc_rep[has_sg]) {
			sprintf(errbuf,
				"Termcap '%s' capability is unsupported",
				tp->tc_name);
			return errbuf;
		}
		err = add_parse(tp->tc_name, cp, tp->tc_action,
				tp->tc_rep[has_sg]);
		if (err) {
			sprintf(errbuf,
				"Termcap '%s' capability unsupported: %s",
				tp->tc_name, err);
			return errbuf;
		}
	}

	/* if "ho" differs from "cm" to (0,0), add it */
	if (cp = get_strcap("ho")) {
		if (!(s = tgoto_home()) || strcmp(cp, s) != 0) {
			if (err = add_parse("ho", cp, AC_FMT, ANSI_HOME)) {
				sprintf(errbuf, "Termcap 'ho' capability "
						"unsupported: %s", err);
				return errbuf;
			}
		}
	}

	/* if "le" differs from "bs" and "bc", add it */
	if (cp = get_strcap("le")) {
		if ((!tgetflag("bs") || strcmp(cp, "\b") != 0) &&
		    (!(s = get_strcap("bc")) || strcmp(cp, s) != 0)) {
			if (err = add_parse("le", cp, AC_FMT, ANSI_LEFT)) {
				sprintf(errbuf, "Termcap 'le' capability "
						"unsupported: %s", err);
				return errbuf;
			}
		}
	}

	/* if "sf" differs from "do" and newline, add it */
	if (cp = get_strcap("sf")) {
		if (strcmp(cp, "\n") != 0 &&
		    (!(s = get_strcap("do")) || strcmp(cp, s) != 0)) {
			if (err = add_parse("sf", cp, AC_FMT, ANSI_SCROLL_UP)) {
				sprintf(errbuf, "Termcap 'sf' capability "
						"unsupported: %s", err);
				return errbuf;
			}
		}
	}

	/* if "md" differs from "mr", add it */
	if (cp = get_strcap("md")) {
		if (!(s = get_strcap("mr")) || strcmp(cp, s) != 0) {
			s = has_sg ? ANSI_BOLD "«" : ANSI_BOLD;
			if (err = add_parse("md", cp, AC_FMT, s)) {
				sprintf(errbuf, "Termcap 'md' capability "
						"unsupported: %s", err);
				return errbuf;
			}
		}
	}

	/* if "so" differs from "md", "mr", and "us", add it */
	if (cp = get_strcap("so")) {
		if ((!(s = get_strcap("md")) || strcmp(cp, s) != 0) &&
		    (!(s = get_strcap("mr")) || strcmp(cp, s) != 0) &&
		    (!(s = get_strcap("us")) || strcmp(cp, s) != 0)) {
			s = has_sg ? ANSI_INVERSE "«" : ANSI_INVERSE;
			if (err = add_parse("so", cp, AC_FMT, s)) {
				sprintf(errbuf, "Termcap 'so' capability "
						"unsupported: %s", err);
				return errbuf;
			}
		}
	}

	/* arrow keys */
	for (c = 0; c < 4; c++) {
		if (cp = get_strcap(arrow_caps + c*2))
			term_arrows[c] = cp;
	}

	/* all done */
	term_set = 1;
	if (debug) {
		if (debug > 1)
			fprintf(stderr, "parsetab:\n");
		dump_pt(parsetab, 0);
		for (c = 0; c < 4; c++) {
			fprintf(stderr, "%2.2s=\"", arrow_caps + c*2);
			for (s = term_arrows[c]; *s; s++)
				fprintf(stderr, *s == '\\' ? "\\%c" :
					*s >= 32 && *s < 127 ? "%c" : "\\%03o",
					*s);
			fprintf(stderr, "\"\n");
		}
	}
	return NULL;
}


/* read output from slave pty, write to user */
int handle_output(int mfd)
{
	static struct pentry *pt = parsetab;
	static struct pentry *pp = NULL;
	static int nump = 0, p[2] = { 0, 0 };
	static char prevc = -1;
	static enum action prev_action = -1;
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
		if (debug > 2 && prevc >= 0)
			fprintf(stderr, prevc == '\\' ? "\\%c" :
			   prevc > 32 && prevc < 127 ? "%c" : "\\%03o", prevc);
		prevc = c = buf[i] & 0x7f;	/* strip parity bit */

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
				fprintf(stderr, "\r\ninternal error: params\r\n");
				dump_pt(pt, 0);
				if (debug)
					abort();
				return -1;
			}
			switch (state) {
			    case ST_UNSET:
			    case ST_NEXT:
				fprintf(stderr, "\r\ninternal error: state\r\n");
				dump_pt(pt, 0);
				if (debug)
					abort();
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
							"%%d\r\n");
					dump_pt(pt, 0);
					if (debug)
						abort();
					return -1;
				}
				pt = pp->pt_ptr;
				pp = NULL;
				goto next_level;
			}

			/* fall through to do action */
		}

		/* log the action for this character sequence */
		if (debug > 2) {
			/* log contiguous PRINT actions in a single group */
			if (prev_action == AC_PRINT &&
			    pp->pt_action != prev_action)
				fprintf(stderr, " PRT\r\n");
			if (pp->pt_action == AC_PRINT)
				goto log_end;

			fprintf(stderr, c == '\\' ? "\\%c" :
				c > 32 && c < 127 ? "%c" : "\\%03o", c);
			prevc = -1;
			if (pp->pt_action == AC_NEXT)
				goto log_end;

			fprintf(stderr, " [%2.2s] ", pp->pt_cap);
			switch (pp->pt_action) {
			    case AC_IGNORE:   fprintf(stderr, "IGN\r\n"); break;
			    case AC_FMT:      fprintf(stderr, "FMT\r\n"); break;
			    case AC_FMT1:     fprintf(stderr, "FM1\r\n"); break;
			    case AC_FMT2:     fprintf(stderr, "FM2\r\n"); break;
			    case AC_FMT2_REV: fprintf(stderr, "F2R\r\n"); break;
			    case AC_LL:	      fprintf(stderr, "LL \r\n"); break;
			    case AC_STLINE:   fprintf(stderr, "STL\r\n"); break;
			    default:	      fprintf(stderr, "?%d?\r\n",
							      pp->pt_action);
			}

log_end:
			prev_action = pp->pt_action;
		}

#pragma GCC diagnostic ignored "-Wformat-security"
		switch (pp->pt_action) {
		    case AC_IGNORE:
			break;

		    case AC_PRINT:
			rv = write(STDOUT_FILENO, &c, 1);
			break;

		    case AC_FMT:
		    case AC_STLINE:
			rv = dprintf(STDOUT_FILENO, (char *)pp->pt_ptr);
			break;

		    case AC_FMT1:
			if (nump != 1) {
				fprintf(stderr, "\r\ninternal error: fmt1\r\n");
				dump_pt(pt, 0);
				if (debug)
					abort();
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
				fprintf(stderr, "\r\ninternal error: fmt2\r\n");
				dump_pt(pt, 0);
				if (debug)
					abort();
				return -1;
			}

			/* Hazeltine row/col. can be specified multiple ways */
			if (term_hz) {
				p[0] %= 32;
				p[1] %= 96;
			}

			/* ensure in range */
			p[0] = MIN(p[0], term_lines-1);
			p[1] = MIN(p[1], term_cols-1);

			/* termcap row, col are 0-based, ANSI is 1-based */
			rv = dprintf(STDOUT_FILENO, (char *)pp->pt_ptr,
						    p[0]+1, p[1]+1);
			break;

		    case AC_LL:
			rv = dprintf(STDOUT_FILENO, (char *)pp->pt_ptr,
						    term_lines);
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
