# $Id: Makefile,v 1.34 2000/05/20 14:42:35 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1998--2000 Martin Mares <mj@suse.cz>

OPT=-O2 -fomit-frame-pointer
#OPT=-O2 -g
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Werror

VERSION=2.1.8
#SUFFIX=-pre2
#SUFFIX=-alpha
DATE=2000-05-20

INSTALL=install
DIRINSTALL=install -d
ifeq ($(shell uname),FreeBSD)
ROOT=/usr/local
PREFIX=/usr/local
else
ifeq ($(shell uname),AIX)
ROOT=/usr/local
PREFIX=/usr/local
CFLAGS=-g
INSTALL=installbsd
DIRINSTALL=mkdir -p
else
ROOT=/
PREFIX=/usr
endif
endif
MANDIR=$(shell if [ -d $(PREFIX)/share/man ] ; then echo $(PREFIX)/share/man ; else echo $(PREFIX)/man ; fi)

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
	M=`echo $(DATE) | sed 's/-01-/-January-/;s/-02-/-February-/;s/-03-/-March-/;s/-04-/-April-/;s/-05-/-May-/;s/-06-/-June-/;s/-07-/-July-/;s/-08-/-August-/;s/-09-/-September-/;s/-10-/-October-/;s/-11-/-November-/;s/-12-/-December-/;s/\(.*\)-\(.*\)-\(.*\)/\3 \2 \1/'` ; sed <$< >$@ "s/@TODAY@/$$M/;s/@VERSION@/pciutils-$(VERSION)$(SUFFIX)/"

clean:
	rm -f `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core`
	rm -f lspci setpci lib/config.* *.8
	rm -rf dist

install: all
	# -c is ignored on Linux, but required on FreeBSD
	$(DIRINSTALL) -m 755 $(ROOT)/sbin $(PREFIX)/share $(MANDIR)/man8
	$(INSTALL) -c -m 755 -s lspci setpci $(ROOT)/sbin
	$(INSTALL) -c -m 644 pci.ids $(PREFIX)/share
	$(INSTALL) -c -m 644 lspci.8 setpci.8 $(MANDIR)/man8
	# Remove relics from old versions
	rm -f $(ROOT)/etc/pci.ids

uninstall: all
	rm -f $(ROOT)/sbin/lspci $(ROOT)/sbin/setpci
	rm -f $(PREFIX)/pci.ids
	rm -f $(PREFIX)/man/man8/lspci.8 $(PREFIX)/man/man8/setpci.8

release:
	sed "s/^\\(Version:[ 	]*\\)[0-9.]*/\\1$(VERSION)/;s/^\\(Entered-date:[ 	]*\\)[0-9]*/\\1`date -d$(DATE) '+%y%m%d'`/;s/\\(pciutils-\\)[0-9.]*/\\1$(VERSION)\\./" <pciutils.lsm >pciutils.lsm.new
	sed "s/^\\(Version:[ 	]*\\)[0-9.]*/\\1$(VERSION)/" <pciutils.spec >pciutils.spec.new
	sed "s/\\(, version \\).*\./\\1$(VERSION)$(SUFFIX)./" <README >README.new
	mv pciutils.lsm.new pciutils.lsm
	mv pciutils.spec.new pciutils.spec
	mv README.new README

REL=pciutils-$(VERSION)$(SUFFIX)

dist: clean
	mkdir dist
	cp -a . dist/$(REL)
	rm -rf `find dist/$(REL) -name CVS -o -name tmp` dist/$(REL)/dist
	[ -f dist/$(REL)/lib/header.h ] || cp /usr/src/linux/include/linux/pci.h dist/$(REL)/lib/header.h
	cd dist ; tar czvvf /tmp/$(REL).tar.gz $(REL)

.PHONY: all lib clean install dist man release
