/*
 *	The PCI Library -- Reading of Bus Dumps
 *
 *	Copyright (c) 1997--2004 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "internal.h"

struct dump_data {
  int len;
  byte data[1];
};

static int
dump_detect(struct pci_access *a)
{
  return !!a->method_params[PCI_ACCESS_DUMP];
}

static void
dump_alloc_data(struct pci_dev *dev, int len)
{
  struct dump_data *dd = pci_malloc(dev->access, sizeof(struct dump_data) + len - 1);
  dd->len = len;
  memset(dd->data, 0xff, len);
  dev->aux = dd;
}

static void
dump_init(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_DUMP];
  FILE *f;
  char buf[256];
  struct pci_dev *dev = NULL;
  int len, mn, bn, dn, fn, i, j;

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
      mn = 0;
      if ((len >= 8 && buf[2] == ':' && buf[5] == '.' && buf[7] == ' ' &&
	   sscanf(buf, "%x:%x.%d ", &bn, &dn, &fn) == 3) ||
	  (len >= 13 && buf[4] == ':' && buf[7] == ':' && buf[10] == '.' && buf[12] == ' ' &&
	   sscanf(buf, "%x:%x:%x.%d", &mn, &bn, &dn, &fn) == 4))
	{
	  dev = pci_get_dev(a, mn, bn, dn, fn);
	  dump_alloc_data(dev, 256);
	  pci_link_dev(a, dev);
	}
      else if (!len)
	dev = NULL;
      else if (dev &&
	       (len >= 51 && buf[2] == ':' && buf[3] == ' ' || len >= 52 && buf[3] == ':' && buf[4] == ' ') &&
	       sscanf(buf, "%x: ", &i) == 1)
	{
	  struct dump_data *dd = dev->aux;
	  z = strchr(buf, ' ') + 1;
	  while (isspace(z[0]) && isxdigit(z[1]) && isxdigit(z[2]))
	    {
	      z++;
	      if (sscanf(z, "%x", &j) != 1 || i >= 256)
		a->error("dump: Malformed line");
	      if (i >= 4096)
		break;
	      if (i > dd->len)		/* Need to re-allocate the buffer */
		{
		  dump_alloc_data(dev, 4096);
		  memcpy(((struct dump_data *) dev->aux)->data, dd->data, 256);
		  pci_mfree(dd);
		  dd = dev->aux;
		}
	      dd->data[i++] = j;
	      z += 2;
	    }
	}
    }
}

static void
dump_cleanup(struct pci_access *a UNUSED)
{
}

static void
dump_scan(struct pci_access *a UNUSED)
{
}

static int
dump_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct dump_data *dd;
  if (!(dd = d->aux))
    {
      struct pci_dev *e = d->access->devices;
      while (e && (e->bus != d->bus || e->dev != d->dev || e->func != d->func))
	e = e->next;
      if (!e)
	return 0;
      dd = e->aux;
    }
  if (pos + len > dd->len)
    return 0;
  memcpy(buf, dd->data + pos, len);
  return 1;
}

static int
dump_write(struct pci_dev *d UNUSED, int pos UNUSED, byte *buf UNUSED, int len UNUSED)
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
