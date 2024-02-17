/*
 *	The PCI Library -- Access to i386 I/O ports on OpenBSD
 *
 *	Copyright (c) 2023 Grant Pannell <grant@pannell.net.au>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <sys/types.h>
#include <machine/sysarch.h>
#include <machine/pio.h>

#include "i386-io-access.h"

#if defined(__amd64__)
  #define obsd_iopl amd64_iopl
#else
  #define obsd_iopl i386_iopl
#endif

static int iopl_enabled;

static int
intel_setup_io(struct pci_access *a UNUSED)
{
  if (iopl_enabled)
    return 1;

  if (obsd_iopl(3) < 0)
    {
      return 0;
    }

  iopl_enabled = 1;
  return 1;
}

static inline void
intel_cleanup_io(struct pci_access *a UNUSED)
{
  if (iopl_enabled)
    {
      obsd_iopl(0);
      iopl_enabled = 0;
    }
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
