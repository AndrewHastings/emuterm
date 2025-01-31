#
# Copyright 2024, 2025 Andrew B. Hastings. All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

# If your build fails because of a missing "asan" library, try:
#   make clean
#   make CFLAGS=

CFLAGS=-g -fsanitize=address -Werror -Wunused-variable

HDRS = emuterm.h input.h output.h termcap.h
OBJS = emuterm.o input.o output.o termcap.o
LIBS = -lutil

BSD = https://www.tuhs.org/cgi-bin/utree.pl?file=4.4BSD/etc/termcap

all: emuterm termcap tsete

emuterm: $(OBJS)
	$(CC) $(CFLAGS) -o emuterm $^ $(LIBS)

tsete: tsete.o termcap.o
	$(CC) $(CFLAGS) -o tsete $^

termcap: extras.tc termtypes.tc
	wget -O - $(BSD) | sed -e '1,/<pre>/d' -e '/<\/pre>/,$$d' -e 's/&lt;/</g' -e 's/&gt;/>/g' -e 's/&quot;/"/g' -e "s/&#39;/'/g" -e 's/&amp;/\&/g' > bsd.tc
	@if [ -s bsd.tc ]; then \
		echo 'cat $< bsd.tc > $@'; \
		cat $< bsd.tc > $@; \
	else \
		echo 'cat $^ > $@'; \
		cat $^ > $@; \
	fi

clean:
	$(RM) bsd.tc tsete.o $(OBJS)

clobber:
	$(RM) emuterm termcap bsd.tc tsete tsete.o $(OBJS)

%.o : %.c $(HDRS)
	$(CC) $(CFLAGS) -c $<
