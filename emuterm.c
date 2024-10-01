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
#include <stdio.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>
#include <pty.h>
#include "emuterm.h"
#include "input.h"
#include "output.h"


int sendfd = -1;

void send_file(char *path)
{
	if (path[0] == ' ')	/* skip optional space after "~r" */
		path++;
	if (!path[0]) {
		dprintf(STDOUT_FILENO, "emuterm: ~r requires a pathname\r\n");
		return;
	}

	if ((sendfd = open(path, O_RDONLY)) < 0) {
		dprintf(STDOUT_FILENO, "%s: %s\r\n", path, strerror(errno));
		return;
	}
	dprintf(STDOUT_FILENO, "Sending '%s'\r\n", path);
}


void end_send(struct pollfd *pfds)
{
	close(sendfd);
	sendfd = -1;
	pfds[0].events = POLLIN;
}


void cleanup(int sig)
{
	/* Stop recording, restore user terminal size, leave raw mode. */
	dprintf(STDOUT_FILENO, "\r\n");
	save_output(NULL);
	omode(0);

	if (sig) {
		psignal(sig, NULL);
		exit(1);
	}
}


void pty_master(int mfd, pid_t cpid)
{
	struct pollfd pfds[2];
	int npoll;
	int flags;

	pfds[0].fd = mfd;
	pfds[0].events = POLLIN;
	pfds[1].fd = STDIN_FILENO;
	pfds[1].events = POLLIN;
	npoll = 2;

	/* Cleanup if we don't get some other error first. */
	signal(SIGCHLD, cleanup);
	signal(SIGTERM, cleanup);

	/* Resize user terminal, enter raw mode, don't block on tty input. */
	omode(1);
	flags = fcntl(STDIN_FILENO, F_GETFL);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	dprintf(STDOUT_FILENO, "emuterm: escape character is ~\r\n");

	for (;;) {

		if (poll(pfds, npoll, -1) < 0)
			break;

		/* Output from slave? */
		if (pfds[0].revents & (POLLIN|POLLERR))
			if (handle_output(mfd) < 0)
				break;

		/* Not sending a file, handle user input normally. */
		if (sendfd < 0) {
			if (pfds[1].revents & (POLLIN|POLLERR))
				if (handle_input(mfd) < 0)
					break;
			if (sendfd < 0)
				continue;

			/* Now sending a file, set up poll. */
			pfds[0].events = POLLIN|POLLOUT;
			continue;
		}

		/* Any user input terminates file sending. */
		if (pfds[1].revents & (POLLIN|POLLERR)) {
			dprintf(STDOUT_FILENO,
				"\r\nUser terminated file send.\r\n");
			end_send(pfds);
			continue;
		}

		/* Slave ready for input from file? */
		if (pfds[0].revents & POLLOUT) {
			char buf[256];
			int ic;

			ic = read(sendfd, buf, sizeof buf);
			if (ic <= 0) {
				if (ic < 0)
					dprintf(STDOUT_FILENO,
					        "\r\nread: %s\r\n",
						strerror(errno));
				end_send(pfds);
				continue;
			}
			if (write(mfd, buf, ic) < 0) {
				dprintf(STDOUT_FILENO,
					"\r\nWrite to child failed: %s.\r\n",
					strerror(errno));
				break;
			}
		}
	}

	cleanup(0);

	/* Ensure child is dead. */
	signal(SIGCHLD, SIG_DFL);
	kill(cpid, SIGTERM);
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
