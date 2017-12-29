/*
 *	The PCI Library -- Access to i386 I/O ports on DJGPP
 *
 *	Copyright (c) 2010, 2017 Rudolf Marek <r.marek@assembler.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <pc.h>
#include <dos.h>
#define outb(x,y) outportb(y, x)
#define outw(x,y) outportw(y, x)
#define outl(x,y) outportl(y, x)

#define inb  inportb
#define inw  inportw
#define inl  inportl

static int irq_enabled;

static int
intel_setup_io(struct pci_access *a UNUSED)
{
  return 1;
}

static inline int
intel_cleanup_io(struct pci_access *a UNUSED)
{
  return 1;
}

static inline void intel_io_lock(void)
{
  asm volatile("" : : : "memory");
  irq_enabled = disable();
}

static inline void intel_io_unlock(void)
{
  asm volatile("" : : : "memory");
  if (irq_enabled) {
    enable();
  }
}
