# Makefile for The PCI Utilities
# (c) 1998--2022 Martin Mares <mj@ucw.cz>

OPT=-O2
CFLAGS=$(OPT) -Wall -W -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes

VERSION=3.8.0
DATE=2022-04-18

# Host OS and release (override if you are cross-compiling)
HOST=
RELEASE=
CROSS_COMPILE=

# Support for compressed pci.ids (yes/no, default: detect)
ZLIB=

# Support for resolving ID's by DNS (yes/no, default: detect)
DNS=

# Build libpci as a shared library (yes/no; or local for testing; requires GCC)
SHARED=no

# Use libkmod to resolve kernel modules on Linux (yes/no, default: detect)
LIBKMOD=

# Use libudev to resolve device names using hwdb on Linux (yes/no, default: detect)
HWDB=

# ABI version suffix in the name of the shared library
# (as we use proper symbol versioning, this seldom needs changing)
ABI_VERSION=.3

# Installation directories
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
SBINDIR=$(PREFIX)/sbin
SHAREDIR=$(PREFIX)/share
IDSDIR=$(SHAREDIR)
MANDIR:=$(shell if [ -d $(PREFIX)/share/man ] ; then echo $(PREFIX)/share/man ; else echo $(PREFIX)/man ; fi)
INCDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib
PKGCFDIR=$(LIBDIR)/pkgconfig

# Commands
INSTALL=install
DIRINSTALL=install -d
STRIP=-s
ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
else
CC=cc
endif
AR=$(CROSS_COMPILE)ar
RANLIB=$(CROSS_COMPILE)ranlib

# Base name of the library (overridden on NetBSD, which has its own libpci)
LIBNAME=libpci

-include lib/config.mk

PCIINC=lib/config.h lib/header.h lib/pci.h lib/types.h lib/sysdep.h
PCIINC_INS=lib/config.h lib/header.h lib/pci.h lib/types.h

export

all: lib/$(PCILIB) lspci$(EXEEXT) setpci$(EXEEXT) example$(EXEEXT) lspci.8 setpci.8 pcilib.7 pci.ids.5 update-pciids update-pciids.8 $(PCI_IDS)

lib/$(PCILIB): $(PCIINC) force
	$(MAKE) -C lib all

force:

lib/config.h lib/config.mk:
	cd lib && ./configure

COMMON=common.o
ifeq ($(COMPAT_GETOPT),yes)
PCIINC+=compat/getopt.h
COMMON+=compat/getopt.o
endif

lspci$(EXEEXT): lspci.o ls-vpd.o ls-caps.o ls-caps-vendor.o ls-ecaps.o ls-kernel.o ls-tree.o ls-map.o $(COMMON) lib/$(PCILIB)
setpci$(EXEEXT): setpci.o $(COMMON) lib/$(PCILIB)

LSPCIINC=lspci.h pciutils.h $(PCIINC)
lspci.o: lspci.c $(LSPCIINC)
ls-vpd.o: ls-vpd.c $(LSPCIINC)
ls-caps.o: ls-caps.c $(LSPCIINC)
ls-ecaps.o: ls-ecaps.c $(LSPCIINC)
ls-kernel.o: ls-kernel.c $(LSPCIINC)
ls-tree.o: ls-tree.c $(LSPCIINC)
ls-map.o: ls-map.c $(LSPCIINC)

setpci.o: setpci.c pciutils.h $(PCIINC)
common.o: common.c pciutils.h $(PCIINC)
compat/getopt.o: compat/getopt.c

lspci$(EXEEXT): LDLIBS+=$(LIBKMOD_LIBS)
ls-kernel.o: CFLAGS+=$(LIBKMOD_CFLAGS)

update-pciids: update-pciids.sh
	sed <$< >$@ "s@^DEST=.*@DEST=$(if $(IDSDIR),$(IDSDIR)/,)$(PCI_IDS)@;s@^PCI_COMPRESSED_IDS=.*@PCI_COMPRESSED_IDS=$(PCI_COMPRESSED_IDS)@"
	chmod +x $@

# The example of use of libpci
example$(EXEEXT): example.o lib/$(PCILIB)
example.o: example.c $(PCIINC)

%$(EXEEXT): %.o
	$(CC) $(LDFLAGS) $(TARGET_ARCH) $^ $(LDLIBS) -o $@

%.8 %.7 %.5: %.man
	M=`echo $(DATE) | sed 's/-01-/-January-/;s/-02-/-February-/;s/-03-/-March-/;s/-04-/-April-/;s/-05-/-May-/;s/-06-/-June-/;s/-07-/-July-/;s/-08-/-August-/;s/-09-/-September-/;s/-10-/-October-/;s/-11-/-November-/;s/-12-/-December-/;s/\(.*\)-\(.*\)-\(.*\)/\3 \2 \1/'` ; sed <$< >$@ "s/@TODAY@/$$M/;s/@VERSION@/pciutils-$(VERSION)/;s#@IDSDIR@#$(IDSDIR)#;s#@PCI_IDS@#$(PCI_IDS)#"

ctags:
	rm -f tags
	find . -name '*.[hc]' -exec ctags --append {} +

TAGS:
	rm -f TAGS
	find . -name '*.[hc]' -exec etags --append {} +

