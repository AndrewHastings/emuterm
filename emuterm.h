/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 */

/*
 * Emulate an old terminal by handling its output escape sequences.
 */

#ifndef _EMUTERM_H
#define _EMUTERM_H 1

extern char *prog;
extern int resize_win;
extern int term_cols, term_lines;
extern struct timespec odelay;
extern char *term_type;
extern void send_file(char *path);

#endif /* _EMUTERM_H */
