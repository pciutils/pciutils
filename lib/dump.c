/*
 *	$Id: dump.c,v 1.1 1999/01/22 21:05:20 mj Exp $
 *
 *	The PCI Library -- Reading of Bus Dumps
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "internal.h"

static int
dump_detect(struct pci_access *a)
{
  return !!a->method_params[PCI_ACCESS_DUMP];
}

static void
dump_init(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_DUMP];
  FILE *f;
  char buf[256];
  struct pci_dev *dev = NULL;
  int len, bn, dn, fn, i, j;

  if (!a)
    a->error("dump: File name not given.");
  if (!(f = fopen(name, "r")))
    a->error("dump: Cannot open %s: %s", name, strerror(errno));
  while (fgets(buf, sizeof(buf)-1, f))
    {
      char *z = strchr(buf, '\n');
      if (!z)
	a->error("dump: line too long or unterminated");
      *z-- = 0;
      if (z >= buf && *z == '\r')
	*z-- = 0;
      len = z - buf + 1;
      if (len >= 8 && buf[2] == ':' && buf[5] == '.' && buf[7] == ' ' &&
	  sscanf(buf, "%x:%x.%d ", &bn, &dn, &fn) == 3)
	{
	  dev = pci_get_dev(a, bn, dn, fn);
	  dev->aux = pci_malloc(a, 256);
	  memset(dev->aux, 0xff, 256);
	  pci_link_dev(a, dev);
	}
      else if (!len)
	dev = NULL;
      else if (dev && len >= 51 && buf[2] == ':' && buf[3] == ' ' &&
	       sscanf(buf, "%x: ", &i) == 1)
	{
	  z = buf+3;
	  while (isspace(z[0]) && isxdigit(z[1]) && isxdigit(z[2]))
	    {
	      z++;
	      if (sscanf(z, "%x", &j) != 1 || i >= 256)
		a->error("dump: Malformed line");
	      ((byte *) dev->aux)[i++] = j;
	      z += 2;
	    }
	}
    }
}

static void
dump_cleanup(struct pci_access * UNUSED a)
{
}

static void
dump_scan(struct pci_access * UNUSED a)
{
}

static int
dump_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  if (!d->aux)
    {
      struct pci_dev *e = d->access->devices;
      while (e && (e->bus != d->bus || e->dev != d->dev || e->func != d->func))
	e = e->next;
      if (e)
	d = e;
      else
	return 0;
    }
  memcpy(buf, (byte *) d->aux + pos, len);
  return 1;
}

static int
dump_write(struct pci_dev * UNUSED d, int UNUSED pos, byte * UNUSED buf, int UNUSED len)
{
  d->access->error("Writing to dump files is not supported.");
  return 0;
}

static void
dump_cleanup_dev(struct pci_dev *d)
{
  if (d->aux)
    {
      pci_mfree(d->aux);
      d->aux = NULL;
    }
}

struct pci_methods pm_dump = {
  "dump",
  NULL,					/* config */
  dump_detect,
  dump_init,
  dump_cleanup,
  dump_scan,
  pci_generic_fill_info,
  dump_read,
  dump_write,
  NULL,					/* init_dev */
  dump_cleanup_dev
};
