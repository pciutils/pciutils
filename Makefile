# $Id: Makefile,v 1.53 2003/01/04 12:49:06 mj Exp $
# Makefile for Linux PCI Utilities
# (c) 1998--2003 Martin Mares <mj@ucw.cz>

OPT=-O2 -fomit-frame-pointer
#OPT=-O2 -g
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes

VERSION=2.1.11
#SUFFIX=-pre2
DATE=2003-01-04

PREFIX=/usr/local
SBINDIR=$(PREFIX)/sbin
SHAREDIR=$(PREFIX)/share
MANDIR=$(shell if [ -d $(PREFIX)/share/man ] ; then echo $(PREFIX)/share/man ; else echo $(PREFIX)/man ; fi)
INSTALL=install
DIRINSTALL=install -d
PCILIB=libpci.a

ifeq ($(shell uname),NetBSD)
PCILIB=libpciutils.a
LDFLAGS+=-lpci
else
ifeq ($(shell uname),AIX)
CFLAGS=-g
INSTALL=installbsd
DIRINSTALL=mkdir -p
endif
endif

export

all: lib lspci setpci lspci.8 setpci.8 update-pciids update-pciids.8 pci.ids

lib: lib/config.h
	$(MAKE) -C lib all

lib/config.h:
	cd lib && ./configure $(SHAREDIR) $(VERSION)

lspci: lspci.o common.o lib/$(PCILIB)
setpci: setpci.o common.o lib/$(PCILIB)

lspci.o: lspci.c pciutils.h
setpci.o: setpci.c pciutils.h
common.o: common.c pciutils.h

update-pciids: update-pciids.sh
	sed <$< >$@ "s@^DEST=.*@DEST=$(SHAREDIR)/pci.ids@"

%.8: %.man
	M=`echo $(DATE) | sed 's/-01-/-January-/;s/-02-/-February-/;s/-03-/-March-/;s/-04-/-April-/;s/-05-/-May-/;s/-06-/-June-/;s/-07-/-July-/;s/-08-/-August-/;s/-09-/-September-/;s/-10-/-October-/;s/-11-/-November-/;s/-12-/-December-/;s/\(.*\)-\(.*\)-\(.*\)/\3 \2 \1/'` ; sed <$< >$@ "s/@TODAY@/$$M/;s/@VERSION@/pciutils-$(VERSION)$(SUFFIX)/;s#@SHAREDIR@#$(SHAREDIR)#"

clean:
	rm -f `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core`
	rm -f update-ids lspci setpci lib/config.* *.8 pci.ids.*

install: all
# -c is ignored on Linux, but required on FreeBSD
	$(DIRINSTALL) -m 755 $(SBINDIR) $(SHAREDIR) $(MANDIR)/man8
	$(INSTALL) -c -m 755 -s lspci setpci $(SBINDIR)
	$(INSTALL) -c -m 755 update-pciids $(SBINDIR)
	$(INSTALL) -c -m 644 pci.ids $(SHAREDIR)
	$(INSTALL) -c -m 644 lspci.8 setpci.8 update-pciids.8 $(MANDIR)/man8

uninstall: all
	rm -f $(SBINDIR)/lspci $(SBINDIR)/setpci $(SBINDIR)/update-pciids
	rm -f $(SHAREDIR)/pci.ids
	rm -f $(MANDIR)/man8/lspci.8 $(MANDIR)/man8/setpci.8 $(MANDIR)/man8/update-pciids.8

get-ids:
	cp ~/tree/pciids/pci.ids pci.ids

pci.ids:
	@ [ -f pci.ids ] || echo >&2 "The pci.ids file is no longer part of the CVS. Please do run update-ids.sh to download them." && false

release:
	sed "s/^\\(Version:[ 	]*\\)[0-9.]*/\\1$(VERSION)/;s/^\\(Entered-date:[ 	]*\\)[0-9]*/\\1`date -d$(DATE) '+%y%m%d'`/;s/\\(pciutils-\\)[0-9.]*/\\1$(VERSION)\\./" <pciutils.lsm >pciutils.lsm.new
	sed "s/^\\(Version:[ 	]*\\)[0-9.]*/\\1$(VERSION)/" <pciutils.spec >pciutils.spec.new
	sed "s/\\(, version \\).*\./\\1$(VERSION)$(SUFFIX)./" <README >README.new
	mv pciutils.lsm.new pciutils.lsm
	mv pciutils.spec.new pciutils.spec
	mv README.new README

REL=pciutils-$(VERSION)$(SUFFIX)
DISTTMP=/tmp/pciutils-dist

dist: clean pci.ids
	rm -rf $(DISTTMP)
	mkdir $(DISTTMP)
	cp -a . $(DISTTMP)/$(REL)
	rm -rf `find $(DISTTMP)/$(REL) -name CVS -o -name tmp -o -name maint`
	cd $(DISTTMP) ; tar czvvf /tmp/$(REL).tar.gz $(REL)
	rm -rf $(DISTTMP)

upload: dist
	maint/upload $(REL)

.PHONY: all lib clean install uninstall dist man release upload get-ids
