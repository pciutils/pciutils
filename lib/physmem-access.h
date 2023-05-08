/*
 *      The PCI Library -- Compiler-specific wrappers for memory mapped I/O
 *
 *      Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * FIXME
 * Unfortunately gcc does not provide architecture independent way to read from
 * or write to memory mapped I/O. The best approximation is to use volatile and
 * for the write operation follow it by the read operation from the same address.
 */

static inline void
physmem_writeb(unsigned char value, volatile void *ptr)
{
  *(volatile unsigned char *)ptr = value;
}

static inline void
physmem_writew(unsigned short value, volatile void *ptr)
{
  *(volatile unsigned short *)ptr = value;
}

static inline void
physmem_writel(u32 value, volatile void *ptr)
{
  *(volatile u32 *)ptr = value;
}

static inline unsigned char
physmem_readb(volatile void *ptr)
{
  return *(volatile unsigned char *)ptr;
}

static inline unsigned short
physmem_readw(volatile void *ptr)
{
  return *(volatile unsigned short *)ptr;
}

static inline u32
physmem_readl(volatile void *ptr)
{
  return *(volatile u32 *)ptr;
}
