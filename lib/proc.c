/*
 *	The PCI Library -- Configuration Access via /proc/bus/pci
 *
 *	Copyright (c) 1997--2003 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>

#include "internal.h"
#include "pread.h"

static void
proc_config(struct pci_access *a)
{
  a->method_params[PCI_ACCESS_PROC_BUS_PCI] = PCI_PATH_PROC_BUS_PCI;
}

static int
proc_detect(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_PROC_BUS_PCI];

  if (access(name, R_OK))
    {
      a->warning("Cannot open %s", name);
      return 0;
    }
  a->debug("...using %s", name);
  return 1;
}

static void
proc_init(struct pci_access *a)
{
  a->fd = -1;
}

static void
proc_cleanup(struct pci_access *a)
{
  if (a->fd >= 0)
    {
      close(a->fd);
      a->fd = -1;
    }
}

static void
proc_scan(struct pci_access *a)
{
  FILE *f;
  char buf[512];

  if (snprintf(buf, sizeof(buf), "%s/devices", a->method_params[PCI_ACCESS_PROC_BUS_PCI]) == sizeof(buf))
    a->error("File name too long");
  f = fopen(buf, "r");
  if (!f)
    a->error("Cannot open %s", buf);
  while (fgets(buf, sizeof(buf)-1, f))
    {
      struct pci_dev *d = pci_alloc_dev(a);
      unsigned int dfn, vend, cnt, known;

#define F " " PCIADDR_T_FMT
      cnt = sscanf(buf, "%x %x %x" F F F F F F F F F F F F F F,
	     &dfn,
	     &vend,
	     &d->irq,
	     &d->base_addr[0],
	     &d->base_addr[1],
	     &d->base_addr[2],
	     &d->base_addr[3],
	     &d->base_addr[4],
	     &d->base_addr[5],
	     &d->rom_base_addr,
	     &d->size[0],
	     &d->size[1],
	     &d->size[2],
	     &d->size[3],
	     &d->size[4],
	     &d->size[5],
	     &d->rom_size);
#undef F
      if (cnt != 9 && cnt != 10 && cnt != 17)
	a->error("proc: parse error (read only %d items)", cnt);
      d->bus = dfn >> 8U;
      d->dev = PCI_SLOT(dfn & 0xff);
      d->func = PCI_FUNC(dfn & 0xff);
      d->vendor_id = vend >> 16U;
      d->device_id = vend & 0xffff;
      known = PCI_FILL_IDENT;
      if (!a->buscentric)
	{
	  known |= PCI_FILL_IRQ | PCI_FILL_BASES;
	  if (cnt >= 10)
	    known |= PCI_FILL_ROM_BASE;
	  if (cnt >= 17)
	    known |= PCI_FILL_SIZES;
	}
      d->known_fields = known;
      pci_link_dev(a, d);
    }
  fclose(f);
}

static int
proc_setup(struct pci_dev *d, int rw)
{
  struct pci_access *a = d->access;

  if (a->cached_dev != d || a->fd_rw < rw)
    {
      char buf[1024];
      int e;
      if (a->fd >= 0)
	close(a->fd);
      e = snprintf(buf, sizeof(buf), "%s/%02x/%02x.%d",
		   a->method_params[PCI_ACCESS_PROC_BUS_PCI],
		   d->bus, d->dev, d->func);
      if (e < 0 || e >= (int) sizeof(buf))
	a->error("File name too long");
      a->fd_rw = a->writeable || rw;
      a->fd = open(buf, a->fd_rw ? O_RDWR : O_RDONLY);
      if (a->fd < 0)
	a->warning("Cannot open %s", buf);
      a->cached_dev = d;
      a->fd_pos = 0;
    }
  return a->fd;
}

static int
proc_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = proc_setup(d, 0);
  int res;

  if (fd < 0)
    return 0;
  res = do_read(d, fd, buf, len, pos);
  if (res < 0)
    {
      d->access->warning("proc_read: read failed: %s", strerror(errno));
      return 0;
    }
  else if (res != len)
    return 0;
  return 1;
}

static int
proc_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = proc_setup(d, 1);
  int res;

  if (fd < 0)
    return 0;
  res = do_write(d, fd, buf, len, pos);
  if (res < 0)
    {
      d->access->warning("proc_write: write failed: %s", strerror(errno));
      return 0;
    }
  else if (res != len)
    {
      d->access->warning("proc_write: tried to write %d bytes at %d, but only %d succeeded", len, pos, res);
      return 0;
    }
  return 1;
}

static void
proc_cleanup_dev(struct pci_dev *d)
{
  if (d->access->cached_dev == d)
    d->access->cached_dev = NULL;
}

struct pci_methods pm_linux_proc = {
  "Linux-proc",
  proc_config,
  proc_detect,
  proc_init,
  proc_cleanup,
  proc_scan,
  pci_generic_fill_info,
  proc_read,
  proc_write,
  NULL,					/* init_dev */
  proc_cleanup_dev
};
