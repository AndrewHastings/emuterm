/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 */

/*
 * Emulate an old terminal by handling its output escape sequences.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <pty.h>
#include <sys/select.h>
#include "emuterm.h"
#include "input.h"
#include "output.h"


void cleanup(int sig)
{
	/* Restore user terminal size, leave raw mode. */
	omode(0);

	if (sig)
		exit(1);
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
