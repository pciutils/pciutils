# $Id: Makefile,v 1.4 1998/01/27 11:50:07 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

OPT=-O2 -fomit-frame-pointer
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wno-unused -Werror

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

dist: clean
	sh -c 'X=`pwd` ; X=`basename $$X` ; cd .. ; tar czvvf /tmp/$$X.tar.gz $$X --exclude CVS --exclude tmp'
