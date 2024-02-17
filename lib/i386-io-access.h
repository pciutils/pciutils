/*
 *	The PCI Library -- Compiler-specific wrappers around x86 I/O port access instructions
 *
 *	Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#if defined(__GNUC__)

static inline unsigned char
intel_inb(unsigned short int port)
{
  unsigned char value;
  asm volatile ("inb %w1, %0" : "=a" (value) : "Nd" (port));
  return value;
}

static inline unsigned short int
intel_inw(unsigned short int port)
{
  unsigned short value;
  asm volatile ("inw %w1, %0" : "=a" (value) : "Nd" (port));
  return value;
}

static inline unsigned int
intel_inl(unsigned short int port)
{
  u32 value;
  asm volatile ("inl %w1, %0" : "=a" (value) : "Nd" (port));
  return value;
}

static inline void
intel_outb(unsigned char value, unsigned short int port)
{
  asm volatile ("outb %b0, %w1" : : "a" (value), "Nd" (port));
}

static inline void
intel_outw(unsigned short int value, unsigned short int port)
{
  asm volatile ("outw %w0, %w1" : : "a" (value), "Nd" (port));
}

static inline void
intel_outl(u32 value, unsigned short int port)
{
  asm volatile ("outl %0, %w1" : : "a" (value), "Nd" (port));
}

#elif defined(_MSC_VER)

#pragma intrinsic(_outp)
#pragma intrinsic(_outpw)
#pragma intrinsic(_outpd)
#pragma intrinsic(_inp)
#pragma intrinsic(_inpw)
#pragma intrinsic(_inpd)

#define intel_outb(x, y) _outp(y, x)
#define intel_outw(x, y) _outpw(y, x)
#define intel_outl(x, y) _outpd(y, x)
#define intel_inb(x) _inp(x)
#define intel_inw(x) _inpw(x)
#define intel_inl(x) _inpd(x)

#else

#error Do not know how to access I/O ports on this compiler

#endif
