/*
 *	The PCI Library
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _PCI_LIB_H
#define _PCI_LIB_H

#include "config.h"
#include "header.h"
#include "types.h"

#define PCI_LIB_VERSION 0x020204	/* FIXME: Update */

/*
 *	PCI Access Structure
 */

struct pci_methods;

enum pci_access_type {
  /* Known access methods, remember to update access.c as well */
  PCI_ACCESS_AUTO,			/* Autodetection (params: none) */
  PCI_ACCESS_SYS_BUS_PCI,		/* Linux /sys/bus/pci (params: path) */
  PCI_ACCESS_PROC_BUS_PCI,		/* Linux /proc/bus/pci (params: path) */
  PCI_ACCESS_I386_TYPE1,		/* i386 ports, type 1 (params: none) */
  PCI_ACCESS_I386_TYPE2,		/* i386 ports, type 2 (params: none) */
  PCI_ACCESS_FBSD_DEVICE,		/* FreeBSD /dev/pci (params: path) */
  PCI_ACCESS_AIX_DEVICE,		/* /dev/pci0, /dev/bus0, etc. */
  PCI_ACCESS_NBSD_LIBPCI,		/* NetBSD libpci */
  PCI_ACCESS_OBSD_DEVICE,		/* OpenBSD /dev/pci */
  PCI_ACCESS_DUMP,			/* Dump file (params: filename) */
  PCI_ACCESS_MAX
};

struct pci_access {
  /* Options you can change: */
  unsigned int method;			/* Access method */
  char *method_params[PCI_ACCESS_MAX];	/* Parameters for the methods */
  int writeable;			/* Open in read/write mode */
  int buscentric;			/* Bus-centric view of the world */

  char *id_file_name;			/* Name of ID list file (use pci_set_name_list_path()) */
  int free_id_name;			/* Set if id_file_name is malloced */
  int numeric_ids;			/* Enforce PCI_LOOKUP_NUMERIC (>1 => PCI_LOOKUP_MIXED) */

  unsigned int id_lookup_mode;		/* pci_lookup_mode flags which are set automatically */
  					/* Default: PCI_LOOKUP_CACHE */
  char *id_domain;			/* DNS domain used for the lookups (use pci_set_net_domain()) */
  int free_id_domain;			/* Set if id_domain is malloced */
  char *id_cache_file;			/* Name of the ID cache file (use pci_set_net_cache()) */
  int free_id_cache_file;		/* Set if id_cache_file is malloced */

  int debugging;			/* Turn on debugging messages */

  /* Functions you can override: */
  void (*error)(char *msg, ...);	/* Write error message and quit */
  void (*warning)(char *msg, ...);	/* Write a warning message */
  void (*debug)(char *msg, ...);	/* Write a debugging message */

  struct pci_dev *devices;		/* Devices found on this bus */

  /* Fields used internally: */
  struct pci_methods *methods;
  struct id_entry **id_hash;		/* names.c */
  struct id_bucket *current_id_bucket;
  int id_load_failed;
  int id_cache_status;			/* 0=not read, 1=read, 2=dirty */
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
struct pci_dev *pci_get_dev(struct pci_access *acc, int domain, int bus, int dev, int func); /* Raw access to specified device */
void pci_free_dev(struct pci_dev *);

/*
 *	Devices
 */

struct pci_dev {
  struct pci_dev *next;			/* Next device in the chain */
  u16 domain;				/* PCI domain (host bridge) */
  u8 bus, dev, func;			/* Bus inside domain, device and function */

  /* These fields are set by pci_fill_info() */
  int known_fields;			/* Set of info fields already known */
  u16 vendor_id, device_id;		/* Identity of the device */
  u16 device_class;			/* PCI device class */
  int irq;				/* IRQ number */
  pciaddr_t base_addr[6];		/* Base addresses */
  pciaddr_t size[6];			/* Region sizes */
  pciaddr_t rom_base_addr;		/* Expansion ROM base address */
  pciaddr_t rom_size;			/* Expansion ROM size */

