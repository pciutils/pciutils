/*
 *	$Id: pciutils.h,v 1.3 1998/02/09 12:32:56 mj Exp $
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
