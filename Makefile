# $Id: Makefile,v 1.18 1999/07/07 11:23:03 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1998--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

OPT=-O2 -fomit-frame-pointer
#OPT=-O2 -g
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Werror

ROOT=/
PREFIX=/usr

VERSION=2.1-pre4
SUFFIX=
#SUFFIX=-alpha
DATE=99-05-19

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
	sed <$< >$@ "s/@TODAY@/`date -d $(DATE) '+%d %B %Y'`/;s/@VERSION@/pciutils-$(VERSION)$(SUFFIX)/"

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core`
	rm -f lspci setpci lib/config.* *.8
	rm -rf dist

install: all
	install -m 755 -s lspci setpci $(ROOT)/sbin
	install -m 644 pci.ids $(PREFIX)/share
	install -m 644 lspci.8 setpci.8 $(PREFIX)/man/man8
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

REL=pciutils-$(VERSION)

dist: clean
	mkdir dist
	cp -a . dist/$(REL)
	rm -rf `find dist/$(REL) -name CVS -o -name tmp` dist/$(REL)/dist
	[ -f dist/$(REL)/lib/header.h ] || cp /usr/src/linux/include/linux/pci.h dist/$(REL)/lib/header.h
	cd dist ; tar czvvf /tmp/$(REL).tar.gz $(REL)

.PHONY: all lib clean install dist man release
