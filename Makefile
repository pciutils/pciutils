# $Id: Makefile,v 1.7 1998/03/31 21:02:12 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

ARCH=$(shell uname -m | sed -e 's/i.86/i386/' -e 's/sun4u/sparc64/' | tr 'a-z' 'A-Z')
KERN_H=$(shell if [ ! -f pci.h ] ; then echo '-DKERNEL_PCI_H' ; fi)
OPT=-O2 -fomit-frame-pointer
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wno-unused -Werror -DARCH_$(ARCH) $(KERN_H)

PREFIX=/
MANPREFIX=/usr

all: lspci

#all: lspci setpci

lspci: lspci.o names.o filter.o

lspci.o: lspci.c pciutils.h
names.o: names.c pciutils.h
filter.o: filter.c pciutils.h

setpci: setpci.o filter.o

setpci.o: setpci.c

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core`
	rm -f lspci setpci pci.h

install: all
	install -o root -g root -m 755 -s lspci $(PREFIX)/sbin
	install -o root -g root -m 644 pci.ids $(PREFIX)/etc
	install -o root -g root -m 644 lspci.8 $(MANPREFIX)/man/man8

dist: clean
	cp /usr/src/linux/include/linux/pci.h .
	sh -c 'X=`pwd` ; X=`basename $$X` ; cd .. ; tar czvvf /tmp/$$X.tar.gz $$X --exclude CVS --exclude tmp'
	rm -f pci.h
