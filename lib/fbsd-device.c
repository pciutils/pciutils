/*
 *	The PCI Library -- FreeBSD /dev/pci access
 *
 *	Copyright (c) 1999 Jari Kirma <kirma@cs.hut.fi>
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

#include <pci/pcivar.h>
#include <pci/pci_ioctl.h>

#include "internal.h"

static void
fbsd_config(struct pci_access *a)
{
  a->method_params[PCI_ACCESS_FBSD_DEVICE] = PATH_FBSD_DEVICE;
}

static int
fbsd_detect(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_FBSD_DEVICE];

  if (access(name, R_OK))
    {
      a->warning("Cannot open %s", name);
      return 0;
    }
  a->debug("...using %s", name);
  return 1;
}

static void
fbsd_init(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_FBSD_DEVICE];

  a->fd = open(name, O_RDWR, 0);
  if (a->fd < 0)
    {
      a->error("fbsd_init: %s open failed", name);
    }
}

static void
fbsd_cleanup(struct pci_access *a)
{
  close(a->fd);
}

static int
fbsd_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct pci_io pi;

  if (!(len == 1 || len == 2 || len == 4))
    {
      return pci_generic_block_read(d, pos, buf, len);
    }

  pi.pi_sel.pc_bus = d->bus;
  pi.pi_sel.pc_dev = d->dev;
  pi.pi_sel.pc_func = d->func;

  pi.pi_reg = pos;
  pi.pi_width = len;
	
  if (ioctl(d->access->fd, PCIOCREAD, &pi) < 0)
    d->access->error("fbsd_read: ioctl(PCIOCREAD) failed");
  
  switch (len)
    {
    case 1:
      buf[0] = (u8) pi.pi_data;
      break;
    case 2:
      ((u16 *) buf)[0] = (u16) pi.pi_data;
      break;
    case 4:
      ((u32 *) buf)[0] = (u32) pi.pi_data;
      break;
    }
  return 1;
}

static int
fbsd_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct pci_io pi;

  if (!(len == 1 || len == 2 || len == 4))
    {
      return pci_generic_block_write(d, pos, buf, len);
    }

  pi.pi_sel.pc_bus = d->bus;
  pi.pi_sel.pc_dev = d->dev;
  pi.pi_sel.pc_func = d->func;

  pi.pi_reg = pos;
  pi.pi_width = len;
	
  switch (len)
    {
    case 1:
      pi.pi_data = buf[0];
      break;
    case 2:
      pi.pi_data = ((u16 *) buf)[0];
      break;
    case 4:
      pi.pi_data = ((u32 *) buf)[0];
      break;
    }
  
  if (ioctl(d->access->fd, PCIOCWRITE, &pi) < 0)
    {
      d->access->error("fbsd_write: ioctl(PCIOCWRITE) failed");
    }

  return 1;
}

struct pci_methods pm_fbsd_device = {
  "FreeBSD-device",
  fbsd_config,
  fbsd_detect,
  fbsd_init,
  fbsd_cleanup,
  pci_generic_scan,
  pci_generic_fill_info,
  fbsd_read,
  fbsd_write,
  NULL,                                 /* dev_init */
  NULL                                  /* dev_cleanup */
};
