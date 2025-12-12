##
# Basex Fuse System
#
# @file
# @version 0.1
#

LDFLAGS=-lssl -l:basexdbc.a -lcrypto -lfuse -L basex-api/ -Wl,-rpath=/usr/lib/basex

all: basexfuse

basexfuse: src/basexfs.c basexdbc.a
	cd src && cc basexfs.c -fsanitize=undefined -D_FILE_OFFSET_BITS=64 -g -o ../basexfuse $(LDFLAGS)

basexdbc.o md5.o readstring.o: src/basex-api/basexdbc.c
	cd src/basex-api && $(MAKE)

basexdbc.a: basexdbc.o md5.o readstring.o
	cd src/basex-api && ar rcs $@ $^


install: basexfuse
clean:
	rm -f basexfuse
	cd src/basex-api && rm -f basexdbc.o md5.o readstring.o basexdbc.a
# end
