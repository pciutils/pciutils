/*
 *  The PCI Library -- Direct Configuration access via SylixOS Ports
 *
 *  Copyright (c) 2018 YuJian.Gong <gongyujian@acoinfo.com>
 *
 *  Can be freely distributed and used under the terms of the GNU GPL.
 */

#define _GNU_SOURCE
#define  __SYLIXOS_KERNEL
#define  __SYLIXOS_PCI_DRV
#include <SylixOS.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define PCI_VENDOR_ID_IS_INVALID(vendor) (((vendor) == 0xffff) || ((vendor) == 0x0000))

typedef struct {
    struct pci_access *a;
    byte *busmap;
    int bus;
} pci_dev_scan;

static int
sylixos_pci_traversal (int (*function)(), void *arg, int min_bus, int max_bus)
{
  int bus, dev, func;
  u8 header;
  u16 vendor;

  if (!function || (min_bus < 0) || (max_bus < 0))
    {
      return  (PX_ERROR);
    }

  min_bus = (min_bus > (PCI_MAX_BUS - 1)) ? (PCI_MAX_BUS - 1) : min_bus;
  max_bus = (max_bus > (PCI_MAX_BUS - 1)) ? (PCI_MAX_BUS - 1) : max_bus;

  for (bus = min_bus; bus <= max_bus; bus++)
    {
      for (dev = 0; dev < PCI_MAX_SLOTS; dev++)
    {
      for (func = 0; func < PCI_MAX_FUNCTIONS; func++)
    {
      pciConfigInWord(bus, dev, func, PCI_VENDOR_ID, &vendor);
      if (PCI_VENDOR_ID_IS_INVALID(vendor))
        {
          if (func == 0)
            {
              break;
            }
          continue;
        }

      if (function(bus, dev, func, arg) != ERROR_NONE)
        {
          goto  __out;
        }

      if (func == 0)
        {
          pciConfigInByte(bus, dev, func, PCI_HEADER_TYPE, &header);
          if ((header & PCI_HEADER_MULTI_FUNC) != PCI_HEADER_MULTI_FUNC)
            {
              break;
            }
        }
    }
    }
    }

__out:
  return  (ERROR_NONE);
}

static int
pci_dev_list_create (int  bus, int  dev, int  func, void *arg)
{
  pci_dev_scan *f = (pci_dev_scan *)arg;
  struct pci_dev *d;
  u32 vd;

  f->busmap[bus] = 1;
  d = pci_alloc_dev(f->a);
  d->bus = bus;
  d->dev = dev;
  d->func = func;

  vd = pci_read_long(d, PCI_VENDOR_ID);
  d->vendor_id = vd & 0xffff;
  d->device_id = vd >> 16U;
  d->known_fields = PCI_FILL_IDENT;
  d->hdrtype = pci_read_byte(d, PCI_HEADER_TYPE) & 0x7f;
  pci_link_dev(f->a, d);

  return  (ERROR_NONE);
}

static void
pci_generic_scan_bus_tbl(struct pci_access *a, byte *busmap, int bus)
{
  pci_dev_scan    f;

  f.a = a;
  f.busmap = busmap;
  f.bus = bus;

  sylixos_pci_traversal(pci_dev_list_create, &f, bus, PCI_MAX_BUS);
}

static void
sylixos_scan(struct pci_access *a)
{
  int se;
  u8 busmap[256];
  char *env;

  memset(busmap, 0, sizeof(busmap));

  env = getenv(PCI_SCAN_FUNC);
  if (!env)
    {
      pci_generic_scan_bus(a, busmap, 0);
      return;
    }

  se = atoi(env);
  if (se)
    {
      pci_generic_scan_bus_tbl(a, busmap, 0);
    }
  else
    {
      pci_generic_scan_bus(a, busmap, 0);
    }
}

static void
sylixos_config(struct pci_access *a)
{
  pci_define_param(a, "sylixos.path", PCI_PATH_SYLIXOS_DEVICE, "Path to the SylixOS PCI device");
}

static int
sylixos_detect(struct pci_access *a)
{
  char *name = pci_get_param(a, "sylixos.path");

  if (access(name, R_OK))
    {
      a->warning("Cannot open %s", name);
      return 0;
    }

  a->debug("...using %s", name);

  return 1;
}

static void
sylixos_init(struct pci_access *a)
{
  a->fd = -1;
}

static void
sylixos_cleanup(struct pci_access *a)
{
    a->fd = -1;
}

static int
sylixos_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  int ret = -1;
  u8  data_byte = -1;
  u16 data_word = -1;
  u32 data_dword = -1;

  if (!(len == 1 || len == 2 || len == 4))
    {
      return pci_generic_block_read(d, pos, buf, len);
    }

  if (pos >= 256)
    {
      return 0;
    }

  switch (len)
    {
    case 1:
      ret = pciConfigInByte(d->bus, d->dev, d->func, pos, &data_byte);
      if (ret != ERROR_NONE)
        {
          return  (0);
        }
      buf[0] = (u8)data_byte;
      break;

    case 2:
      ret = pciConfigInWord(d->bus, d->dev, d->func, pos, &data_word);
      if (ret != ERROR_NONE)
        {
          return  (0);
        }
      ((u16 *) buf)[0] = cpu_to_le16(data_word);
      break;

    case 4:
      ret = pciConfigInDword(d->bus, d->dev, d->func, pos, &data_dword);
      if (ret != ERROR_NONE)
        {
          return  (0);
        }
      ((u32 *) buf)[0] = cpu_to_le32(data_dword);
      break;
    }

  return 1;
}

static int
sylixos_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  int ret = PX_ERROR;
  u8 data_byte;
  u16 data_word;
  u32 data_dword;

  if (!(len == 1 || len == 2 || len == 4))
    {
      return pci_generic_block_write(d, pos, buf, len);
    }

  if (pos >= 256)
    {
      return 0;
    }

  switch (len)
    {
    case 1:
      data_byte = buf[0];
      ret = pciConfigOutByte(d->bus, d->dev, d->func, pos, data_byte);
      if (ret != ERROR_NONE)
        {
          return  (0);
        }
      break;

    case 2:
      data_word = le16_to_cpu(((u16 *) buf)[0]);
      ret = pciConfigOutWord(d->bus, d->dev, d->func, pos, data_word);
      if (ret != ERROR_NONE)
        {
          return  (0);
        }
      break;

    case 4:
      data_dword = le32_to_cpu(((u32 *) buf)[0]);
      ret = pciConfigOutDword(d->bus, d->dev, d->func, pos, data_dword);
      if (ret != ERROR_NONE)
        {
          return  (0);
        }
      break;
    }

  return 1;
}

struct pci_methods pm_sylixos_device = {
  "sylixos-device",
  "SylixOS /proc/pci device",
  sylixos_config,					                                    /* config                       */
  sylixos_detect,
  sylixos_init,
  sylixos_cleanup,
  sylixos_scan,
  pci_generic_fill_info,
  sylixos_read,
  sylixos_write,
  NULL,					                                                /* read_vpd                     */
  NULL,					                                                /* init_dev                     */
  NULL					                                                /* cleanup_dev                  */
};
