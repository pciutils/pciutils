# Makefile for The PCI Library
# (c) 1999--2008 Martin Mares <mj@ucw.cz>

include config.mk

OBJS=init.o access.o generic.o dump.o names.o filter.o names-hash.o names-parse.o names-net.o names-cache.o
INCL=internal.h pci.h config.h header.h sysdep.h types.h

PCILIB=libpci.a
PCILIBPC=libpci.pc

ifdef PCI_HAVE_PM_LINUX_SYSFS
OBJS += sysfs.o
endif

ifdef PCI_HAVE_PM_LINUX_PROC
OBJS += proc.o
endif

ifdef PCI_HAVE_PM_INTEL_CONF
OBJS += i386-ports.o
endif

ifdef PCI_HAVE_PM_DUMP
OBJS += dump.o
endif

ifdef PCI_HAVE_PM_FBSD_DEVICE
OBJS += fbsd-device.o
CFLAGS += -I/usr/src/sys
ifdef FREEBSD_SYS
CFLAGS += -I${FREEBSD_SYS}
endif
endif

ifdef PCI_HAVE_PM_OBSD_DEVICE
OBJS += obsd-device.o
endif

ifdef PCI_HAVE_PM_AIX_DEVICE
OBJS += aix-device.o
endif

ifdef PCI_HAVE_PM_NBSD_LIBPCI
OBJS += nbsd-libpci.o
PCILIB=libpciutils.a
endif

all: $(PCILIB) $(PCILIBPC)

$(PCILIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(PCILIBPC): $(PCILIBPC).in
	sed <$< >$@ -e 's,@PREFIX@,$(PREFIX),' \
		-e 's,@INCDIR@,$(INCDIR),' \
		-e 's,@LIBDIR@,$(LIBDIR),' \
		-e 's,@IDSDIR@,$(IDSDIR),' \
		-e 's,@VERSION@,$(VERSION),' \
		-e 's,@LIBZ@,$(LIBZ),'

init.o: init.c $(INCL)
access.o: access.c $(INCL)
i386-ports.o: i386-ports.c $(INCL) i386-io-hurd.h i386-io-linux.h i386-io-sunos.h i386-io-windows.h
proc.o: proc.c $(INCL) pread.h
sysfs.o: sysfs.c $(INCL) pread.h
generic.o: generic.c $(INCL)
syscalls.o: syscalls.c $(INCL)
obsd-device.o: obsd-device.c $(INCL)
fbsd-device.o: fbsd-device.c $(INCL)
aix-device.o: aix-device.c $(INCL)
dump.o: dump.c $(INCL)
names.o: names.c $(INCL) names.h
names-cache.o: names-cache.c $(INCL) names.h
names-hash.o: names-hash.c $(INCL) names.h
names-net.o: names-net.c $(INCL) names.h
names-parse.o: names-parse.c $(INCL) names.h
filter.o: filter.c $(INCL)
nbsd-libpci.o: nbsd-libpci.c $(INCL)

example: example.c $(PCILIB)
