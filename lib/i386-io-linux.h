/*
 *	The PCI Library -- Access to i386 I/O ports on Linux
 *
 *	Copyright (c) 1997--2006 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/io.h>
#include <errno.h>

static int ioperm_enabled;
static int iopl_enabled;

static int
intel_setup_io(struct pci_access *a UNUSED)
{
  if (ioperm_enabled || iopl_enabled)
    return 1;

  /*
   * Before Linux 2.6.8, only the first 0x3ff I/O ports permissions can be
   * modified via ioperm(). Since 2.6.8 all ports are supported.
   * Since Linux 5.5, EFLAGS-based iopl() implementation was removed and
   * replaced by new TSS-IOPB-map-all-based emulator. Before Linux 5.5,
   * EFLAGS-based iopl() allowed userspace to enable/disable interrupts,
   * which is dangerous. So prefer usage of ioperm() and fallback to iopl().
   */
  if (ioperm(0xcf8, 8, 1) < 0) /* conf1 + conf2 ports */
    {
      if (errno == EINVAL) /* ioperm() unsupported */
        {
          if (iopl(3) < 0)
            return 0;
          iopl_enabled = 1;
          return 1;
        }
      return 0;
    }
  if (ioperm(0xc000, 0xfff, 1) < 0) /* remaining conf2 ports */
    {
      ioperm(0xcf8, 8, 0);
      return 0;
    }

  ioperm_enabled = 1;
  return 1;
}

static inline void
intel_cleanup_io(struct pci_access *a UNUSED)
{
  if (ioperm_enabled)
    {
      ioperm(0xcf8, 8, 0);
      ioperm(0xc000, 0xfff, 0);
      ioperm_enabled = 0;
    }

  if (iopl_enabled)
    {
      iopl(0);
      iopl_enabled = 0;
    }
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
