/*
 *	$Id: generic.c,v 1.6 2000/04/21 11:58:00 mj Exp $
 *
 *	The PCI Library -- Generic Direct Access Functions
 *
 *	Copyright (c) 1997--2000 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>

#include "internal.h"

void
pci_generic_scan_bus(struct pci_access *a, byte *busmap, int bus)
{
  int dev, multi, ht;
  struct pci_dev *t = pci_alloc_dev(a);

  a->debug("Scanning bus %02x for devices...\n", bus);
  if (busmap[bus])
    {
      a->warning("Bus %02x seen twice (firmware bug). Ignored.", bus);
      return;
    }
  busmap[bus] = 1;
  t->bus = bus;
  for(dev=0; dev<32; dev++)
    {
      t->dev = dev;
      multi = 0;
      for(t->func=0; t->func<8; t->func++)
	{
	  u32 vd = pci_read_long(t, PCI_VENDOR_ID);
	  struct pci_dev *d;

	  if (!vd || vd == 0xffffffff)
	    break;
	  ht = pci_read_byte(t, PCI_HEADER_TYPE);
	  if (!t->func)
	    multi = ht & 0x80;
	  ht &= 0x7f;
	  d = pci_alloc_dev(a);
	  d->bus = t->bus;
	  d->dev = t->dev;
	  d->func = t->func;
	  d->vendor_id = vd & 0xffff;
	  d->device_id = vd >> 16U;
	  d->known_fields = PCI_FILL_IDENT;
	  d->hdrtype = ht;
	  pci_link_dev(a, d);
	  switch (ht)
	    {
	    case PCI_HEADER_TYPE_NORMAL:
	      break;
	    case PCI_HEADER_TYPE_BRIDGE:
	    case PCI_HEADER_TYPE_CARDBUS:
	      pci_generic_scan_bus(a, busmap, pci_read_byte(t, PCI_SECONDARY_BUS));
	      break;
	    default:
	      a->debug("Device %02x:%02x.%d has unknown header type %02x.\n", d->bus, d->dev, d->func, ht);
	    }
	  if (!multi)
	    break;
	}
    }
}

void
pci_generic_scan(struct pci_access *a)
{
  byte busmap[256];

  bzero(busmap, sizeof(busmap));
  pci_generic_scan_bus(a, busmap, 0);
}

int
pci_generic_fill_info(struct pci_dev *d, int flags)
{
  struct pci_access *a = d->access;

  if (flags & PCI_FILL_IDENT)
    {
      d->vendor_id = pci_read_word(d, PCI_VENDOR_ID);
      d->device_id = pci_read_word(d, PCI_DEVICE_ID);
    }
  if (flags & PCI_FILL_IRQ)
    d->irq = pci_read_byte(d, PCI_INTERRUPT_LINE);
  if (flags & PCI_FILL_BASES)
    {
      int cnt = 0, i;
      bzero(d->base_addr, sizeof(d->base_addr));
      switch (d->hdrtype)
	{
	case PCI_HEADER_TYPE_NORMAL:
	  cnt = 6;
	  break;
	case PCI_HEADER_TYPE_BRIDGE:
	  cnt = 2;
	  break;
	case PCI_HEADER_TYPE_CARDBUS:
	  cnt = 1;
	  break;
	}
      if (cnt)
	{
	  u16 cmd = pci_read_word(d, PCI_COMMAND);
	  for(i=0; i<cnt; i++)
	    {
	      u32 x = pci_read_long(d, PCI_BASE_ADDRESS_0 + i*4);
	      if (!x || x == (u32) ~0)
		continue;
	      d->base_addr[i] = x;
	      if (x & PCI_BASE_ADDRESS_SPACE_IO)
		{
		  if (!a->buscentric && !(cmd & PCI_COMMAND_IO))
		    d->base_addr[i] = 0;
		}
	      else if (a->buscentric || (cmd & PCI_COMMAND_MEMORY))
		{
		  if ((x & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64)
		    {
		      if (i >= cnt-1)
			a->warning("%02x:%02x.%d: Invalid 64-bit address seen.", d->bus, d->dev, d->func);
		      else
			{
			  u32 y = pci_read_long(d, PCI_BASE_ADDRESS_0 + (++i)*4);
#ifdef HAVE_64BIT_ADDRESS
			  d->base_addr[i-1] |= ((pciaddr_t) y) << 32;
#else
			  if (y)
			    {
			      a->warning("%02x:%02x.%d 64-bit device address ignored.", d->bus, d->dev, d->func);
			      d->base_addr[i-1] = 0;
			    }
#endif
			}
		    }
		}
	      else
		d->base_addr[i] = 0;
	    }
	}
    }
  if (flags & PCI_FILL_ROM_BASE)
    {
      int reg = 0;
      d->rom_base_addr = 0;
      switch (d->hdrtype)
	{
	case PCI_HEADER_TYPE_NORMAL:
	  reg = PCI_ROM_ADDRESS;
	  break;
	case PCI_HEADER_TYPE_BRIDGE:
	  reg = PCI_ROM_ADDRESS1;
	  break;
	}
      if (reg)
	{
	  u32 a = pci_read_long(d, reg);
	  if (a & PCI_ROM_ADDRESS_ENABLE)
	    d->rom_base_addr = a;
	}
    }
  return flags & ~PCI_FILL_SIZES;
}

static int
pci_generic_block_op(struct pci_dev *d, int pos, byte *buf, int len,
		 int (*r)(struct pci_dev *d, int pos, byte *buf, int len))
{
  if ((pos & 1) && len >= 1)
    {
      if (!r(d, pos, buf, 1))
	return 0;
      pos++; buf++; len--;
    }
  if ((pos & 3) && len >= 2)
    {
      if (!r(d, pos, buf, 2))
	return 0;
      pos += 2; buf += 2; len -= 2;
    }
  while (len >= 4)
    {
      if (!r(d, pos, buf, 4))
	return 0;
      pos += 4; buf += 4; len -= 4;
    }
  if (len >= 2)
    {
      if (!r(d, pos, buf, 2))
	return 0;
      pos += 2; buf += 2; len -= 2;
    }
  if (len && !r(d, pos, buf, 1))
    return 0;
  return 1;
}

int
pci_generic_block_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  return pci_generic_block_op(d, pos, buf, len, d->access->methods->read);
}

int
pci_generic_block_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  return pci_generic_block_op(d, pos, buf, len, d->access->methods->write);
}
