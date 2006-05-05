# $Id: Makefile,v 1.1 1997/12/23 10:29:18 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

OPT=-O2 -m486 -malign-loops=0 -malign-jumps=0 -malign-functions=2 -fno-strength-reduce
#-fomit-frame-pointer
#LOPT=-s
#DEBUG=-ggdb
#LDEBUG=-lefence
CFLAGS=$(OPT) $(DEBUG) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wno-unused -Werror
LDFLAGS=$(LOPT) $(LDEBUG)

all: lspci

lspci: lspci.o names.o

lspci.o: lspci.c pciutils.h
names.o: names.c pciutils.h

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core`
	rm -f lspci
