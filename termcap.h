/* Declarations for termcap library.
   Copyright (C) 1991, 1992, 1995 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*
 * Adapted for emuterm by removing unneeded declarations.
 * Copyright 2024 Andrew B. Hastings.
 */

#ifndef _TERMCAP_H
#define _TERMCAP_H 1

extern int tgetent (char *buffer, const char *termtype);

extern int tgetnum (const char *name);
extern int tgetflag (const char *name);
extern char *tgetstr (const char *name, char **area);

#endif /* not _TERMCAP_H */
