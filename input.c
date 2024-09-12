/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 */

/*
 * Handle input from user to emulated terminal.
 */

#include <unistd.h>
#define __USE_GNU /* for sighandler_t */
#include <signal.h>
#include "emuterm.h"
#include "input.h"
#include "output.h"


/* read input from user, write to slave pty */
int handle_input(int mfd)
{
	int ic, rv = 0;
	char buf[128], *bp;	/* user input */
	char obuf[512], *op;	/* output to user */
	char wbuf[128], *wp;	/* write to slave */
	sighandler_t osig;
	static char cmd[512] = "C", *cp = cmd+1;

	if ((ic = read(STDIN_FILENO, buf, sizeof buf)) <= 0)
		return ic;

	bp = buf;
	op = obuf;
	wp = wbuf;

	/*
	 * Iterate through user input, copy to slave by default.
	 * Once the start of a "~" command sequence is recognized,
	 * stop copying to slave and instead echo back to user.
	 * Command sequence is terminated by a newline or another "~".
	 */
	for ( ; bp < buf + ic; bp++) {
		char c = *bp;

		/* Command sequence must immediately follow newline. */
		if (cp == cmd) {
			if (c == '\r' || c == '\n')
				cp++;
			*wp++ = c;
			continue;
		}

		/* Is the next character "~"? */
		if (cp == cmd+1) {
			if (c == '~') {
				*cp++ = c;
				*op++ = c;
			} else {
				cp = cmd;
				*wp++ = c;
			}
			continue;
		}

		/* "~~" sends a single "~" */
		if (cp == cmd+2 && c == '~') {
			cp = cmd;
			*wp++ = c;
			continue;
		}

		/* Perform rudimentary line editing. */
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
		if (wp - wbuf)
			rv = write(mfd, wbuf, wp-wbuf);
		if (op - obuf)
			write(STDOUT_FILENO, obuf, op-obuf);
		wp = wbuf;
		op = obuf;
		cp = cmd+1;

		/* Handle command. */
		switch (c = cmd[2]) {
		    case '?': case 'h':
			uprintf("~~      send ~\r\n");
			uprintf("~?      help\r\n");
			uprintf("~.      quit\r\n");
			uprintf("~^Z     suspend\r\n");
			break;

		    case '.': case 'q':
			uprintf("emuterm: exiting\r\n");
			return -2;
			break;

		    case '\032':	/* ^Z */
			omode(0);
			osig = signal(SIGCHLD, SIG_IGN);
			kill(0, SIGTSTP);
			signal(SIGCHLD, osig);
			omode(1);
			break;

		    default:
			uprintf("emuterm: unrecognized command %s, "
				"~? for help\r\n", cp);
			break;
		}
	}

	/* Flush buffers. */
	if (wp - wbuf)
		rv = write(mfd, wbuf, wp-wbuf);
	if (op - obuf)
		write(STDOUT_FILENO, obuf, op-obuf);
	return rv;
}
