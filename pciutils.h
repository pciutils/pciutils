/*
 *	$Id: pciutils.h,v 1.6 1998/06/12 09:48:36 mj Exp $
 *
 *	Linux PCI Utilities -- Declarations
 *
 *	Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <linux/types.h>

#ifdef KERNEL_PCI_H
#include <linux/pci.h>
#else
#include "pci.h"
#endif

#define PCIUTILS_VERSION "1.06"

#define PROC_BUS_PCI "/proc/bus/pci"
#define ETC_PCI_IDS "/etc/pci.ids"

/* Types */

typedef __u8 byte;
typedef __u16 word;
typedef __u32 u32;

/* lspci.c */

void *xmalloc(unsigned int);

/* names.c */

extern int show_numeric_ids;
extern char *pci_ids;

char *lookup_vendor(word);
char *lookup_device(word, word);
char *lookup_device_full(word, word);
char *lookup_class(word);

/* filter.c */

struct pci_filter {
  int bus, slot, func;			/* -1 = ANY */
  int vendor, device;
};

void filter_init(struct pci_filter *);
char *filter_parse_slot(struct pci_filter *, char *);
char *filter_parse_id(struct pci_filter *, char *);
int filter_match(struct pci_filter *, byte bus, byte devfn, word vendid, word devid);
