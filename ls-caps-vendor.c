/*
 *	The PCI Utilities -- Show Vendor-specific Capabilities
 *
 *	Copyright (c) 2014 Gerd Hoffmann <kraxel@redhat.com>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "lspci.h"

static int
show_vendor_caps_virtio(struct device *d, int where, int cap)
{
  int length = BITS(cap, 0, 8);
  int type = BITS(cap, 8, 8);
  char *tname;
  u32 offset;
  u32 size;
  u32 offset_hi;
  u32 size_hi;

  if (length < 16)
    return 0;
  if (!config_fetch(d, where, length))
    return 0;

  switch (type)
    {
    case 1:
      tname = "CommonCfg";
      break;
    case 2:
      tname = "Notify";
      break;
    case 3:
      tname = "ISR";
      break;
    case 4:
      tname = "DeviceCfg";
      break;
    case 8:
      tname = "SharedMemory";
      break;
    default:
      tname = "<unknown>";
      break;
    }

  printf("VirtIO: %s\n", tname);

  if (verbose < 2)
    return 1;

  offset = get_conf_long(d, where + 8);
  size = get_conf_long(d, where + 12);
  if (type != 8)  
    printf("\t\tBAR=%d offset=%08x size=%08x",
      get_conf_byte(d, where +  4), offset, size);
  else {
    offset_hi = get_conf_long(d, where + 16);
    size_hi = get_conf_long(d, where + 20);
    printf("\t\tBAR=%d offset=%016lx size=%016lx id=%d",
      get_conf_byte(d, where +  4),
      (u64) offset | (u64) offset_hi << 32,
      (u64) size | (u64) size_hi << 32,
      get_conf_byte(d, where + 5));
  }

  if (type == 2 && length >= 20)
    printf(" multiplier=%08x", get_conf_long(d, where+16));

  printf("\n");
  return 1;
}

static int
do_show_vendor_caps(struct device *d, int where, int cap)
{
  switch (d->dev->vendor_id)
    {
    case 0x1af4: /* Red Hat */
      if (d->dev->device_id >= 0x1000 &&
	  d->dev->device_id <= 0x107f)
	return show_vendor_caps_virtio(d, where, cap);
      break;
    }
  return 0;
}

void
show_vendor_caps(struct device *d, int where, int cap)
{
  printf("Vendor Specific Information: ");
  if (!do_show_vendor_caps(d, where, cap))
    printf("Len=%02x <?>\n", BITS(cap, 0, 8));
}
