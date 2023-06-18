/*
 *	The PCI Library -- Access to i386 I/O ports on GNU Hurd
 *
 *	Copyright (c) 2003 Marco Gerards <metgerards@student.han.nl>
 *	Copyright (c) 2003 Martin Mares <mj@ucw.cz>
 *	Copyright (c) 2006 Samuel Thibault <samuel.thibault@ens-lyon.org> and
 *	                   Thomas Schwinge <tschwinge@gnu.org>
 *	Copyright (c) 2007 Thomas Schwinge <tschwinge@gnu.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/io.h>

static inline int
intel_setup_io(struct pci_access *a UNUSED)
{
  return (ioperm (0, 65535, 1) == -1) ? 0 : 1;
}

static inline void
intel_cleanup_io(struct pci_access *a UNUSED)
{
  ioperm (0, 65535, 0);
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
