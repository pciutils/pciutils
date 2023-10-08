/*
 *	The PCI Library -- Access to i386 I/O ports on Solaris
 *
 *	Copyright (c) 2003 Bill Moore <billm@eng.sun.com>
 *	Copyright (c) 2003--2006 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/sysi86.h>
#include <sys/psw.h>

#include "i386-io-access.h"

static int
intel_setup_io(struct pci_access *a UNUSED)
{
  return (sysi86(SI86V86, V86SC_IOPL, PS_IOPL) < 0) ? 0 : 1;
}

static inline void
intel_cleanup_io(struct pci_access *a UNUSED)
{
  /* FIXME: How to switch off I/O port access? */
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
