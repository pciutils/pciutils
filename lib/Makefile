# Makefile for The PCI Library
# (c) 1999 Martin Mares <mj@ucw.cz>

include config.mk

OBJS=access.o generic.o dump.o names.o filter.o
INCL=internal.h pci.h config.h

PCILIB=libpci.a

ifdef HAVE_PM_LINUX_SYSFS
OBJS += sysfs.o
endif

ifdef HAVE_PM_LINUX_PROC
OBJS += proc.o
endif

ifdef HAVE_PM_INTEL_CONF
OBJS += i386-ports.o
endif

ifdef HAVE_PM_DUMP
OBJS += dump.o
endif

ifdef HAVE_PM_SYSCALLS
OBJS += syscalls.o
endif

ifdef HAVE_PM_FBSD_DEVICE
OBJS += fbsd-device.o
CFLAGS += -I/usr/src/sys
ifdef FREEBSD_SYS
CFLAGS += -I${FREEBSD_SYS}
endif
endif

ifdef HAVE_PM_AIX_DEVICE
OBJS += aix-device.o
endif

ifdef HAVE_PM_NBSD_LIBPCI
OBJS += nbsd-libpci.o
PCILIB=libpciutils.a
endif

ifdef HAVE_OWN_HEADER_H
INCL += header.h
endif

all: $(PCILIB)

$(PCILIB): $(OBJS)
	rm -f $@
	ar rcs $@ $^
	ranlib $@

access.o: access.c $(INCL)
i386-ports.o: i386-ports.c $(INCL) i386-io-hurd.h i386-io-linux.h i386-io-sunos.h
proc.o: proc.c $(INCL) pread.h
sysfs.o: sysfs.c $(INCL) pread.h
generic.o: generic.c $(INCL)
syscalls.o: syscalls.c $(INCL)
fbsd-device.o: fbsd-device.c $(INCL)
aix-device.o: aix-device.c $(INCL)
dump.o: dump.c $(INCL)
names.o: names.c $(INCL)
filter.o: filter.c $(INCL)
nbsd-libpci.o: nbsd-libpci.c $(INCL)
