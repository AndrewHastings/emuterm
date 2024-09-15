/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 */

/*
 * Translate output from emulated terminal into ANSI escape sequences.
 */

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "emuterm.h"
#include "output.h"


#define ANSI_SCROLL_REGION  "\033[;%dr"
#define ANSI_SCROLL_RESET   "\033[r"
#define ANSI_RESIZE	    "\033[8;%d;%dt"
#define ANSI_UP		    "\033[A"
#define ANSI_DOWN	    "\033[B"
#define ANSI_RIGHT	    "\033[C"
#define ANSI_LEFT	    "\033[D"
#define ANSI_HOME	    "\033[H"
#define ANSI_CLEAR	    "\033[H\033[2J"


void omode(int emulate)
{
	static struct termios otio;
	static struct winsize ows;

	if (emulate) {
		struct termios ntio;

		/* save tty settings and enter raw mode */
		tcgetattr(STDIN_FILENO, &otio);
		ntio = otio;
		cfmakeraw(&ntio);
		tcsetattr(STDIN_FILENO, TCSANOW, &ntio);

		/* save window size, then resize user terminal */
		ioctl(STDIN_FILENO, TIOCGWINSZ, &ows);
#ifdef notyet	/* for some reason this doesn't always work */
		uprintf(ANSI_RESIZE, TERM_LINES, TERM_COLUMNS);
#else
		uprintf(ANSI_SCROLL_REGION, TERM_LINES);
#endif
	} else {
		/* restore user terminal size */
#ifdef notyet
		uprintf(ANSI_RESIZE, ows.ws_row, ows.ws_col);
#else
		uprintf(ANSI_SCROLL_RESET);
#endif

		/* restore tty settings */
		tcsetattr(STDIN_FILENO, TCSANOW, &otio);
	}
}


int savefd = -1;

void save_output(char *path)
{
	if (savefd >= 0) {
		if (!path || !path[0]) {
			uprintf("Recording stopped\r\n");
			close(savefd);
			savefd = -1;
			return;
		}

		uprintf("Recording already in progress, use ~w to stop\r\n");
		return;
	}

	if (!path)
		return;

	if (path[0] == ' ')	/* skip optional space after "~w" */
		path++;
	if (!path[0]) {
		uprintf("No recording in progress, use ~? for help\r\n");
		return;
	}

	if ((savefd = open(path, O_WRONLY|O_CREAT|O_APPEND, 0666)) < 0) {
		uprintf("%s: %s\r\n", path, strerror(errno));
		return;
	}
	uprintf("Recording to '%s'\r\n", path);
}


/* read output from slave pty, write to user */
int handle_output(int mfd)
{
	int i, rc, rv = 0;
	char c, buf[128];

	if ((rc = read(mfd, buf, sizeof buf)) <= 0)
		return rc;
	if (savefd >= 0)
		write(savefd, buf, rc);
	for (i = 0; i < rc; i++) {
		c = buf[i] & 0x7f;
		switch (c) {
		    case '\010':	/* ^H */
			rv = uprintf(ANSI_LEFT);
			break;

		    case '\011':	/* ^I */
			rv = uprintf(ANSI_RIGHT);
			break;

		    case '\013':	/* ^K */
			rv = uprintf(ANSI_UP);
			break;

		    case '\014':	/* ^L */
			rv = uprintf(ANSI_CLEAR);
			break;

		    case '\016':	/* ^N */
			rv = uprintf(ANSI_HOME);
			break;

		    case '\033':	/* ESC */
			break;

		    default:
			rv = write(STDOUT_FILENO, &c, 1);
			break;
		}
		if (rv < 0)
			return rv;
	}
	return rv;
}
