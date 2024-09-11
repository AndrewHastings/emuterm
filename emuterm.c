/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 */

/*
 * Emulate an old terminal by handling its output escape sequences.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <pty.h>
#include <sys/select.h>


#define TERM_TYPE	"digilog33"
#define TERM_LINES	16
#define TERM_COLUMNS	80

#define ANSI_SCROLL_REGION  "\033[;%dr"
#define ANSI_SCROLL_RESET   "\033[r"
#define ANSI_RESIZE	    "\033[8;%d;%dt"
#define ANSI_UP		    "\033[A"
#define ANSI_DOWN	    "\033[B"
#define ANSI_RIGHT	    "\033[C"
#define ANSI_LEFT	    "\033[D"
#define ANSI_HOME	    "\033[H"
#define ANSI_CLEAR	    "\033[H\033[2J"


/* unbuffered printf */
int uprintf(char *fmt, ...)
{
	va_list ap;
	int rc;
	char buf[256];

	va_start(ap, fmt);
	rc = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	return write(STDOUT_FILENO, buf, rc);
}
#pragma printflike uprintf


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


void cleanup(int sig)
{
	/* Restore user terminal size, leave raw mode. */
	omode(0);

	if (sig)
		exit(1);
}


/* read output from slave pty, write to user */
int handle_output(int mfd)
{
	int i, rc, rv = 0;
	char c, buf[128];

	if ((rc = read(mfd, buf, sizeof buf)) <= 0)
		return rc;
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


/* read input from user, write to slave pty */
int handle_input(int mfd)
{
	int rc;
	char buf[128];

	if ((rc = read(STDIN_FILENO, buf, sizeof buf)) <= 0)
		return rc;
	return write(mfd, buf, rc);
}


void pty_master(int mfd, pid_t cpid)
{
	int flags;

	/* Cleanup when child exits if we don't get some other error first. */
	signal(SIGCHLD, cleanup);

	/* Resize user terminal, enter raw mode, don't block on tty input. */
	omode(1);
	flags = fcntl(STDIN_FILENO, F_GETFL);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	for (;;) {
		fd_set rfds;

		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		FD_SET(mfd, &rfds);

		if (select(mfd+1, &rfds, NULL, NULL, NULL) < 0)
			break;

		if (FD_ISSET(STDIN_FILENO, &rfds))
			if (handle_input(mfd) < 0)
				break;

		if (FD_ISSET(mfd, &rfds))
			if (handle_output(mfd) < 0)
				break;
	}

	cleanup(0);
}


void pty_slave(char **argv)
{
	char *defargs[] = {"bash", NULL};

	putenv("TERM=" TERM_TYPE);
	if (!*argv)
		argv = defargs;
	execvp(argv[0], argv);
	printf("emuterm: %s: %s", argv[0], strerror(errno));
	exit(1);
}


void main(int argc, char **argv)
{
	int mfd;
	pid_t pid;
	struct termios tio;
	struct winsize ws;

	/* Copy current tty modes to emulated terminal. */
	tcgetattr(STDIN_FILENO, &tio);

	/* Set kernel's window size params to match emulated terminal. */
	memset(&ws, sizeof ws, 0);
	ws.ws_row = TERM_LINES;
	ws.ws_col = TERM_COLUMNS;

	if (pid = forkpty(&mfd, NULL, &tio, &ws)) {
		if (pid < 0) {
			perror("emuterm");
			exit(1);
		}
		pty_master(mfd, pid);
	} else {
		pty_slave(argv+1);
	}
	exit(0);
}
