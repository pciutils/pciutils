/*
 *	$Id: pci.h,v 1.8 2000/04/21 11:58:00 mj Exp $
 *
 *	The PCI Library
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _PCI_LIB_H
#define _PCI_LIB_H

#include "config.h"

#ifdef HAVE_OWN_HEADER_H
#include "header.h"
#else
#include <linux/pci.h>
#endif

/*
 *	Types
 */

#ifdef OS_LINUX
#include <linux/types.h>

typedef __u8 byte;
typedef __u8 u8;
typedef __u16 word;
typedef __u16 u16;
typedef __u32 u32;
#endif

#ifdef OS_FREEBSD
#include <sys/types.h>

typedef u_int8_t byte;
typedef u_int8_t u8;
typedef u_int16_t word;
typedef u_int16_t u16;
typedef u_int32_t u32;
#endif

#ifdef OS_AIX
#include <sys/param.h>

typedef u_int8_t byte;
typedef u_int8_t u8;
typedef u_int16_t word;
typedef u_int16_t u16;
typedef u_int32_t u32;
#endif

#ifdef HAVE_LONG_ADDRESS
typedef unsigned long long pciaddr_t;
#else
typedef unsigned long pciaddr_t;
#endif

/*
 *	PCI Access Structure
 */

struct pci_methods;
struct nl_entry;

#define PCI_ACCESS_AUTO			0	/* Autodetection (params: none) */
#define PCI_ACCESS_PROC_BUS_PCI		1	/* Linux /proc/bus/pci (params: path) */
#define PCI_ACCESS_SYSCALLS		2	/* pciconfig_read() syscalls (params: none) */
#define PCI_ACCESS_I386_TYPE1		3	/* i386 ports, type 1 (params: none) */
#define PCI_ACCESS_I386_TYPE2		4	/* i386 ports, type 2 (params: none) */
#define PCI_ACCESS_FBSD_DEVICE		5	/* FreeBSD /dev/pci (params: path) */
#define PCI_ACCESS_AIX_DEVICE		6	/* /dev/pci0, /dev/bus0, etc. */
#define PCI_ACCESS_DUMP			7	/* Dump file (params: filename) */
#define PCI_ACCESS_MAX			8

struct pci_access {
  /* Options you can change: */
  unsigned int method;			/* Access method */
  char *method_params[PCI_ACCESS_MAX];	/* Parameters for the methods */
  int writeable;			/* Open in read/write mode */
  int buscentric;			/* Bus-centric view of the world */
  char *id_file_name;			/* Name of ID list file */
  int numeric_ids;			/* Don't resolve device IDs to names */
  int debugging;			/* Turn on debugging messages */

  /* Functions you can override: */
  void (*error)(char *msg, ...);	/* Write error message and quit */
  void (*warning)(char *msg, ...);	/* Write a warning message */
  void (*debug)(char *msg, ...);	/* Write a debugging message */

  struct pci_dev *devices;		/* Devices found on this bus */

  /* Fields used internally: */
  struct pci_methods *methods;
  char *nl_list;			/* Name list cache */
  struct nl_entry **nl_hash;
  int fd;				/* proc: fd */
  int fd_rw;				/* proc: fd opened read-write */
  struct pci_dev *cached_dev;		/* proc: device the fd is for */
  int fd_pos;				/* proc: current position */
};

/* Initialize PCI access */
struct pci_access *pci_alloc(void);
void pci_init(struct pci_access *);
void pci_cleanup(struct pci_access *);

/* Scanning of devices */
void pci_scan_bus(struct pci_access *acc);
struct pci_dev *pci_get_dev(struct pci_access *acc, int bus, int dev, int func); /* Raw access to specified device */
void pci_free_dev(struct pci_dev *);

/*
 *	Devices
 */

struct pci_dev {
  struct pci_dev *next;			/* Next device in the chain */
  word bus;				/* Higher byte can select host bridges */
  byte dev, func;			/* Device and function */

  /* These fields are set by pci_fill_info() */
  int known_fields;			/* Set of info fields already known */
  word vendor_id, device_id;		/* Identity of the device */
  int irq;				/* IRQ number */
  pciaddr_t base_addr[6];		/* Base addresses */
  pciaddr_t size[6];			/* Region sizes */
  pciaddr_t rom_base_addr;		/* Expansion ROM base address */
  pciaddr_t rom_size;			/* Expansion ROM size */

  /* Fields used internally: */
  struct pci_access *access;
  struct pci_methods *methods;
  byte *cache;				/* Cached information */
  int cache_len;
  int hdrtype;				/* Direct methods: header type */
  void *aux;				/* Auxillary data */
};

#define PCI_ADDR_IO_MASK (~(pciaddr_t) 0x3)
#define PCI_ADDR_MEM_MASK (~(pciaddr_t) 0xf)

byte pci_read_byte(struct pci_dev *, int pos); /* Access to configuration space */
word pci_read_word(struct pci_dev *, int pos);
u32  pci_read_long(struct pci_dev *, int pos);
int pci_read_block(struct pci_dev *, int pos, byte *buf, int len);
int pci_write_byte(struct pci_dev *, int pos, byte data);
int pci_write_word(struct pci_dev *, int pos, word data);
int pci_write_long(struct pci_dev *, int pos, u32 data);
int pci_write_block(struct pci_dev *, int pos, byte *buf, int len);

int pci_fill_info(struct pci_dev *, int flags); /* Fill in device information */

#define PCI_FILL_IDENT		1
#define PCI_FILL_IRQ		2
#define PCI_FILL_BASES		4
#define PCI_FILL_ROM_BASE	8
#define PCI_FILL_SIZES		16
#define PCI_FILL_RESCAN		0x10000

void pci_setup_cache(struct pci_dev *, byte *cache, int len);

/*
 *	Filters
 */

struct pci_filter {
  int bus, slot, func;			/* -1 = ANY */
  int vendor, device;
};

void pci_filter_init(struct pci_access *, struct pci_filter *);
char *pci_filter_parse_slot(struct pci_filter *, char *);
char *pci_filter_parse_id(struct pci_filter *, char *);
int pci_filter_match(struct pci_filter *, struct pci_dev *);

/*
 *	Device names
 */

char *pci_lookup_name(struct pci_access *a, char *buf, int size, int flags, u32 arg1, u32 arg2, u32 arg3, u32 arg4);
void pci_free_name_list(struct pci_access *a);

#define PCI_LOOKUP_VENDOR 1
#define PCI_LOOKUP_DEVICE 2
#define PCI_LOOKUP_CLASS 4
#define PCI_LOOKUP_SUBSYSTEM 8
#define PCI_LOOKUP_PROGIF 16
#define PCI_LOOKUP_NUMERIC 0x10000

#endif
