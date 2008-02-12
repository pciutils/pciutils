/*
 *	The PCI Library -- NetBSD libpci access
 *         (based on FreeBSD /dev/pci access)
 *
 *	Copyright (c) 1999 Jari Kirma <kirma@cs.hut.fi>
 *      Copyright (c) 2002 Quentin Garnier <cube@cubidou.net>
 *	Copyright (c) 2002 Martin Mares <mj@ucw.cz>
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
  pci_define_param(a, "nbsd.path", PCI_PATH_NBSD_DEVICE, "Path to the NetBSD PCI device");
}

static int
nbsd_detect(struct pci_access *a)
{
  char *name = pci_get_param(a, "nbsd.path");

  if (access(name, R_OK))
    {
      a->warning("Cannot open %s", name);
      return 0;
    }

  if (!access(name, W_OK))
    a->writeable = O_RDWR;
  a->debug("...using %s", name);
  return 1;
}

static void
nbsd_init(struct pci_access *a)
{
  char *name = pci_get_param(a, "nbsd.path");
  int mode = a->writeable ? O_RDWR : O_RDONLY;

  a->fd = open(name, mode, 0);
  if (a->fd < 0)
    a->error("nbsd_init: %s open failed", name);
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
  int shift;

  if (!(len == 1 || len == 2 || len == 4))
    return pci_generic_block_read(d, pos, buf, len);

  if (pos >= 256)
    return 0;

  shift = 8*(pos % 4);
  pos &= ~3;

  if (pcibus_conf_read(d->access->fd, d->bus, d->dev, d->func, pos, &val) < 0)
    d->access->error("nbsd_read: pci_bus_conf_read() failed");

  switch (len)
    {
    case 1:
      *buf = val >> shift;
      break;
    case 2:
      *(u16*)buf = cpu_to_le16(val >> shift);
      break;
    case 4:
      *(u32*)buf = cpu_to_le32(val);
      break;
    }
  return 1;
}

static int
nbsd_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  pcireg_t val = 0;
  int shift;

  if (!(len == 1 || len == 2 || len == 4))
    return pci_generic_block_write(d, pos, buf, len);

  if (pos >= 256)
    return 0;

  /*
   *  BEWARE: NetBSD seems to support only 32-bit access, so we have
   *  to emulate byte and word writes by read-modify-write, possibly
   *  causing troubles.
   */

  shift = 8*(pos % 4);
  pos &= ~3;
  if (len != 4)
    {
      if (pcibus_conf_read(d->access->fd, d->bus, d->dev, d->func, pos, &val) < 0)
	d->access->error("nbsd_write: pci_bus_conf_read() failed");
    }

  switch (len)
    {
    case 1:
      val = (val & ~(0xff << shift)) | (buf[0] << shift);
      break;
    case 2:
      val = (val & ~(0xffff << shift)) | (le16_to_cpu(*(u16*)buf) << shift);
      break;
    case 4:
      val = le32_to_cpu(*(u32*)buf);
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
