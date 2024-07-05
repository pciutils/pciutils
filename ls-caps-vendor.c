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
    default:
      tname = "<unknown>";
      break;
    }

  printf("VirtIO: %s\n", tname);

  if (verbose < 2)
    return 1;

  printf("\t\tBAR=%d offset=%08x size=%08x",
	 get_conf_byte(d, where +  4),
	 get_conf_long(d, where +  8),
	 get_conf_long(d, where + 12));

  if (type == 2 && length >= 20)
    printf(" multiplier=%08x", get_conf_long(d, where+16));

  printf("\n");
  return 1;
}

static int
show_vendor_caps_intel(struct device *d, int where, int cap)
{
  int length = BITS(cap, 0, 8);
  int version = BITS(cap, 8, 4);
  int type = BITS(cap, 12, 4);
  u32 l;

  if (type == 0)
    {
      printf("Intel Capabilities v%d\n", version);
      /*
       * Intel Capabilities is used at least on Intel Host Bridge / DRAM Controller
       * and Intel Integrated Graphics Controller. Format of the CAPID0_<X>
       * registers parsed below matches Cap Version 1 which is used since second
       * generation of the Intel Core processors (Sandy Bridge). Parsing of other
       * versions is not currently supported.
       */
      if (version != 1)
        return 1;
    }
  else if (type == 1)
    {
      printf("Intel Feature Detection\n");
      /*
       * Intel Feature Detection Capabilities is used on Intel LPC Controller.
       * Capabilities are accessed indirectly by writing indirect capability
       * register to PCI config space. Because lspci cannot write to PCI config
       * space, it is not possible to read or parse Intel Feature Vector Space.
       */
      return 1;
    }
  else
    {
      printf("Intel <unknown>\n");
      return 1;
    }

  if (!config_fetch(d, where, length))
    return 0;

  /* CAPID0_A */
  if (length >= 8)
    {
      l = get_conf_long(d, where + 4);
      printf("\t\tCapA:");
      printf(" Peg60Dis%c", FLAG(l, BIT(31)));
      printf(" Peg12Dis%c", FLAG(l, BIT(30)));
      printf(" Peg11Dis%c", FLAG(l, BIT(29)));
      printf(" Peg10Dis%c", FLAG(l, BIT(28)));
      printf(" PeLWUDis%c", FLAG(l, BIT(27)));
      printf(" DmiWidth=x%u", (l & BIT(26)) ? 2 : 4);
      printf("\n\t\t     ");
      printf(" EccDis%c", FLAG(l, BIT(25)));
      printf(" ForceEccEn%c", FLAG(l, BIT(24)));
      printf(" VTdDis%c", FLAG(l, BIT(23)));
      printf(" DmiG2Dis%c", FLAG(l, BIT(22)));
      printf(" PegG2Dis%c", FLAG(l, BIT(21)));
      printf(" DDRMaxSize=");
      if (BITS(l, 19, 2) == 0)
        printf("Unlimited");
      else
        printf("%gGB/chan", 512 * (1 << ((3-BITS(l, 19, 2))*2)) / 1024.0);
      printf("\n\t\t     ");
      printf(" 1NDis%c", FLAG(l, BIT(17)));
      printf(" CDDis%c", FLAG(l, BIT(15)));
      printf(" DDPCDis%c", FLAG(l, BIT(14)));
      printf(" X2APICEn%c", FLAG(l, BIT(13)));
      printf(" PDCDis%c", FLAG(l, BIT(12)));
      printf(" IGDis%c", FLAG(l, BIT(11)));
      printf(" CDID=%u", BITS(l, 8, 2));
      printf(" CRID=%u", BITS(l, 4, 4));
      printf("\n\t\t     ");
      printf(" DDROCCAP%c", FLAG(l, BIT(3)));
      printf(" OCEn%c", FLAG(l, BIT(2)));
      printf(" DDRWrtVrefEn%c", FLAG(l, BIT(1)));
      printf(" DDR3LEn%c", FLAG(l, BIT(0)));
      printf("\n");
    }

  /* CAPID0_B */
  if (length >= 12)
    {
      l = get_conf_long(d, where + 8);
      printf("\t\tCapB:");
      printf(" ImguDis%c", FLAG(l, BIT(31)));
      printf(" OCbySSKUCap%c", FLAG(l, BIT(30)));
      printf(" OCbySSKUEn%c", FLAG(l, BIT(29)));
      printf(" SMTCap%c", FLAG(l, BIT(28)));
      printf(" CacheSzCap 0x%x", BITS(l, 25, 3));
      printf("\n\t\t     ");
      printf(" SoftBinCap%c", FLAG(l, BIT(24)));
      printf(" DDR3MaxFreqWithRef100=");
      if (BITS(l, 21, 3) == 0)
        printf("Disabled");
      else if (BITS(l, 21, 3) == 7)
        printf("Unlimited");
      else
        printf("%uMHz", (6+BITS(l, 21, 3)) * 200);
      printf(" PegG3Dis%c", FLAG(l, BIT(20)));
      printf("\n\t\t     ");
      printf(" PkgTyp%c", FLAG(l, BIT(19)));
      printf(" AddGfxEn%c", FLAG(l, BIT(18)));
      printf(" AddGfxCap%c", FLAG(l, BIT(17)));
      printf(" PegX16Dis%c", FLAG(l, BIT(16)));
      printf(" DmiG3Dis%c", FLAG(l, BIT(15)));
      printf(" GmmDis%c", FLAG(l, BIT(8)));
      printf("\n\t\t     ");
      printf(" DDR3MaxFreq=%uMHz", (11-BITS(l, 4, 2)) * 2666 / 10);
      printf(" LPDDR3En%c", FLAG(l, BIT(2)));
      printf("\n");
    }

  /* CAPID0_C */
  if (length >= 16)
    {
      l = get_conf_long(d, where + 12);
      printf("\t\tCapC:");
      printf(" PegG4Dis%c", FLAG(l, BIT(28)));
      printf(" DDR4MaxFreq=");
      if (BITS(l, 23, 4) == 0)
        printf("Unlimited");
      else
        printf("%uMHz", BITS(l, 0, 4) * 2666 / 10);
      printf(" LPDDREn%c", FLAG(l, BIT(22)));
      printf(" LPDDR4MaxFreq=");
      if (BITS(l, 17, 4) == 0)
        printf("Unlimited");
      else
        printf("%uMHz", BITS(l, 0, 4) * 2666 / 10);
      printf(" LPDDR4En%c", FLAG(l, BIT(16)));
      printf("\n\t\t     ");
      printf(" QClkGvDis%c", FLAG(l, BIT(14)));
      printf(" SgxDis%c", FLAG(l, BIT(9)));
      printf(" BClkOC=%s", BITS(l, 7, 2) == 0 ? "Disabled" :
                           BITS(l, 7, 2) == 1 ? "115MHz" :
                           BITS(l, 7, 2) == 2 ? "130MHz" :
                                                "Unlimited");
      printf(" IddDis%c", FLAG(l, BIT(6)));
      printf(" Pipe3Dis%c", FLAG(l, BIT(5)));
      printf(" Gear1MaxFreq=");
      if (BITS(l, 0, 4) == 0)
        printf("Unlimited");
      else
        printf("%uMHz", BITS(l, 0, 4) * 2666 / 10);
      printf("\n");
    }

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
    case 0x8086: /* Intel */
      return show_vendor_caps_intel(d, where, cap);
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
