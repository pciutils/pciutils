# $Id: Makefile,v 1.10 1999/01/22 21:04:46 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1998--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

OPT=-O2 -fomit-frame-pointer
#OPT=-O2 -g
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Werror

VERSION=1.99.2-alpha
DATE=22 January 1999

ROOT=/
PREFIX=/usr

export

all: lib lspci setpci lspci.8 setpci.8

lib: lib/config.h
	$(MAKE) -C lib all

lib/config.h:
	cd lib && ./configure $(PREFIX) $(VERSION)

lspci: lspci.o common.o lib/libpci.a
setpci: setpci.o common.o lib/libpci.a

lspci.o: lspci.c pciutils.h lib/libpci.a
setpci.o: setpci.c pciutils.h lib/libpci.a
common.o: common.c pciutils.h lib/libpci.a

%.8: %.man
	sed <$< >$@ "s/@TODAY@/$(DATE)/;s/@VERSION@/pciutils-$(VERSION)/"

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core`
	rm -f lspci setpci lib/config.* lib/header.h *.8

install: all
	install -o root -g root -m 755 -s lspci setpci $(ROOT)/sbin
	install -o root -g root -m 644 pci.ids $(PREFIX)/share
	install -o root -g root -m 644 lspci.8 setpci.8 $(PREFIX)/man/man8
	# Remove relics from old versions
	rm -f $(ROOT)/etc/pci.ids

dist: clean
	cp /usr/src/linux/include/linux/pci.h lib/header.h
	sh -c 'X=`pwd` ; X=`basename $$X` ; cd .. ; tar czvvf /tmp/$$X.tar.gz $$X --exclude CVS --exclude tmp'
	rm -f lib/header.h

.PHONY: all lib clean install dist man
