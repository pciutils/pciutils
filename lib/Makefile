# $Id: Makefile,v 1.4 2000/04/21 11:58:00 mj Exp $
# Makefile for The PCI Library
# (c) 1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

include config.mk

OBJS=access.o generic.o dump.o names.o filter.o
INCL=internal.h pci.h config.h

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
endif

ifdef HAVE_PM_AIX_DEVICE
OBJS += aix-device.o
endif

ifdef HAVE_OWN_HEADER_H
INCL += header.h
endif

all: libpci.a

libpci.a: $(OBJS)
	rm -f $@
	ar rcs $@ $^
	ranlib $@

access.o: access.c $(INCL)
i386-ports.o: i386-ports.c $(INCL)
proc.o: proc.c $(INCL)
generic.o: generic.c $(INCL)
syscalls.o: syscalls.c $(INCL)
fbsd-device.o: fbsd-device.c $(INCL)
aix-device.o: aix-device.c $(INCL)
dump.o: dump.c $(INCL)
names.o: names.c $(INCL)
filter.o: filter.c $(INCL)
