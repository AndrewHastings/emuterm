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
#include <time.h>
#include <alloca.h>
#include <stdio.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>
#include <pty.h>
#include "emuterm.h"
#include "input.h"
#include "output.h"


char *prog;
int resize_win = 0;
int term_cols, term_lines;
char *term_type = NULL;
struct timespec odelay = {0, 0};
int sendfd = -1;


void set_ospeed(struct termios *tio, int cps)
{
	static struct {
		int	o_bval;
		int	o_cps;
	} *spdp, speeds[] = {
	    B50,	5,
	    B75,	8,
	    B110,	10,
	    B134,	13,
	    B150,	15,
	    B200,	20,
	    B300,	30,
	    B600,	60,
	    B1200,	120,
	    B1800,	180,
	    B2400,	240,
	    B4800,	480,
	    B9600,	960,
	    B19200,	1920,
	    B38400,	3840,
	    B57600,	5760,
	    B115200,	11520,
	    B230400,	0
	};

	for (spdp = speeds; spdp->o_cps; spdp++) {
		if (cps <= spdp->o_cps)
			break;
	}
	cfsetospeed(tio, spdp->o_bval);
	odelay.tv_nsec = 1000000000 / cps;
}


void send_file(char *path)
{
	if (path[0] == ' ')	/* skip optional space after "~r" */
		path++;
	if (!path[0]) {
		dprintf(STDOUT_FILENO, "%s: ~r requires a pathname\r\n", prog);
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

	dprintf(STDOUT_FILENO, "%s: escape character is ~\r\n", prog);

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

	if (!*argv)
		argv = defargs;
	if (term_type) {
		char *term = alloca(strlen(term_type) + 6);
		sprintf(term, "TERM=%s", term_type);
		putenv(term);
	}
	execvp(argv[0], argv);
	printf("%s: %s: %s", prog, argv[0], strerror(errno));
	exit(1);
}


void usage(int ec)
{
	fprintf(stderr, "Usage: %s [-c cps] [-r] [-t termtype] [cmd args...]\n",
			prog);
	fprintf(stderr, "Default cmd: 'bash'\n");
	fprintf(stderr, " -c   specify output chars/sec (default no delay)\n");
	fprintf(stderr, " -r   resize X terminal (default change scroll region)\n");
	fprintf(stderr, " -t   emulated terminal type (default no emulation)\n");
	exit(ec);
}


void main(int argc, char **argv)
{
	int c;
	int mfd;
	pid_t pid;
	int ospeed = 0;
	struct termios tio;
	struct winsize ws;

	prog = strrchr(argv[0], '/');
	prog = prog ? prog+1 : argv[0];

	while ((c = getopt(argc, argv, "+:c:hrt:")) != -1) {
		switch (c) {
		    case 'c':
			if ((ospeed = atoi(optarg)) < 5) {
				fprintf(stderr, "cps must be >= 5\n");
				usage(1);
			}
			break;

		    case 'h':
			usage(0);
			break;

		    case 'r':
			resize_win = 1;
			break;

		    case 't':
			term_type = optarg;
			break;

		    case ':':
			fprintf(stderr, "option -%c requires an operand\n",
				optopt);
			usage(1);
			break;

		    case '?':
			fprintf(stderr, "unrecognized option -%c\n", optopt);
			usage(1);
			break;
		}
	}

	/* Copy current tty modes to emulated terminal. */
	tcgetattr(STDIN_FILENO, &tio);

	/* Set kernel's window size params to match emulated terminal. */
	if (term_type) {
		if (strcmp(term_type, "digilog33") != 0) {
			fprintf(stderr, "Only '-t digilog33' is supported\n");
			exit(1);
		}
		term_cols = 80;		/* XXX hardcoded for now */
		term_lines = 16;	/* XXX hardcoded for now */
		memset(&ws, sizeof ws, 0);
		ws.ws_row = term_lines;
		ws.ws_col = term_cols;
	} else
		ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);

	if (ospeed)
		set_ospeed(&tio, ospeed);
	if (pid = forkpty(&mfd, NULL, &tio, &ws)) {
		if (pid < 0) {
			perror(prog);
			exit(1);
		}
		pty_master(mfd, pid);
	} else {
		pty_slave(argv+optind);
	}
	exit(0);
}
