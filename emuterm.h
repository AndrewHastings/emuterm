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
 * Emulate an old terminal by handling its output control sequences.
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
