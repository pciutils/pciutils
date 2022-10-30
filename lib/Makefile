# Makefile for The PCI Library
# (c) 1999--2014 Martin Mares <mj@ucw.cz>

# Expects to be invoked from the top-level Makefile and uses lots of its variables.

OBJS=init access generic dump names filter names-hash names-parse names-net names-cache names-hwdb params caps
INCL=internal.h pci.h config.h header.h sysdep.h types.h

ifdef PCI_HAVE_PM_LINUX_SYSFS
OBJS += sysfs
endif

ifdef PCI_HAVE_PM_LINUX_PROC
OBJS += proc
endif

ifdef PCI_HAVE_PM_INTEL_CONF
OBJS += i386-ports
endif

ifdef PCI_HAVE_PM_DUMP
OBJS += dump
endif

ifdef PCI_HAVE_PM_FBSD_DEVICE
OBJS += fbsd-device
CFLAGS += -I/usr/src/sys
ifdef FREEBSD_SYS
CFLAGS += -I${FREEBSD_SYS}
endif
endif

ifdef PCI_HAVE_PM_OBSD_DEVICE
OBJS += obsd-device
endif

ifdef PCI_HAVE_PM_AIX_DEVICE
OBJS += aix-device
endif

ifdef PCI_HAVE_PM_NBSD_LIBPCI
OBJS += nbsd-libpci
endif

ifdef PCI_HAVE_PM_DARWIN_DEVICE
OBJS += darwin
endif

ifdef PCI_HAVE_PM_SYLIXOS_DEVICE
OBJS += sylixos-device
endif

ifdef PCI_HAVE_PM_HURD_CONF
OBJS += hurd
endif

ifdef PCI_HAVE_PM_WIN32_CFGMGR32
OBJS += emulated
OBJS += win32-cfgmgr32
endif

ifdef PCI_HAVE_PM_WIN32_SYSDBG
OBJS += win32-sysdbg
endif

all: $(PCILIB) $(PCILIBPC)

ifeq ($(SHARED),no)
$(PCILIB): $(addsuffix .o,$(OBJS))
	rm -f $@
	$(AR) rcs $@ $^
	$(RANLIB) $@
else
CFLAGS += -fPIC -fvisibility=hidden
$(PCILIB): $(addsuffix .o,$(OBJS))
 ifdef PCI_HAVE_PM_DARWIN_DEVICE
	$(CC) -shared $(LDFLAGS) $(SONAME) -Wl,-install_name,$(LIBDIR)/$(PCILIB) -o $@ $^ $(LIB_LDLIBS)
 else
	$(CC) -shared $(LDFLAGS) $(SONAME) -Wl,--version-script=libpci.ver -o $@ $^ $(LIB_LDLIBS)
 endif
endif

$(PCILIBPC): libpci.pc.in
	sed <$< >$@ -e 's,@PREFIX@,$(PREFIX),' \
		-e 's,@INCDIR@,$(INCDIR),' \
		-e 's,@LIBDIR@,$(LIBDIR),' \
		-e 's,@IDSDIR@,$(IDSDIR),' \
		-e 's,@VERSION@,$(VERSION),' \
		-e 's,@LDLIBS@,$(LDLIBS),' \
		-e 's,@WITH_LIBS@,$(WITH_LIBS),'

init.o: init.c $(INCL)
access.o: access.c $(INCL)
params.o: params.c $(INCL)
i386-ports.o: i386-ports.c $(INCL) i386-io-hurd.h i386-io-linux.h i386-io-sunos.h i386-io-windows.h i386-io-cygwin.h
proc.o: proc.c $(INCL) pread.h
sysfs.o: sysfs.c $(INCL) pread.h
generic.o: generic.c $(INCL)
emulated.o: emulated.c $(INCL)
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
names-hwdb.o: names-hwdb.c $(INCL) names.h
filter.o: filter.c $(INCL)
nbsd-libpci.o: nbsd-libpci.c $(INCL)
hurd.o: hurd.c $(INCL)
win32-cfgmgr32.o: win32-cfgmgr32.c $(INCL)
win32-sysdbg.o: win32-sysdbg.c $(INCL)

# MinGW32 toolchain has some required Win32 header files in /ddk subdirectory.
# But these header files include another header files from /ddk subdirectory
# and expect that build system has already set /ddk subdirectory into includes.
# So include /ddk subdirectory of each system predefined include path via -I.
ifdef PCI_HAVE_PM_WIN32_CFGMGR32
DDKCFLAGS:=$(shell echo | $(CC) $(CFLAGS) -E -Wp,-v -o /dev/null - 2>&1 | sed -n 's/^ \(.*\)/-I\1\/ddk/p')
win32-cfgmgr32.o: override CFLAGS+=$(DDKCFLAGS)
endif
