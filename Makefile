# $Id: Makefile,v 1.5 1998/02/08 10:58:52 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

ARCH=$(shell uname -m | sed -e 's/i.86/i386/' -e 's/sun4u/sparc64/' | tr 'a-z' 'A-Z')
OPT=-O2 -fomit-frame-pointer
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wno-unused -Werror -DARCH_$(ARCH)

PREFIX=/

all: lspci

lspci: lspci.o names.o

lspci.o: lspci.c pciutils.h
names.o: names.c pciutils.h

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core`
	rm -f lspci

install: all
	install -o root -g root -m 755 -s lspci $(PREFIX)/sbin
	install -o root -g root -m 644 pci.ids $(PREFIX)/etc
	install -o root -g root -m 644 lspci.8 $(PREFIX)/man/man8

dist: clean
	sh -c 'X=`pwd` ; X=`basename $$X` ; cd .. ; tar czvvf /tmp/$$X.tar.gz $$X --exclude CVS --exclude tmp'
