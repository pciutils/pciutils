/*
 *	The PCI Library -- Virtual Emulated Config Space Access Functions
 *
 *	Copyright (c) 2022 Pali Rohár
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "internal.h"

static u32
ioflg_to_pciflg(pciaddr_t ioflg)
{
  u32 flg = 0;

  if ((ioflg & PCI_IORESOURCE_TYPE_BITS) == PCI_IORESOURCE_IO)
    flg = PCI_BASE_ADDRESS_SPACE_IO;
  else if ((ioflg & PCI_IORESOURCE_TYPE_BITS) == PCI_IORESOURCE_MEM)
    {
      flg = PCI_BASE_ADDRESS_SPACE_MEMORY;
      if (ioflg & PCI_IORESOURCE_MEM_64)
        flg |= PCI_BASE_ADDRESS_MEM_TYPE_64;
      else
        flg |= PCI_BASE_ADDRESS_MEM_TYPE_32;
      if (ioflg & PCI_IORESOURCE_PREFETCH)
        flg |= PCI_BASE_ADDRESS_MEM_PREFETCH;
    }

  return flg;
}

static u32
baseres_to_pcires(pciaddr_t addr, pciaddr_t ioflg, int *have_sec, u32 *sec_val)
{
  u32 val = ioflg_to_pciflg(ioflg);

  if (have_sec)
    *have_sec = 0;

  if ((val & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO && addr <= 0xffffffff)
    val |= addr & PCI_BASE_ADDRESS_IO_MASK;
  else if ((val & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY)
    {
      val |= addr & PCI_BASE_ADDRESS_MEM_MASK;
      if ((val & PCI_BASE_ADDRESS_MEM_TYPE_64) && have_sec)
        {
          *have_sec = 1;
          *sec_val = addr >> 32;
        }
    }

  return val;
}

static inline u32
even_baseres_to_pcires(pciaddr_t addr, pciaddr_t ioflg)
{
  return baseres_to_pcires(addr, ioflg, NULL, NULL);
}

static inline u32
odd_baseres_to_pcires(pciaddr_t addr0, pciaddr_t ioflg0, pciaddr_t addr, pciaddr_t ioflg)
{
  int have_sec;
  u32 val;
  baseres_to_pcires(addr0, ioflg0, &have_sec, &val);
  if (!have_sec)
    val = baseres_to_pcires(addr, ioflg, NULL, NULL);
  return val;
}

int
pci_emulated_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  u32 ht = PCI_HEADER_TYPE_NORMAL;
  u32 val = 0;
  int i;

  if (pos >= 64)
    return 0;

  if (len > 4)
    return pci_generic_block_read(d, pos, buf, len);

  if (d->device_class == PCI_CLASS_BRIDGE_PCI)
    ht = PCI_HEADER_TYPE_BRIDGE;
  else if (d->device_class == PCI_CLASS_BRIDGE_CARDBUS)
    ht = PCI_HEADER_TYPE_CARDBUS;

  switch (pos & ~3)
    {
      case PCI_COMMAND:
        for (i = 0; i < 6; i++)
          {
            if (!d->size[i])
              continue;
            if ((d->flags[i] & PCI_IORESOURCE_TYPE_BITS) == PCI_IORESOURCE_IO)
              val |= PCI_COMMAND_IO;
            else if ((d->flags[i] & PCI_IORESOURCE_TYPE_BITS) == PCI_IORESOURCE_MEM)
              val |= PCI_COMMAND_MEMORY;
          }
        break;
      case PCI_VENDOR_ID:
        val = (d->device_id << 16) | d->vendor_id;
        break;
      case PCI_CLASS_REVISION:
        val = (d->device_class << 16) | (d->prog_if << 8) | d->rev_id;
        break;
      case PCI_CACHE_LINE_SIZE:
        val = ht << 16;
        break;
      case PCI_BASE_ADDRESS_0:
        val = even_baseres_to_pcires(d->base_addr[0], d->flags[0]);
        break;
      case PCI_INTERRUPT_LINE:
        val = (d->irq >= 0 && d->irq <= 0xff) ? d->irq : 0;
        break;
    }

  if ((pos & ~3) == PCI_BASE_ADDRESS_1 && (ht == PCI_HEADER_TYPE_NORMAL || ht == PCI_HEADER_TYPE_BRIDGE))
    val = odd_baseres_to_pcires(d->base_addr[0], d->flags[0], d->base_addr[1], d->flags[1]);

  if (ht == PCI_HEADER_TYPE_NORMAL)
    switch (pos & ~3)
      {
        case PCI_BASE_ADDRESS_2:
          val = even_baseres_to_pcires(d->base_addr[2], d->flags[2]);
          break;
        case PCI_BASE_ADDRESS_3:
          val = odd_baseres_to_pcires(d->base_addr[2], d->flags[2], d->base_addr[3], d->flags[3]);
          break;
        case PCI_BASE_ADDRESS_4:
          val = even_baseres_to_pcires(d->base_addr[4], d->flags[4]);
          break;
        case PCI_BASE_ADDRESS_5:
          val = odd_baseres_to_pcires(d->base_addr[4], d->flags[4], d->base_addr[5], d->flags[5]);
          break;
        case PCI_SUBSYSTEM_VENDOR_ID:
          val = (d->subsys_id << 16) | d->subsys_vendor_id;
          break;
        case PCI_ROM_ADDRESS:
          val = d->rom_base_addr & PCI_ROM_ADDRESS_MASK;
          if (val)
            val |= PCI_ROM_ADDRESS_ENABLE;
          break;
      }
  else if (ht == PCI_HEADER_TYPE_BRIDGE)
    switch (pos & ~3)
      {
        case PCI_COMMAND:
          if (d->bridge_size[0])
            val |= PCI_COMMAND_IO;
          if (d->bridge_size[1] || d->bridge_size[2])
            val |= PCI_COMMAND_MEMORY;
          break;
        case PCI_PRIMARY_BUS:
          val = d->bus;
          break;
        case PCI_IO_BASE:
          if (d->bridge_size[0])
            {
              val = (((((d->bridge_base_addr[0] + d->bridge_size[0] - 1) >> 8) & PCI_IO_RANGE_MASK) << 8) & 0xff00) |
                    (((d->bridge_base_addr[0] >> 8) & PCI_IO_RANGE_MASK) & 0x00ff);
              if ((d->bridge_flags[0] & PCI_IORESOURCE_IO_16BIT_ADDR) &&
                  d->bridge_base_addr[0] + d->bridge_size[0] - 1 <= 0xffff)
                val |= (PCI_IO_RANGE_TYPE_16 << 8) | PCI_IO_RANGE_TYPE_16;
              else
                val |= (PCI_IO_RANGE_TYPE_32 << 8) | PCI_IO_RANGE_TYPE_32;
            }
          else
            val = 0xff & PCI_IO_RANGE_MASK;
          break;
        case PCI_MEMORY_BASE:
          if (d->bridge_size[1])
            val = (((((d->bridge_base_addr[1] + d->bridge_size[1] - 1) >> 16) & PCI_MEMORY_RANGE_MASK) << 16) & 0xffff0000) |
                  (((d->bridge_base_addr[1] >> 16) & PCI_MEMORY_RANGE_MASK) & 0x0000ffff);
          else
            val = 0xffff & PCI_MEMORY_RANGE_MASK;
          break;
        case PCI_PREF_MEMORY_BASE:
          if (d->bridge_size[2])
            {
              val = (((((d->bridge_base_addr[2] + d->bridge_size[2] - 1) >> 16) & PCI_PREF_RANGE_MASK) << 16) & 0xffff0000) |
                    (((d->bridge_base_addr[2] >> 16) & PCI_PREF_RANGE_MASK) & 0x0000ffff);
              if ((d->bridge_flags[2] & PCI_IORESOURCE_MEM_64) ||
                  d->bridge_base_addr[2] + d->bridge_size[2] - 1 > 0xffffffff)
                val |= (PCI_PREF_RANGE_TYPE_64 << 16) | PCI_PREF_RANGE_TYPE_64;
              else
                val |= (PCI_PREF_RANGE_TYPE_32 << 16) | PCI_PREF_RANGE_TYPE_32;
            }
          else
            val = 0xffff & PCI_PREF_RANGE_MASK;
          break;
        case PCI_PREF_BASE_UPPER32:
          if (d->bridge_size[2])
            val = d->bridge_base_addr[2] >> 32;
          break;
        case PCI_PREF_LIMIT_UPPER32:
          if (d->bridge_size[2])
            val = (d->bridge_base_addr[2] + d->bridge_size[2] - 1) >> 32;
          break;
        case PCI_IO_BASE_UPPER16:
          if (d->bridge_size[0])
            val = ((((d->bridge_base_addr[0] + d->bridge_size[0] - 1) >> 16) << 16) & 0xffff0000) |
                  ((d->bridge_base_addr[0] >> 16) & 0x0000ffff);
          break;
        case PCI_ROM_ADDRESS1:
          val = d->rom_base_addr & PCI_ROM_ADDRESS_MASK;
          if (val)
            val |= PCI_ROM_ADDRESS_ENABLE;
          break;
      }
  else if (ht == PCI_HEADER_TYPE_CARDBUS)
    switch (pos & ~3)
      {
        case PCI_COMMAND:
          if (d->bridge_size[0] || d->bridge_size[1])
            val |= PCI_COMMAND_MEMORY;
          if (d->bridge_size[2] || d->bridge_size[3])
            val |= PCI_COMMAND_IO;
          break;
        case PCI_CB_PRIMARY_BUS:
          val = d->bus;
          break;
        case PCI_CB_MEMORY_BASE_0:
          if (d->bridge_size[0])
            val = d->bridge_base_addr[0] & ~0xfff;
          else
            val = 0xffffffff & ~0xfff;
          break;
        case PCI_CB_MEMORY_LIMIT_0:
          if (d->bridge_size[0])
            val = (d->bridge_base_addr[0] + d->bridge_size[0] - 1) & ~0xfff;
          break;
        case PCI_CB_MEMORY_BASE_1:
          if (d->bridge_size[1])
            val = d->bridge_base_addr[1] & ~0xfff;
          else
            val = 0xffffffff & ~0xfff;
          break;
        case PCI_CB_MEMORY_LIMIT_1:
          if (d->bridge_size[1])
            val = (d->bridge_base_addr[1] + d->bridge_size[1] - 1) & ~0xfff;
          break;
        case PCI_CB_IO_BASE_0:
          if (d->bridge_size[2])
            {
              val = d->bridge_base_addr[2] & PCI_CB_IO_RANGE_MASK;
              if ((d->bridge_flags[2] & PCI_IORESOURCE_IO_16BIT_ADDR) ||
                  d->bridge_base_addr[2] + d->bridge_size[2] - 1 <= 0xffff)
                val |= PCI_IO_RANGE_TYPE_16;
              else
                val |= PCI_IO_RANGE_TYPE_32;
            }
          else
            val = 0x0000ffff & PCI_CB_IO_RANGE_MASK;
          break;
        case PCI_CB_IO_LIMIT_0:
          if (d->bridge_size[2])
            val = (d->bridge_base_addr[2] + d->bridge_size[2] - 1) & PCI_CB_IO_RANGE_MASK;
          break;
        case PCI_CB_IO_BASE_1:
          if (d->bridge_size[3])
            {
              val = d->bridge_base_addr[3] & PCI_CB_IO_RANGE_MASK;
              if ((d->bridge_flags[3] & PCI_IORESOURCE_IO_16BIT_ADDR) ||
                  d->bridge_base_addr[3] + d->bridge_size[3] - 1 <= 0xffff)
                val |= PCI_IO_RANGE_TYPE_16;
              else
                val |= PCI_IO_RANGE_TYPE_32;
            }
          else
            val = 0x0000ffff & PCI_CB_IO_RANGE_MASK;
          break;
        case PCI_CB_IO_LIMIT_1:
          if (d->bridge_size[3])
            val = (d->bridge_base_addr[3] + d->bridge_size[3] - 1) & PCI_CB_IO_RANGE_MASK;
          break;
        case PCI_CB_BRIDGE_CONTROL:
          if (d->bridge_flags[0] & PCI_IORESOURCE_PREFETCH)
            val |= PCI_CB_BRIDGE_CTL_PREFETCH_MEM0;
          if (d->bridge_flags[1] & PCI_IORESOURCE_PREFETCH)
            val |= PCI_CB_BRIDGE_CTL_PREFETCH_MEM1;
          break;
        case PCI_CB_SUBSYSTEM_VENDOR_ID:
          val = (d->subsys_id << 16) | d->subsys_vendor_id;
          break;
      }

  if (len <= 2)
    val = (val >> (8 * (pos & 3))) & ((1 << (len * 8)) - 1);

  while (len-- > 0)
    {
      *(buf++) = val & 0xff;
      val >>= 8;
    }
  return 1;
}
