/*
 *	The PCI Library -- Configuration Access via AmigaOS 4.x expansion.library
 *
 *	Copyright (c) 2024 Olrick Lefebvre <olrick.lefebvre@olrick.fr>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE

#include <proto/exec.h>
#include <exec/types.h>
#include <proto/expansion.h>
#include <interfaces/expansion.h>


// have to undef PCI values to avoid redefine warning
#undef PCI_BASE_ADDRESS_MEM_MASK
#undef PCI_BASE_ADDRESS_IO_MASK
#undef PCI_ROM_ADDRESS_MASK
#include <expansion/pci.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/unistd.h>

#include "internal.h"


// custom Amiga x.y version tag
#define VERSTAG "\0$VER: pciutils " PCILIB_VERSION " (" PCILIB_DATE_AMIGAOS ") AmigaOS4 port"


/*** AmigaOS access support ***/

typedef struct _PCIAccess {
	struct ExpansionBase *expansion;
	struct PCIIFace *ipci;
} PCIAccess;

static void 
aos_close_pci_interface(struct pci_access *a)
{
	PCIAccess *pci;

	pci = (PCIAccess *)a->backend_data;
	if (pci) {
		if (pci->expansion) {
			if (pci->ipci) {
				IExec->DropInterface((struct Interface *)pci->ipci);
				pci->ipci = NULL;
			}
			IExec->CloseLibrary((struct Library *)pci->expansion);
			pci->expansion = NULL;
		}
		pci_mfree(pci);
		a->backend_data = NULL;
	}
}

static BOOL 
aos_open_pci_interface(struct pci_access *a)
{
	PCIAccess *pci;
	BOOL res = FALSE;

	if (NULL == a->backend_data) {
		pci = pci_malloc(a, sizeof(PCIAccess));
		a->backend_data = pci;
		pci->expansion = (struct ExpansionBase *)IExec->OpenLibrary("expansion.library", 0);
		if(NULL == pci->expansion) {
			a->warning("Unable to open expansion.library");
			aos_close_pci_interface(a);
		} else {
			pci->ipci = (struct PCIIFace *)IExec->GetInterface((struct Library *)pci->expansion, "pci", 1, TAG_DONE);
			if(NULL == pci->ipci) {
				a->warning("Unable to obtain pci interface");
				aos_close_pci_interface(a);			
			} else {
				res = TRUE;
			}
		}
	} else {
		res = TRUE;  // already opened
	}

	return res;
}

static int 
aos_expansion_detect(struct pci_access *a)
{
	int res = FALSE;
	struct PCIDevice *device = NULL;
	PCIAccess *pci;

	if(TRUE == aos_open_pci_interface(a)) {	
		pci = a->backend_data;

		// Try to read PCI first device
		device = pci->ipci->FindDeviceTags(FDT_Index, 0);
		if(NULL == device) {
			a->warning("AmigaOS Expansion PCI interface cannot find any device");
			aos_close_pci_interface(a);
		} else {
			pci->ipci->FreeDevice(device);
			res = TRUE;
		}
	}
	
	return res;
}

static void 
aos_expansion_init(struct pci_access *a)
{
	// to avoid flushing of version tag
	static STRPTR USED ver = (STRPTR)VERSTAG;

	if (!aos_open_pci_interface(a)) {
		a->debug("\n");
		a->error("AmigaOS Expansion PCI interface cannot be accessed.");
	}
}

static void 
aos_expansion_cleanup(struct pci_access *a)
{
	aos_close_pci_interface(a);
}

static void 
aos_expansion_scan(struct pci_access *a)
{
	struct PCIDevice *device = NULL;
	PCIAccess *pci = NULL;
	UBYTE bus_num;
	UBYTE dev_num;
	UBYTE fn_num;
	struct pci_dev *d;
	int found_devs = 0;

	pci = a->backend_data;

	// X1000 has a bug which left shifts secondary bus by one bit, so we don't scan but get all devices identified by the system
	device = pci->ipci->FindDeviceTags(FDT_Index, found_devs);
	while (device) {
		d = pci_alloc_dev(a);
		d->domain = 0; // only one domain for AmigaOS
		device->GetAddress(&bus_num, &dev_num, &fn_num);
		d->bus = bus_num;
		d->dev = dev_num;
		d->func = fn_num;
		d->backend_data = device;
		d->vendor_id = device->ReadConfigWord(PCI_VENDOR_ID);
		d->device_id = device->ReadConfigWord(PCI_DEVICE_ID);
		d->known_fields = PCI_FILL_IDENT;
		d->hdrtype = device->ReadConfigByte(PCI_HEADER_TYPE) & ~PCI_HEADER_TYPE_MULTIFUNCTION;
		pci_link_dev(a, d);
		a->debug("  Found device %02x:%02x.%d %04x:%04x\n", d->bus, d->dev, d->func, d->vendor_id, d->device_id);

		found_devs++;
		device = pci->ipci->FindDeviceTags(FDT_Index, found_devs);
	}
}

static int 
aos_expansion_read(struct pci_dev *d, int pos, byte *buf, int len)
{
	int res = FALSE;
	byte *ptr = buf;
	if (d->backend_data) {
		for (int i = 0; i < len; i++) {
			// byte by byte to avoid endianness troubles
			*ptr = ((struct PCIDevice *)(d->backend_data))->ReadConfigByte(pos + i);
			ptr++;
			res = TRUE;
		}
	}

	return res;
}

static int 
aos_expansion_write(struct pci_dev *d, int pos, byte *buf, int len)
{
	int res = FALSE;
	byte *ptr = buf;

	if (d->backend_data) {
		for (int i = 0; i < len; i++) {
			// byte by byte to avoid endianness troubles
			((struct PCIDevice *)(d->backend_data))->WriteConfigByte(pos + i, *ptr);
			ptr++;
			res = TRUE;
		}
	}

	return res;
}

static void 
aos_expansion_init_dev(struct pci_dev *d)
{
	d->backend_data = NULL; // struct PCIDevice * to be obtained
}

static void 
aos_expansion_cleanup_dev(struct pci_dev *d)
{
	PCIAccess *pci;

	if (d->backend_data && d->access->backend_data) {
		pci = d->access->backend_data;
		pci->ipci->FreeDevice((struct PCIDevice *)d->backend_data);
		d->backend_data = NULL;
	}
}

struct pci_methods pm_aos_expansion = {
	"aos-expansion",
	"The Expansion.library on AmigaOS 4.x",
	NULL,			// config, called after allocation of pci_access, if assigned
	aos_expansion_detect,	// detect, mandatory because called without check
	aos_expansion_init,	// init, called once access chosen, eventually after detect
	aos_expansion_cleanup,	// cleanup, called at the end
	aos_expansion_scan,
	pci_generic_fill_info,
	aos_expansion_read,
	aos_expansion_write,
	NULL,			// read_vpd
	aos_expansion_init_dev,
	aos_expansion_cleanup_dev,
};
