/*
 *	$Id: syscalls.c,v 1.1 1999/01/22 21:05:42 mj Exp $
 *
 *	The PCI Library -- Configuration Access via Syscalls
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "internal.h"

static int
sysc_detect(struct pci_access *a)
{
  return 0;
}

static void
sysc_init(struct pci_access *a)
{
}

static void
sysc_cleanup(struct pci_access *a)
{
}

static int
sysc_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  return 0;
}

static int
sysc_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  return 0;
}

struct pci_methods pm_syscalls = {
  "syscalls",
  NULL,					/* config */
  sysc_detect,
  sysc_init,
  sysc_cleanup,
  pci_generic_scan,
  pci_generic_fill_info,
  sysc_read,
  sysc_write,
  NULL,					/* init_dev */
  NULL					/* cleanup_dev */
};
