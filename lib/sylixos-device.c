/*
 *	The PCI Library -- Direct Configuration access via SylixOS Ports
 *
 *	Copyright (c) 2018 YuJian.Gong <gongyujian@acoinfo.com>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#define  __SYLIXOS_KERNEL
#define  __SYLIXOS_PCI_DRV
#include <SylixOS.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static void
sylixos_scan(struct pci_access *a)
{
  u8 busmap[256];
  int bus;

  memset(busmap, 0, sizeof(busmap));

  for (bus = 0; bus < PCI_MAX_BUS; bus++)
    if (!busmap[bus])
      pci_generic_scan_bus(a, busmap, 0, bus);
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
sylixos_init(struct pci_access *a UNUSED)
{
}

static void
sylixos_cleanup(struct pci_access *a UNUSED)
{
}

static int
sylixos_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  int ret = -1;
  u8  data_byte = -1;
  u16 data_word = -1;
  u32 data_dword = -1;

  if (!(len == 1 || len == 2 || len == 4))
    return pci_generic_block_read(d, pos, buf, len);

  if (pos >= 256)
    return 0;

  switch (len)
    {
    case 1:
      ret = pciConfigInByte(d->bus, d->dev, d->func, pos, &data_byte);
      if (ret != ERROR_NONE)
	return 0;
      buf[0] = (u8)data_byte;
      break;

    case 2:
      ret = pciConfigInWord(d->bus, d->dev, d->func, pos, &data_word);
      if (ret != ERROR_NONE)
	return 0;
      ((u16 *) buf)[0] = cpu_to_le16(data_word);
      break;

    case 4:
      ret = pciConfigInDword(d->bus, d->dev, d->func, pos, &data_dword);
      if (ret != ERROR_NONE)
	return 0;
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
    return pci_generic_block_write(d, pos, buf, len);

  if (pos >= 256)
    return 0;

  switch (len)
    {
    case 1:
      data_byte = buf[0];
      ret = pciConfigOutByte(d->bus, d->dev, d->func, pos, data_byte);
      if (ret != ERROR_NONE)
	return 0;
      break;

    case 2:
      data_word = le16_to_cpu(((u16 *) buf)[0]);
      ret = pciConfigOutWord(d->bus, d->dev, d->func, pos, data_word);
      if (ret != ERROR_NONE)
	return 0;
      break;

    case 4:
      data_dword = le32_to_cpu(((u32 *) buf)[0]);
      ret = pciConfigOutDword(d->bus, d->dev, d->func, pos, data_dword);
      if (ret != ERROR_NONE)
	return 0;
      break;
    }

  return 1;
}

struct pci_methods pm_sylixos_device = {
  .name = "sylixos-device",
  .help = "SylixOS /proc/pci device",
  .config = sylixos_config,
  .detect = sylixos_detect,
  .init = sylixos_init,
  .cleanup = sylixos_cleanup,
  .scan = sylixos_scan,
  .fill_info = pci_generic_fill_info,
  .read = sylixos_read,
  .write = sylixos_write,
};
