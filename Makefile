# Makefile for The PCI Utilities
# (c) 1998--2005 Martin Mares <mj@ucw.cz>

OPT=-O2
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes -Winline

VERSION=2.2.1
DATE=2005-11-26

PREFIX=/usr/local
SBINDIR=$(PREFIX)/sbin
SHAREDIR=$(PREFIX)/share
IDSDIR=$(SHAREDIR)
MANDIR:=$(shell if [ -d $(PREFIX)/share/man ] ; then echo $(PREFIX)/share/man ; else echo $(PREFIX)/man ; fi)
INSTALL=install
DIRINSTALL=install -d
PCILIB=lib/libpci.a
PCIINC=lib/config.h lib/header.h lib/pci.h lib/types.h lib/sysdep.h

ifeq ($(shell uname),NetBSD)
PCILIB=lib/libpciutils.a
LDFLAGS+=-lpci
else
ifeq ($(shell uname),AIX)
CFLAGS=-g
INSTALL=installbsd
DIRINSTALL=mkdir -p
endif
endif

HOST=
RELEASE=

export

all: $(PCILIB) lspci setpci lspci.8 setpci.8 update-pciids update-pciids.8 pci.ids

$(PCILIB): $(PCIINC) force
	$(MAKE) -C lib all

force:

lib/config.h:
	cd lib && ./configure $(IDSDIR) $(VERSION) $(HOST) $(RELEASE)

lspci: lspci.o common.o $(PCILIB)
setpci: setpci.o common.o $(PCILIB)

lspci.o: lspci.c pciutils.h $(PCIINC)
setpci.o: setpci.c pciutils.h $(PCIINC)
common.o: common.c pciutils.h $(PCIINC)

update-pciids: update-pciids.sh
	sed <$< >$@ "s@^DEST=.*@DEST=$(IDSDIR)/pci.ids@"

%.8: %.man
	M=`echo $(DATE) | sed 's/-01-/-January-/;s/-02-/-February-/;s/-03-/-March-/;s/-04-/-April-/;s/-05-/-May-/;s/-06-/-June-/;s/-07-/-July-/;s/-08-/-August-/;s/-09-/-September-/;s/-10-/-October-/;s/-11-/-November-/;s/-12-/-December-/;s/\(.*\)-\(.*\)-\(.*\)/\3 \2 \1/'` ; sed <$< >$@ "s/@TODAY@/$$M/;s/@VERSION@/pciutils-$(VERSION)/;s#@IDSDIR@#$(IDSDIR)#"

clean:
	rm -f `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core`
	rm -f update-pciids lspci setpci lib/config.* *.8 pci.ids.*
	rm -rf maint/dist

distclean: clean

install: all
# -c is ignored on Linux, but required on FreeBSD
	$(DIRINSTALL) -m 755 $(SBINDIR) $(IDSDIR) $(MANDIR)/man8
	$(INSTALL) -c -m 755 -s lspci setpci $(SBINDIR)
	$(INSTALL) -c -m 755 update-pciids $(SBINDIR)
	$(INSTALL) -c -m 644 pci.ids $(IDSDIR)
	$(INSTALL) -c -m 644 lspci.8 setpci.8 update-pciids.8 $(MANDIR)/man8

uninstall: all
	rm -f $(SBINDIR)/lspci $(SBINDIR)/setpci $(SBINDIR)/update-pciids
	rm -f $(IDSDIR)/pci.ids
	rm -f $(MANDIR)/man8/lspci.8 $(MANDIR)/man8/setpci.8 $(MANDIR)/man8/update-pciids.8

get-ids:
	cp ~/tree/pciids/pci.ids pci.ids

.PHONY: all clean distclean install uninstall get-ids force
