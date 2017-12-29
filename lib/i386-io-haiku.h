/*
 *	The PCI Library -- Access to i386 I/O ports on Haiku
 *
 *	Copyright (c) 2009 Francois Revol <revol@free.fr>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <Drivers.h>
#include <ISA.h>
#include <PCI.h>

/* from haiku/trunk/headers/private/drivers/poke.h */

#define POKE_DEVICE_NAME                "poke"
#define POKE_DEVICE_FULLNAME    "/dev/misc/poke"
#define POKE_SIGNATURE                  'wltp'  // "We Like To Poke"

enum {
	POKE_PORT_READ = B_DEVICE_OP_CODES_END + 1,
	POKE_PORT_WRITE,
	POKE_PORT_INDEXED_READ,
	POKE_PORT_INDEXED_WRITE,
	POKE_PCI_READ_CONFIG,
	POKE_PCI_WRITE_CONFIG,
	POKE_GET_NTH_PCI_INFO,
	POKE_GET_PHYSICAL_ADDRESS,
	POKE_MAP_MEMORY,
	POKE_UNMAP_MEMORY
};


typedef struct {
	uint32		signature;
	uint8		index;
	pci_info*	info;
	status_t	status;
} pci_info_args;


typedef struct {
	uint32	signature;
	uint16	port;
	uint8	size;		// == index for POKE_PORT_INDEXED_*
	uint32	value;
} port_io_args;


typedef struct {
	uint32	signature;
	uint8	bus;
	uint8	device;
	uint8	function;
	uint8	size;
	uint8	offset;
	uint32	value;
} pci_io_args;


/* en poke.h*/

static int poke_driver_fd;

static int
intel_setup_io(struct pci_access *a UNUSED)
{
  poke_driver_fd = open(POKE_DEVICE_FULLNAME, O_RDWR);
  return (poke_driver_fd < 0) ? 0 : 1;
}

static inline int
intel_cleanup_io(struct pci_access *a UNUSED)
{
  close(poke_driver_fd);
  return 1;
}

static inline u8
inb (u16 port)
{
  port_io_args args = { POKE_SIGNATURE, port, sizeof(u8), 0 };
  if (ioctl(poke_driver_fd, POKE_PORT_READ, &args, sizeof(args)) < 0)
    return 0;
  return (u8)args.value;
}

static inline u16
inw (u16 port)
{
  port_io_args args = { POKE_SIGNATURE, port, sizeof(u16), 0 };
  if (ioctl(poke_driver_fd, POKE_PORT_READ, &args, sizeof(args)) < 0)
    return 0;
  return (u16)args.value;
}

static inline u32
inl (u16 port)
{
  port_io_args args = { POKE_SIGNATURE, port, sizeof(u32), 0 };
  if (ioctl(poke_driver_fd, POKE_PORT_READ, &args, sizeof(args)) < 0)
    return 0;
  return (u32)args.value;
}

static inline void
outb (u8 value, u16 port)
{
  port_io_args args = { POKE_SIGNATURE, port, sizeof(u8), value };
  ioctl(poke_driver_fd, POKE_PORT_WRITE, &args, sizeof(args));
}

static inline void
outw (u16 value, u16 port)
{
  port_io_args args = { POKE_SIGNATURE, port, sizeof(u16), value };
  ioctl(poke_driver_fd, POKE_PORT_WRITE, &args, sizeof(args));
}

static inline void
outl (u32 value, u16 port)
{
  port_io_args args = { POKE_SIGNATURE, port, sizeof(u32), value };
  ioctl(poke_driver_fd, POKE_PORT_WRITE, &args, sizeof(args));
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
