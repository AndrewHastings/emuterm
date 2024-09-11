#
# Copyright 2024 Andrew B. Hastings. All rights reserved.
#

CFLAGS=-g -fsanitize=address -Wunused-variable

HDRS = emuterm.h input.h output.h
OBJS = emuterm.o input.o output.o
LIBS = -lutil

emuterm: $(OBJS)
	$(CC) $(CFLAGS) -o emuterm $^ $(LIBS)

clean:
	$(RM) $(OBJS)

clobber:
	$(RM) emuterm $(OBJS)

%.o : %.c $(HDRS)
	$(CC) $(CFLAGS) -c $<
