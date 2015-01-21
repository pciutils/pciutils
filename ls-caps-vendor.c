/*
 *	The PCI Utilities -- Show Vendor-specific Capabilities
 *
 *	Copyright (c) 2014 Gerd Hoffmann <kraxel@redhat.com>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>

#include "lspci.h"

static void
show_vendor_caps_virtio(struct device *d, int where, int cap)
{
  int length = BITS(cap, 0, 8);
  int type = BITS(cap, 8, 8);
  char *tname;

  if (length < 16)
    return;
  if (!config_fetch(d, where, length))
    return;

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
    default:
      tname = "<unknown>";
      break;
    }

  printf("VirtIO: %s\n", tname);

  if (verbose < 2)
    return;

  printf("\t\tBAR=%d offset=%08x size=%08x\n",
	 get_conf_byte(d, where +  4),
	 get_conf_long(d, where +  8),
	 get_conf_long(d, where + 12));

  if (type != 2 || length < 20)
    return;

  printf("\t\tmultiplier=%08x\n",
	 get_conf_long(d, where+16));
}

void
show_vendor_caps(struct device *d, int where, int cap)
{
  switch (get_conf_word(d, PCI_VENDOR_ID))
    {
    case 0x1af4: /* Red Hat */
      if (get_conf_word(d, PCI_DEVICE_ID) >= 0x1000 &&
	  get_conf_word(d, PCI_DEVICE_ID) <= 0x107f)
	show_vendor_caps_virtio(d, where, cap);
      break;
    default:
      printf("Vendor Specific Information: Len=%02x <?>\n", BITS(cap, 0, 8));
      break;
    }
}
