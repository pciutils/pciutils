# $Id: Makefile,v 1.9 1998/11/22 08:56:00 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

ARCH=$(shell uname -m | sed -e 's/i.86/i386/' -e 's/sun4u/sparc64/' | tr 'a-z' 'A-Z')
KERN_H=$(shell if [ ! -f pci.h ] ; then echo '-DKERNEL_PCI_H' ; fi)
OPT=-O2 -fomit-frame-pointer
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wno-unused -Werror -DARCH_$(ARCH) $(KERN_H)

ROOT=/
PREFIX=/usr

all: lspci setpci

lspci: lspci.o names.o filter.o
setpci: setpci.o filter.o

lspci.o: lspci.c pciutils.h
names.o: names.c pciutils.h
filter.o: filter.c pciutils.h
setpci.o: setpci.c pciutils.h

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core`
	rm -f lspci setpci pci.h

install: all
	install -o root -g root -m 755 -s lspci setpci $(ROOT)/sbin
	install -o root -g root -m 644 pci.ids $(PREFIX)/share
	install -o root -g root -m 644 lspci.8 setpci.8 $(PREFIX)/man/man8
	# Remove relics from old versions
	rm -f $(ROOT)/etc/pci.ids

dist: clean
	cp /usr/src/linux/include/linux/pci.h .
	sh -c 'X=`pwd` ; X=`basename $$X` ; cd .. ; tar czvvf /tmp/$$X.tar.gz $$X --exclude CVS --exclude tmp'
	rm -f pci.h
