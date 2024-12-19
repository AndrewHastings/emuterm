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
 * Use the termcap database to set TERM and TERMCAP for a given terminal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "termcap.h"


#define TBUFSIZ 2048


/* is this capability empty, a duplicate, or a cancellation? */
int should_skip(char *s)
{
	static char seen[TBUFSIZ];
	static int last = 0;
	int i;

	/* if empty or starts with whitespace, skip */
	switch (s[0]) {
	    case '\0': case ':': case ' ': case '\t': case '\r': case '\n':
		return 1;
	}

	/* if only one character(!?), don't skip */
	if (!s[1] || s[1] == ':')
		return 0;

	/* if previously seen, skip */
	for (i = 0; i < last; i += 2) {
		if (s[0] == seen[i] && s[1] == seen[i+1])
			return 1;
	}

	/* add to the list */
	seen[last++] = s[0];
	seen[last++] = s[1];

	/* skip iff a cancellation */
	return s[2] == '@';
}


char *setterm(char *term)
{
	char tbuf[TBUFSIZ];	/* returned termcap entry */
	char *s, *t;
	unsigned char c, d;
	int rv, is_csh;

	rv = tgetent(tbuf, term);
	if (rv < 0)
		return "No termcap file found, try setting TERMPATH";
	if (rv == 0)
		return "Terminal type not found in termcap database";

	is_csh = (s = getenv("SHELL")) && (rv = strlen(s)) >= 3
				       && strcmp(&s[rv-3], "csh") == 0;
	if (is_csh)
		printf("set noglob;\nsetenv TERM '%s';\nsetenv TERMCAP '",
		       term);
	else
		printf("export TERM='%s';\nexport TERMCAP='", term);

	/* first field: terminal names */
	for (s = tbuf; (c = *s) && c != ':'; s++) {
		if (c == '|') {		/* start of next name */
			int has_ws = 0;

			/* find the end */
			for (t = s+1; (d = *t) && d != ':' && d != '|'; t++) {
				if (d == ' ' || d == '\t')
					has_ws = 1;
			}

			if (has_ws) {	/* skip if it contains whitespace */
				s = t-1;
				continue;
			}

			/* else, fall through and print this name */
		}
		putchar(c);
	}

	/* process capabilities */
	while (c = *s++) {
		switch (c) {
		    case ' ': case '"': case '\'': case '!': case '`':
			/* print as octal escape to prevent shell mangling */
			printf("\\%03o", c);
			break;

		    case '\\': case '^':
			/* already an escape, print as is */
			putchar(c);
			if (c = *s++)
				putchar(c);
			else
				s--;	    /* oops, end of string */
			break;

		    case ':':		    /* start of capability */
			if (should_skip(s)) {
				while ((c = *s++) && c != ':')
					;
				s--;	    /* undo lookahead */
				break;
			}

			/* else, print it */
			putchar(c);
			break;

		    default:
			putchar(c);
			break;
		}
	}

	if (is_csh)
		printf(":';\nunset noglob\n");
	else
		printf(":'\n");
	return NULL;
}


char *prog;


void usage(int ec)
{
	fprintf(stderr, "Usage: %s [-Qs] <termtype>\n", prog);
	fprintf(stderr, "Options always set even if not specified:\n");
	fprintf(stderr, " -Q  don't display erase, interrupt, kill characters\n");
	fprintf(stderr, " -s  print shell commands to set TERM and TERMCAP env. vars.\n");
	fprintf(stderr, "BSD tset(1) replacement designed to be run as \"eval `%s <term>`\"\n", prog);
	exit(ec);
}


void main(int argc, char **argv)
{
	int c;
	char *err;

	prog = strrchr(argv[0], '/');
	prog = prog ? prog+1 : argv[0];

	while ((c = getopt(argc, argv, "+:hQs")) != -1) {
		switch (c) {
		    case 'h':
			usage(0);
			break;

		    case 'Q':
		    case 's':
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

	if (argc - optind != 1)
		usage(1);
	if (err = setterm(argv[optind])) {
		fprintf(stderr, "%s\n", err);
		exit(1);
	}

	exit(0);
}
