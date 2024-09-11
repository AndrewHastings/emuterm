/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 */

/*
 * Handle input from user to emulated terminal.
 */

#include <unistd.h>
#include "input.h"


/* read input from user, write to slave pty */
int handle_input(int mfd)
{
	int rc;
	char buf[128];

	if ((rc = read(STDIN_FILENO, buf, sizeof buf)) <= 0)
		return rc;
	return write(mfd, buf, rc);
}
