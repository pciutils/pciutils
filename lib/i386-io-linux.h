/*
 *	The PCI Library -- Access to i386 I/O ports on Linux
 *
 *	Copyright (c) 1997--2006 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <sys/io.h>

static int
intel_setup_io(struct pci_access *a UNUSED)
{
  return (iopl(3) < 0) ? 0 : 1;
}

static inline int
intel_cleanup_io(struct pci_access *a UNUSED)
{
  iopl(3);
  return -1;
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
