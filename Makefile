# zygo/Makefile
#
# Copyright (c) 2022 hhvn <dev@hhvn.uk>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

PREFIX	= /usr/local
BINDIR	= $(PREFIX)/bin
MANDIR	= $(PREFIX)/man
BIN	= zygo
MAN	= zygo.1
SRC	+= zygo.c
OBJ	= $(SRC:.c=.o)
COMMIT	= $(shell git log HEAD...HEAD~1 --pretty=format:%h)
LDFLAGS = -lncursesw
CFLAGS	= -DCOMMIT=\"$(COMMIT)\"

include config.mk

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(OBJ)
	@echo "---> If you see any warnings or weird things happening, read the FAQ in INSTALL <---"

$(OBJ): Makefile config.mk zygo.h
zygo.o: config.h

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

install:
	mkdir -p $(BINDIR)
	install -m0755 $(BIN) $(BINDIR)/$(BIN)
	mkdir -p $(MANDIR)/man1
	sed "s/COMMIT/$(COMMIT)/" < $(MAN) > $(MANDIR)/man1/$(MAN)

uninstall:
	-rm -rf $(BINDIR)/$(BIN) $(MANDIR)/man1/$(MAN)

clean:
	-rm -f $(OBJ) $(BIN)

config.h: config.def.h
	cp config.def.h config.h

.PHONY: clean install uninstall
