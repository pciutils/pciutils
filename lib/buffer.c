/*
 *	$Id: buffer.c,v 1.1 1999/01/22 21:05:14 mj Exp $
 *
 *	The PCI Library -- Buffered Access
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "internal.h"

static int
buff_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  memcpy(buf, (byte *)d->aux + pos, len);
  return 1;
}

static int
buff_write(struct pci_dev *d, int UNUSED pos, byte * UNUSED buf, int UNUSED len)
{
  d->access->error("buffer: Writing to configuration space not supported.");
  return 0;
}

static struct pci_methods pm_buffer = {
  "Buffer",
  NULL,					/* config */
  NULL,					/* Shall not be called */
  NULL,					/* No init nor cleanup */
  NULL,
  NULL,					/* No scanning */
  pci_generic_fill_info,
  buff_read,
  buff_write,
  NULL,					/* init_dev */
  NULL					/* cleanup_dev */
};

void
pci_setup_buffer(struct pci_dev *d, byte *buf)
{
  if (d->methods->cleanup_dev)
    d->methods->cleanup_dev(d);
  d->methods = &pm_buffer;
  d->aux = buf;
}
