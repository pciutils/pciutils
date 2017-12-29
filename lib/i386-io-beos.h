/*
 *	The PCI Library -- Access to i386 I/O ports on BeOS
 *
 *	Copyright (c) 2009 Francois Revol <revol@free.fr>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/* those are private syscalls */
extern int read_isa_io(int pci_bus, void *addr, int size);
extern int write_isa_io(int pci_bus, void *addr, int size, u32 value);

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

static inline u8
inb (u16 port)
{
  return (u8)read_isa_io(0, (void *)(u32)port, sizeof(u8));
}

static inline u16
inw (u16 port)
{
  return (u16)read_isa_io(0, (void *)(u32)port, sizeof(u16));
}

static inline u32
inl (u16 port)
{
  return (u32)read_isa_io(0, (void *)(u32)port, sizeof(u32));
}

static inline void
outb (u8 value, u16 port)
{
  write_isa_io(0, (void *)(u32)port, sizeof(value), value);
}

static inline void
outw (u16 value, u16 port)
{
  write_isa_io(0, (void *)(u32)port, sizeof(value), value);
}

static inline void
outl (u32 value, u16 port)
{
  write_isa_io(0, (void *)(u32)port, sizeof(value), value);
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
