/*
 *	The PCI Library -- NetBSD libpci access
 *         (based on FreeBSD /dev/pci access)
 *
 *	Copyright (c) 1999 Jari Kirma <kirma@cs.hut.fi>
 *      Copyright (c) 2002 Quentin Garnier <cube@cubidou.net>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/*
 *      Read functionality of this driver is briefly tested, and seems
 *      to supply basic information correctly, but I promise no more.
 */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <pci.h>

#include "internal.h"

static void
nbsd_config(struct pci_access *a)
{
  a->method_params[PCI_ACCESS_NBSD_LIBPCI] = PATH_NBSD_DEVICE;
}

static int
nbsd_detect(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_NBSD_LIBPCI];

  if (access(name, R_OK))
    {
      a->warning("Cannot open %s", name);
      return 0;
    }
  a->debug("...using %s", name);
  return 1;
}

static void
nbsd_init(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_NBSD_LIBPCI];

  a->fd = open(name, O_RDWR, 0);
  if (a->fd < 0)
    {
      a->error("nbsd_init: %s open failed", name);
    }
}

static void
nbsd_cleanup(struct pci_access *a)
{
  close(a->fd);
}

static int
nbsd_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  pcireg_t val;

  if (!(len == 1 || len == 2 || len == 4))
    {
      return pci_generic_block_read(d, pos, buf, len);
    }

	
  if (pcibus_conf_read(d->access->fd, d->bus, d->dev, d->func, pos, &val) < 0)
    d->access->error("nbsd_read: pci_bus_conf_read() failed");
  
  switch (len)
    {
    case 1:
      buf[0] = (u8) ((val>>16) & 0xff);
      break;
    case 2:
      ((u16 *) buf)[0] = (u16) val;
      break;
    case 4:
      ((u32 *) buf)[0] = (u32) val;
      break;
    }
  return 1;
}

static int
nbsd_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  pcireg_t val;

  if (!(len == 1 || len == 2 || len == 4))
    {
      return pci_generic_block_write(d, pos, buf, len);
    }

  switch (len)
    {
    case 1:
      val = buf[0];
      break;
    case 2:
      val = ((u16 *) buf)[0];
      break;
    case 4:
      val = ((u32 *) buf)[0];
      break;
    }
  
  if (pcibus_conf_write(d->access->fd, d->bus, d->dev, d->func, pos, val) < 0)
    d->access->error("nbsd_write: pci_bus_conf_write() failed");

  return 1;
}

struct pci_methods pm_nbsd_libpci = {
  "NetBSD-libpci",
  nbsd_config,
  nbsd_detect,
  nbsd_init,
  nbsd_cleanup,
  pci_generic_scan,
  pci_generic_fill_info,
  nbsd_read,
  nbsd_write,
  NULL,                                 /* dev_init */
  NULL                                  /* dev_cleanup */
};
