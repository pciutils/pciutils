/*
 *	The PCI Library -- MiniOS PCI frontend access
 *
 *	Samuel Thibault <samuel.thibault@eu.citrix.com>, 2008
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <os.h>
#include <pcifront.h>
#include <xenbus.h>
#include "internal.h"

static int
minios_detect(struct pci_access *a)
{
  return 1;
}

static void
minios_init(struct pci_access *a)
{
}

static void
minios_cleanup(struct pci_access *a)
{
  shutdown_pcifront(NULL);
}

static void
minios_scan(struct pci_access *a)
{
  void func(unsigned int domain, unsigned int bus, unsigned int slot, unsigned int fun)
  {
    struct pci_dev *d = pci_alloc_dev(a);

    d->domain = domain;
    d->bus = bus;
    d->dev = slot;
    d->func = fun;

    pci_link_dev(a, d);
  }

  pcifront_scan(NULL, func);
}

static int
minios_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  unsigned int val;
  switch (len) {
    case 1:
      if (pcifront_conf_read(NULL, d->domain, d->bus, d->dev, d->func, pos, len, &val))
        return 0;
      * buf = val;
      return 1;
    case 2:
      if (pcifront_conf_read(NULL, d->domain, d->bus, d->dev, d->func, pos, len, &val))
        return 0;
      *(u16 *) buf = cpu_to_le16((u16) val);
      return 1;
    case 4:
      if (pcifront_conf_read(NULL, d->domain, d->bus, d->dev, d->func, pos, len, &val))
        return 0;
      *(u32 *) buf = cpu_to_le32((u32) val);
      return 1;
    default:
      return pci_generic_block_read(d, pos, buf, len);
  }
}

static int
minios_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  unsigned int val;
  switch (len) {
    case 1:
      val = * buf;
      break;
    case 2:
      val = le16_to_cpu(*(u16 *) buf);
      break;
    case 4:
      val = le32_to_cpu(*(u32 *) buf);
      break;
    default:
      return pci_generic_block_write(d, pos, buf, len);
  }
  return !pcifront_conf_write(NULL, d->domain, d->bus, d->dev, d->func, pos, len, val);
}

struct pci_methods pm_minios = {
  .name = "minios",
  .help = "Xen MiniOS",
  .detect = minios_detect,
  .init = minios_init,
  .cleanup = minios_cleanup,
  .scan = minios_scan,
  .fill_info = pci_generic_fill_info,
  .read = minios_read,
  .write = minios_write,
};