clean:
	rm -f `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core -o -name "*.orig"`
	rm -f update-pciids lspci$(EXEEXT) setpci$(EXEEXT) example$(EXEEXT) lib/config.* *.[578] pci.ids.gz lib/*.pc lib/*.so lib/*.so.* tags
	rm -rf maint/dist

distclean: clean

install: all
# -c is ignored on Linux, but required on FreeBSD
	$(DIRINSTALL) -m 755 $(DESTDIR)$(BINDIR) $(DESTDIR)$(SBINDIR) $(DESTDIR)$(IDSDIR) $(DESTDIR)$(MANDIR)/man8 $(DESTDIR)$(MANDIR)/man7 $(DESTDIR)$(MANDIR)/man5
	$(INSTALL) -c -m 755 $(STRIP) lspci$(EXEEXT) $(DESTDIR)$(LSPCIDIR)
	$(INSTALL) -c -m 755 $(STRIP) setpci$(EXEEXT) $(DESTDIR)$(SBINDIR)
	$(INSTALL) -c -m 755 update-pciids $(DESTDIR)$(SBINDIR)
ifneq ($(IDSDIR),)
	$(INSTALL) -c -m 644 $(PCI_IDS) $(DESTDIR)$(IDSDIR)
else
	$(INSTALL) -c -m 644 $(PCI_IDS) $(DESTDIR)$(SBINDIR)
endif
	$(INSTALL) -c -m 644 lspci.8 setpci.8 update-pciids.8 $(DESTDIR)$(MANDIR)/man8
	$(INSTALL) -c -m 644 pcilib.7 $(DESTDIR)$(MANDIR)/man7
	$(INSTALL) -c -m 644 pci.ids.5 $(DESTDIR)$(MANDIR)/man5
ifeq ($(SHARED),yes)
ifeq ($(LIBEXT),dylib)
	ln -sf $(PCILIB) $(DESTDIR)$(LIBDIR)/$(LIBNAME)$(ABI_VERSION).$(LIBEXT)
else
	ln -sf $(PCILIB) $(DESTDIR)$(LIBDIR)/$(LIBNAME).$(LIBEXT)$(ABI_VERSION)
endif
endif

ifeq ($(SHARED),yes)
install: install-pcilib
endif

install-pcilib: lib/$(PCILIB)
	$(DIRINSTALL) -m 755 $(DESTDIR)$(LIBDIR)
	$(INSTALL) -c -m 644 lib/$(PCILIB) $(DESTDIR)$(LIBDIR)

install-lib: $(PCIINC_INS) install-pcilib
	$(DIRINSTALL) -m 755 $(DESTDIR)$(INCDIR)/pci $(DESTDIR)$(PKGCFDIR)
	$(INSTALL) -c -m 644 $(PCIINC_INS) $(DESTDIR)$(INCDIR)/pci
	$(INSTALL) -c -m 644 lib/$(PCILIBPC) $(DESTDIR)$(PKGCFDIR)
ifeq ($(SHARED),yes)
ifeq ($(LIBEXT),dylib)
	ln -sf $(PCILIB) $(DESTDIR)$(LIBDIR)/$(LIBNAME)$(ABI_VERSION).$(LIBEXT)
	ln -sf $(LIBNAME)$(ABI_VERSION).$(LIBEXT) $(DESTDIR)$(LIBDIR)/$(LIBNAME).$(LIBEXT)
else
	ln -sf $(PCILIB) $(DESTDIR)$(LIBDIR)/$(LIBNAME).$(LIBEXT)$(ABI_VERSION)
	ln -sf $(LIBNAME).$(LIBEXT)$(ABI_VERSION) $(DESTDIR)$(LIBDIR)/$(LIBNAME).$(LIBEXT)
endif
endif

uninstall: all
	rm -f $(DESTDIR)$(SBINDIR)/lspci$(EXEEXT) $(DESTDIR)$(SBINDIR)/setpci$(EXEEXT) $(DESTDIR)$(SBINDIR)/update-pciids
ifneq ($(IDSDIR),)
	rm -f $(DESTDIR)$(IDSDIR)/$(PCI_IDS)
else
	rm -f $(DESTDIR)$(SBINDIR)/$(PCI_IDS)
endif
	rm -f $(DESTDIR)$(MANDIR)/man8/lspci.8 $(DESTDIR)$(MANDIR)/man8/setpci.8 $(DESTDIR)$(MANDIR)/man8/update-pciids.8
	rm -f $(DESTDIR)$(MANDIR)/man7/pcilib.7
	rm -f $(DESTDIR)$(MANDIR)/man5/pci.ids.5
	rm -f $(DESTDIR)$(LIBDIR)/$(PCILIB)
	rm -f $(DESTDIR)$(PKGCFDIR)/$(PCILIBPC)
	rm -f $(addprefix $(DESTDIR)$(INCDIR)/pci/,$(notdir $(PCIINC_INS)))
ifeq ($(SHARED),yes)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIBNAME).$(LIBEXT)
ifeq ($(LIBEXT),dylib)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIBNAME)$(ABI_VERSION).$(LIBEXT)
else
	rm -f $(DESTDIR)$(LIBDIR)/$(LIBNAME).$(LIBEXT)$(ABI_VERSION)
endif
endif

pci.ids.gz: pci.ids
	gzip -9n <$< >$@

.PHONY: all clean distclean install install-lib uninstall force tags TAGS
