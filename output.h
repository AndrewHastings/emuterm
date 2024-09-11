/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 */

/*
 * Translate output from emulated terminal into ANSI escape sequences.
 */

#ifndef _OUTPUT_H
#define _OUTPUT_H 1

extern void omode(int emulate);
extern int handle_output(int mfd);

#endif /* _OUTPUT_H */
