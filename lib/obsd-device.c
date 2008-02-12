/*
 *	The PCI Library -- OpenBSD /dev/pci access
 *
 *	Adapted from fbsd-device.c by Matthieu Herrb <matthieu.herrb@laas.fr>, 2006
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/endian.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/pciio.h>
#include "internal.h"

static void
obsd_config(struct pci_access *a)
{
  pci_define_param(a, "obsd.path", PCI_PATH_OBSD_DEVICE, "Path to the OpenBSD PCI device");
}

static int
obsd_detect(struct pci_access *a)
{
  char *name = pci_get_param(a, "obsd.path");

  if (access(name, R_OK))
    {
      a->warning("Cannot open %s", name);
      return 0;
    }
  a->debug("...using %s", name);
  return 1;
}

static void
obsd_init(struct pci_access *a)
{
  char *name = pci_get_param(a, "obsd.path");

  a->fd = open(name, O_RDWR, 0);
  if (a->fd < 0)
    a->error("obsd_init: %s open failed", name);
}

static void
obsd_cleanup(struct pci_access *a)
{
  close(a->fd);
}

static int
obsd_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct pci_io pi;
  union {
	  u_int32_t u32;
	  u_int16_t u16[2];
	  u_int8_t u8[4];
  } u;

  if (!(len == 1 || len == 2 || len == 4))
    return pci_generic_block_read(d, pos, buf, len);

  if (pos >= 256)
    return 0;

  pi.pi_sel.pc_bus = d->bus;
  pi.pi_sel.pc_dev = d->dev;
  pi.pi_sel.pc_func = d->func;

  pi.pi_reg = pos - (pos % 4);
  pi.pi_width = 4;

  if (ioctl(d->access->fd, PCIOCREAD, &pi) < 0) {
	  if (errno == ENXIO)
		  pi.pi_data = 0xffffffff;
	  else
		  d->access->error("obsd_read: ioctl(PCIOCREAD) failed");
  }
  u.u32 = pi.pi_data;

  switch (len)
    {
    case 1:
      buf[0] = (u8) u.u8[pos % 4];
      break;
    case 2:
      ((u16 *) buf)[0] = letoh16(u.u16[(pos % 4) / 2]);
      break;
    case 4:
      ((u32 *) buf)[0] = (u32) letoh32(pi.pi_data);
      break;
    }
  return 1;
}

static int
obsd_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct pci_io pi;

  if (!(len == 1 || len == 2 || len == 4))
    return pci_generic_block_write(d, pos, buf, len);

  if (pos >= 256)
    return 0;

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
    d->access->error("obsd_write: ioctl(PCIOCWRITE) failed");

  return 1;
}

struct pci_methods pm_obsd_device = {
  "obsd-device",
  "/dev/pci on OpenBSD",
  obsd_config,
  obsd_detect,
  obsd_init,
  obsd_cleanup,
  pci_generic_scan,
  pci_generic_fill_info,
  obsd_read,
  obsd_write,
  NULL,                                 /* dev_init */
  NULL                                  /* dev_cleanup */
};