  /* Fields used internally: */
  struct pci_access *access;
  struct pci_methods *methods;
  u8 *cache;				/* Cached config registers */
  int cache_len;
  int hdrtype;				/* Cached low 7 bits of header type, -1 if unknown */
  void *aux;				/* Auxillary data */
};

#define PCI_ADDR_IO_MASK (~(pciaddr_t) 0x3)
#define PCI_ADDR_MEM_MASK (~(pciaddr_t) 0xf)

u8 pci_read_byte(struct pci_dev *, int pos); /* Access to configuration space */
u16 pci_read_word(struct pci_dev *, int pos);
u32  pci_read_long(struct pci_dev *, int pos);
int pci_read_block(struct pci_dev *, int pos, u8 *buf, int len);
int pci_write_byte(struct pci_dev *, int pos, u8 data);
int pci_write_word(struct pci_dev *, int pos, u16 data);
int pci_write_long(struct pci_dev *, int pos, u32 data);
int pci_write_block(struct pci_dev *, int pos, u8 *buf, int len);

int pci_fill_info(struct pci_dev *, int flags); /* Fill in device information */

#define PCI_FILL_IDENT		1
#define PCI_FILL_IRQ		2
#define PCI_FILL_BASES		4
#define PCI_FILL_ROM_BASE	8
#define PCI_FILL_SIZES		16
#define PCI_FILL_CLASS		32
#define PCI_FILL_RESCAN		0x10000

void pci_setup_cache(struct pci_dev *, u8 *cache, int len);

/*
 *	Filters
 */

struct pci_filter {
  int domain, bus, slot, func;			/* -1 = ANY */
  int vendor, device;
};

void pci_filter_init(struct pci_access *, struct pci_filter *);
char *pci_filter_parse_slot(struct pci_filter *, char *);
char *pci_filter_parse_id(struct pci_filter *, char *);
int pci_filter_match(struct pci_filter *, struct pci_dev *);

/*
 *	Conversion of PCI ID's to names (according to the pci.ids file)
 *
 *	Call pci_lookup_name() to identify different types of ID's:
 *
 *	VENDOR				(vendorID) -> vendor
 *	DEVICE				(vendorID, deviceID) -> device
 *	VENDOR | DEVICE			(vendorID, deviceID) -> combined vendor and device
 *	SUBSYSTEM | VENDOR		(subvendorID) -> subsystem vendor
 *	SUBSYSTEM | DEVICE		(vendorID, deviceID, subvendorID, subdevID) -> subsystem device
 *	SUBSYSTEM | VENDOR | DEVICE	(vendorID, deviceID, subvendorID, subdevID) -> combined subsystem v+d
 *	SUBSYSTEM | ...			(-1, -1, subvendorID, subdevID) -> generic subsystem
 *	CLASS				(classID) -> class
 *	PROGIF				(classID, progif) -> programming interface
 */

char *pci_lookup_name(struct pci_access *a, char *buf, int size, int flags, ...);

int pci_load_name_list(struct pci_access *a);	/* Called automatically by pci_lookup_*() when needed; returns success */
void pci_free_name_list(struct pci_access *a);	/* Called automatically by pci_cleanup() */
void pci_set_name_list_path(struct pci_access *a, char *name, int to_be_freed);
void pci_set_net_domain(struct pci_access *a, char *name, int to_be_freed);
void pci_set_id_cache(struct pci_access *a, char *name, int to_be_freed);
void pci_id_cache_flush(struct pci_access *a);

enum pci_lookup_mode {
  PCI_LOOKUP_VENDOR = 1,		/* Vendor name (args: vendorID) */
  PCI_LOOKUP_DEVICE = 2,		/* Device name (args: vendorID, deviceID) */
  PCI_LOOKUP_CLASS = 4,			/* Device class (args: classID) */
  PCI_LOOKUP_SUBSYSTEM = 8,
  PCI_LOOKUP_PROGIF = 16,		/* Programming interface (args: classID, prog_if) */
  PCI_LOOKUP_NUMERIC = 0x10000,		/* Want only formatted numbers; default if access->numeric_ids is set */
  PCI_LOOKUP_NO_NUMBERS = 0x20000,	/* Return NULL if not found in the database; default is to print numerically */
  PCI_LOOKUP_MIXED = 0x40000,		/* Include both numbers and names */
  PCI_LOOKUP_NETWORK = 0x80000,		/* Try to resolve unknown ID's by DNS */
  PCI_LOOKUP_SKIP_LOCAL = 0x100000,	/* Do not consult local database */
  PCI_LOOKUP_CACHE = 0x200000,		/* Consult the local cache before using DNS */
  PCI_LOOKUP_REFRESH_CACHE = 0x400000,	/* Forget all previously cached entries, but still allow updating the cache */
};

#endif
