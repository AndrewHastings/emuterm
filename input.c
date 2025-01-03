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
 * Handle input from user to emulated terminal.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <termio.h>
#define __USE_GNU /* for sighandler_t */
#include <signal.h>
#include "emuterm.h"
#include "input.h"
#include "output.h"


/* read input from user, write to slave pty */
int handle_input(int mfd)
{
	int ic, nc, rv = 0;
	char buf[128], *bp;	/* user input */
	char obuf[512], *op;	/* output to user */
	char wbuf[256], *wp;	/* write to slave */
	sighandler_t osig;
	static char cmd[512] = "C", *cp = cmd+1; /* state 1 initially */

	if ((ic = read(STDIN_FILENO, buf, sizeof buf)) <= 0)
		return ic;

	bp = buf;
	op = obuf;
	wp = wbuf;

	/*
	 * Iterate through user input, copy to slave by default.
	 * - Once the start of a "~" command sequence is recognized,
	 *   stop copying to slave and instead echo back to user.
	 *   Command sequence is terminated by a newline or another "~".
	 * - If not in a "~" command sequence, recognize xterm arrow
	 *   keys and translate to emulated terminal (if emulating).
	 */
	for ( ; bp < buf + ic; bp++) {
		char cc, c = *bp;

		if (cp < cmd+2 &&	    /* not yet in state 2 */
		    term_set &&		    /* emulating */
		    c == '\033' &&	    /* starts with ESC */
		    bp + 2 < buf + ic &&    /* potential xterm key sequence */
		    (bp[1] == '[' || bp[1] == 'O')) {
			switch (cc = bp[2]) {
			    case 'A': case 'B': case 'C': case 'D':
				nc = strlen(term_arrows[cc - 'A']);
				memcpy(wp, term_arrows[cc - 'A'], nc);
				wp += nc;
				bp += 2;
				cp = cmd;   /* reset to state 0 */
				continue;
			}
		}

		/* State 0: check for newline */
		if (cp == cmd) {
			if (c == '\r' || c == '\n')
				cp++;	    /* advance to state 1 */
			*wp++ = c;
			continue;
		}

		/* State 1: check for "~" */
		if (cp == cmd+1) {
			if (c == '~') {
				*cp++ = c;  /* advance to state 2 */
				*op++ = c;
			} else {
				cp = cmd;   /* reset to state 0 */
				*wp++ = c;
			}
			continue;
		}

		/* State 2: check for "~~" */
		if (cp == cmd+2 && c == '~') {
			cp = cmd;	    /* reset to state 0 */
			*wp++ = c;
			continue;
		}

		/* State 2+: perform rudimentary line editing. */
		switch (c) {
		    case '\025': case '\030':	/* ^U, ^X: erase line */
			while (cp > cmd+1) {
				*op++ = '\b';
				*op++ = ' ';
				*op++ = '\b';
				cp--;
				if (*cp < ' ') {
					*op++ = '\b';
					*op++ = ' ';
					*op++ = '\b';
				}

			}
			continue;

		    case '\b': case '\177':	/* ^H, DEL: erase char */
			*op++ = '\b';
			*op++ = ' ';
			*op++ = '\b';
			cp--;
			if (*cp < ' ') {
				*op++ = '\b';
				*op++ = ' ';
				*op++ = '\b';
			}
			continue;

		    case '\r': case '\n':	/* newline */
			*op++ = '\r';
			*op++ = '\n';
			*cp = '\0';
			break;

		    default:
			if (c < ' ') {
				*op++ = '^';
				*op++ = c + '@';
			} else
				*op++ = c;
			*cp++ = c;
			continue;
		}

		/* Flush buffers. */
		if (wp - wbuf) {
			if (write(mfd, wbuf, wp-wbuf) < 0)
				rv = -1;
		}
		if (op - obuf)
			write(STDOUT_FILENO, obuf, op-obuf);
		wp = wbuf;
		op = obuf;
		cp = cmd+1;	/* back to state 1 after handling command */

		/* Handle command. */
		switch (c = cmd[2]) {
		    case '?': case 'h':
			dprintf(STDOUT_FILENO, "~~      send ~\r\n"
					       "~?      help\r\n"
					       "~.      quit\r\n"
					       "~^Z     suspend\r\n"
					       "~r FILE send file\r\n"
					       "~w FILE record raw output\r\n"
					       "~w      stop recording\r\n");
			break;

		    case '.': case 'q':
			save_output(NULL);
			dprintf(STDOUT_FILENO, "%s: exiting\r\n", prog);
			return -2;
			break;

		    case '\032':	/* ^Z */
			omode(0);
			osig = signal(SIGCHLD, SIG_IGN);
			kill(0, SIGTSTP);
			signal(SIGCHLD, osig);
			omode(1);
			break;

		    case 'r':
			send_file(cmd+3);
			break;

		    case 'w':
			save_output(cmd+3);
			break;

		    default:
			dprintf(STDOUT_FILENO, "%s: unrecognized command %s, "
				"~? for help\r\n", prog, cp);
			break;
		}
	}

	/* Flush buffers. */
	if (wp - wbuf) {
		if (write(mfd, wbuf, wp-wbuf) < 0)
			rv = -1;
	}
	if (op - obuf)
		write(STDOUT_FILENO, obuf, op-obuf);
	return rv;
}
