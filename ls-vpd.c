/*
 *	The PCI Utilities -- Show Vital Product Data
 *
 *	Copyright (c) 2008 Ben Hutchings <bhutchings@solarflare.com>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>

#include "lspci.h"

static void
print_vpd_string(const byte *buf, word len)
{
  while (len--)
    {
      byte ch = *buf++;
      if (ch == '\\')
        printf("\\\\");
      else if (ch < 32 || ch == 127)
        printf("\\x%02x", ch);
      else
        putchar(ch);
    }
}

static int
read_vpd(struct device *d, int pos, byte *buf, int len, byte *csum)
{
  if (!pci_read_vpd(d->dev, pos, buf, len))
    return 0;
  while (len--)
    *csum += *buf++;
  return 1;
}

void
cap_vpd(struct device *d)
{
  word res_addr = 0, res_len, part_pos, part_len;
  byte key[2], buf[256];
  byte tag;
  byte csum = 0;

  printf("Vital Product Data\n");
  if (verbose < 2)
    return;

  while (res_addr <= PCI_VPD_ADDR_MASK)
    {
      if (!read_vpd(d, res_addr, &tag, 1, &csum))
	break;
      if (tag & 0x80)
	{
	  if (res_addr > PCI_VPD_ADDR_MASK + 1 - 3)
	    break;
	  if (!read_vpd(d, res_addr + 1, buf, 2, &csum))
	    break;
	  res_len = buf[0] + (buf[1] << 8);
	  res_addr += 3;
	}
      else
	{
	  res_len = tag & 7;
	  tag >>= 3;
	  res_addr += 1;
	}
      if (res_len > PCI_VPD_ADDR_MASK + 1 - res_addr)
	break;

      part_pos = 0;

      switch (tag)
	{
	case 0x0f:
	  printf("\t\tEnd\n");
	  return;

	case 0x82:
	  printf("\t\tProduct Name: ");
	  while (part_pos < res_len)
	    {
	      part_len = res_len - part_pos;
	      if (part_len > sizeof(buf))
		part_len = sizeof(buf);
	      if (!read_vpd(d, res_addr + part_pos, buf, part_len, &csum))
		break;
	      print_vpd_string(buf, part_len);
	      part_pos += part_len;
	    }
	  printf("\n");
	  break;

	case 0x90:
	case 0x91:
	  printf("\t\t%s fields:\n",
		 (tag == 0x90) ? "Read-only" : "Read/write");

	  while (part_pos + 3 <= res_len)
	    {
	      word read_len;

	      if (!read_vpd(d, res_addr + part_pos, buf, 3, &csum))
		break;
	      part_pos += 3;
	      key[0] = buf[0];
	      key[1] = buf[1];
	      part_len = buf[2];
	      if (part_len > res_len - part_pos)
		break;

	      /* Only read the first byte of the RV field because the
	       * remaining bytes are not included in the checksum. */
	      read_len = (key[0] == 'R' && key[1] == 'V') ? 1 : part_len;
	      if (!read_vpd(d, res_addr + part_pos, buf, read_len, &csum))
		break;

	      if ((key[0] == 'E' && key[1] == 'C') ||
		  (key[0] == 'P' && key[1] == 'N') ||
		  (key[0] == 'S' && key[1] == 'N') ||
		  key[0] == 'V' ||
		  key[0] == 'Y')
		{
		  /* Alphanumeric content */
		  printf("\t\t\t%c%c: ", key[0], key[1]);
		  print_vpd_string(buf, part_len);
		  printf("\n");
		}
	      else if (key[0] == 'R' && key[1] == 'V')
		{
		  /* Reserved and checksum */
		  printf("\t\t\tRV: checksum %s, %d byte(s) reserved\n",
			 csum ? "bad" : "good", part_len - 1);
		}
	      else if (key[0] == 'R' && key[1] == 'W')
		{
		  /* Read-write area */
		  printf("\t\t\tRW: %d byte(s) free\n", part_len);
		}
	      else
		{
		  /* Binary or unknown content */
		  int i;
		  printf("\t\t\t%c%c:", key[0], key[1]);
		  for (i = 0; i < part_len; i++)
		    printf(" %02x", buf[i]);
		  printf("\n");
		}

	      part_pos += part_len;
	    }
	  break;

	default:
	  printf("\t\tUnknown %s resource type %02x\n",
		 (tag & 0x80) ? "large" : "small", tag & ~0x80);
	  break;
	}

      res_addr += res_len;
    }

  if (res_addr == 0)
    printf("\t\tNot readable\n");
  else
    printf("\t\tNo end tag found\n");
}
