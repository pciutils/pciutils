/*
 *	$Id: pciutils.h,v 1.2 1998/01/27 11:50:13 mj Exp $
 *
 *	Linux PCI Utilities -- Declarations
 *
 *	Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <linux/types.h>

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
