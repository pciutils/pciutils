# Makefile for The PCI Utilities
# (c) 1998--2007 Martin Mares <mj@ucw.cz>

OPT=-O2
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes

VERSION=2.2.8
DATE=2007-10-19

PREFIX=/usr/local
SBINDIR=$(PREFIX)/sbin
SHAREDIR=$(PREFIX)/share
IDSDIR=$(SHAREDIR)
MANDIR:=$(shell if [ -d $(PREFIX)/share/man ] ; then echo $(PREFIX)/share/man ; else echo $(PREFIX)/man ; fi)
INCDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib
PKGCFDIR=$(LIBDIR)/pkgconfig
INSTALL=install
DIRINSTALL=install -d
AR=ar
RANLIB=ranlib
PCILIB=lib/libpci.a
PCILIBPC=lib/libpci.pc
PCIINC=lib/config.h lib/header.h lib/pci.h lib/types.h lib/sysdep.h
PCIINC_INS=lib/config.h lib/header.h lib/pci.h lib/types.h

-include lib/config.mk

HOST=
RELEASE=

export

all: $(PCILIB) lspci setpci lspci.8 setpci.8 update-pciids update-pciids.8 $(PCI_IDS)

$(PCILIB): $(PCIINC) force
	$(MAKE) -C lib all

force:

lib/config.h lib/config.mk:
	cd lib && ./configure "$(IDSDIR)" "$(VERSION)" "$(HOST)" "$(RELEASE)" "$(ZLIB)"

lspci: lspci.o common.o $(PCILIB)
setpci: setpci.o common.o $(PCILIB)

lspci.o: lspci.c pciutils.h $(PCIINC)
setpci.o: setpci.c pciutils.h $(PCIINC)
common.o: common.c pciutils.h $(PCIINC)

update-pciids: update-pciids.sh
	sed <$< >$@ "s@^DEST=.*@DEST=$(IDSDIR)/$(PCI_IDS)@;s@^PCI_COMPRESSED_IDS=.*@PCI_COMPRESSED_IDS=$(PCI_COMPRESSED_IDS)@"
	chmod +x $@

%: %.o
	$(CC) $(LDFLAGS) $(TARGET_ARCH) $^ $(LDLIBS) -o $@

%.8: %.man
	M=`echo $(DATE) | sed 's/-01-/-January-/;s/-02-/-February-/;s/-03-/-March-/;s/-04-/-April-/;s/-05-/-May-/;s/-06-/-June-/;s/-07-/-July-/;s/-08-/-August-/;s/-09-/-September-/;s/-10-/-October-/;s/-11-/-November-/;s/-12-/-December-/;s/\(.*\)-\(.*\)-\(.*\)/\3 \2 \1/'` ; sed <$< >$@ "s/@TODAY@/$$M/;s/@VERSION@/pciutils-$(VERSION)/;s#@IDSDIR@#$(IDSDIR)#"

clean:
	rm -f `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core -o -name "*.orig"`
	rm -f update-pciids lspci setpci lib/config.* lib/example *.8 pci.ids.* lib/*.pc
	rm -rf maint/dist

distclean: clean

install: all
# -c is ignored on Linux, but required on FreeBSD
	$(DIRINSTALL) -m 755 $(DESTDIR)$(SBINDIR) $(DESTDIR)$(IDSDIR) $(DESTDIR)$(MANDIR)/man8
	$(INSTALL) -c -m 755 -s lspci setpci $(DESTDIR)$(SBINDIR)
	$(INSTALL) -c -m 755 update-pciids $(DESTDIR)$(SBINDIR)
	$(INSTALL) -c -m 644 $(PCI_IDS) $(DESTDIR)$(IDSDIR)
	$(INSTALL) -c -m 644 lspci.8 setpci.8 update-pciids.8 $(DESTDIR)$(MANDIR)/man8

install-lib: $(PCIINC_INS) $(PCILIB) $(PCILIBPC)
	$(DIRINSTALL) -m 755 $(DESTDIR)$(INCDIR)/pci $(DESTDIR)$(LIBDIR) $(DESTDIR)$(PKGCFDIR)
	$(INSTALL) -c -m 644 $(PCIINC_INS) $(DESTDIR)$(INCDIR)/pci
	$(INSTALL) -c -m 644 $(PCILIB) $(DESTDIR)$(LIBDIR)
	$(INSTALL) -c -m 644 $(PCILIBPC) $(DESTDIR)$(PKGCFDIR)

uninstall: all
	rm -f $(DESTDIR)$(SBINDIR)/lspci $(DESTDIR)$(SBINDIR)/setpci $(DESTDIR)$(SBINDIR)/update-pciids
	rm -f $(DESTDIR)$(IDSDIR)/$(PCI_IDS)
	rm -f $(DESTDIR)$(MANDIR)/man8/lspci.8 $(DESTDIR)$(MANDIR)/man8/setpci.8 $(DESTDIR)$(MANDIR)/man8/update-pciids.8

pci.ids.gz: pci.ids
	gzip -9 <$< >$@

.PHONY: all clean distclean install uninstall force
