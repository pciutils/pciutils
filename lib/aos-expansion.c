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

#if defined KDEBUG
	#if defined __AMIGAOS4__
		#include <proto/exec.h>
	#endif
#endif
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
#define VERSTAG "\0$VER: lspci 3.10 (02.01.2024) AmigaOS4 port"


// Debug macros
/////////////////////////////////////////////////////////////////////////

#define __PRIV_log(lvl, fmt, ...) fprintf(stderr, "[" lvl "] (%s:%d: errno: %s) " fmt "\n", __FILE__, __LINE__, clean_errno(), __VA_ARGS__)

#ifndef DEBUG
	#define dbg_print(M, ...)
#else
	#if defined KDEBUG && defined __AMIGAOS4__
		#define __PRIV_debug(fmt, ...) IExec->DebugPrintF("DEBUG %s:%d: "fmt"\n", __FILE__, __LINE__, __VA_ARGS__)
		#undef __PRIV_log
		#define __PRIV_log(lvl, fmt, ...) IExec->DebugPrintF("[" lvl "] (%s:%d: errno: %s) " fmt "\n", __FILE__, __LINE__, clean_errno(), __VA_ARGS__)
	#else
		#define __PRIV_debug(fmt, ...) fprintf(stderr, "DEBUG %s:%d: "fmt"\n", __FILE__, __LINE__, __VA_ARGS__)
	#endif
	#define dbg_print(...) __PRIV_debug(__VA_ARGS__, 0)
#endif


#define clean_errno() (errno == 0 ? "None" : strerror(errno))

#define log_err(...) __PRIV_log("ERROR", __VA_ARGS__, 0)
#define log_warn(...) __PRIV_log("WARN", __VA_ARGS__, 0)
#define log_info(...) __PRIV_log("INFO", __VA_ARGS__, 0)

#define check(A, ...) if(!(A)) { log_err(__VA_ARGS__); errno=0; goto on_error; }
#define sentinel(...)  { log_err(__VA_ARGS__); errno=0; goto on_error; }
#define check_mem(A) check((A), "Out of memory.")
#define check_debug(A, ...) if(!(A)) { debug(__VA_ARGS__); errno=0; goto on_error; }


// AmigaOS access support
/////////////////////////////////////////////////////////////////////////

#define PCIACCESS_TAG 0xc0ffeeee
typedef struct _PCIAccess {
	ULONG tag; // 0xc0ffeeee
	struct ExpansionBase *expansion;
	struct PCIIFace *ipci;
} PCIAccess;


static void aos_close_pci_interface(struct pci_access *a)
{
	PCIAccess *pci;

	if(NULL != a)
	{
		pci = (PCIAccess *)a->backend_data;
		if(NULL != pci)
		{
			if(NULL != pci->expansion)
			{
				if(NULL != pci->ipci)
				{
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
}


static BOOL aos_open_pci_interface(struct pci_access *a)
{
	PCIAccess *pci;

	check(NULL != a, "Null pci_access");

	if(NULL != a->backend_data)
	{   
		pci = (PCIAccess *)(a->backend_data);
		check(PCIACCESS_TAG == pci->tag, "pci_access.backend_data already used by alien code");
	}
	else
	{
		pci	= pci_malloc(a, sizeof(PCIAccess));
		a->backend_data = pci;
		pci->tag = PCIACCESS_TAG;
		pci->expansion = (struct ExpansionBase *)IExec->OpenLibrary("expansion.library", 0);
		check(NULL != pci->expansion, "Unable to open expansion.library");

		pci->ipci = (struct PCIIFace *)IExec->GetInterface((struct Library *)pci->expansion, "pci", 1, TAG_DONE);
		check(NULL != pci->ipci, "Unable to obtain pci interface");
	}

	return TRUE;


on_error:
	aos_close_pci_interface(a);
	return FALSE;
}


static int aos_expansion_detect(struct pci_access *a)
{
	struct PCIDevice *device = NULL;
	PCIAccess *pci;

	check(TRUE == aos_open_pci_interface(a), "AmigaOS Expansion PCI interface cannot be accessed.");
	pci = a->backend_data;

	// Try to read PCI first device
	device = pci->ipci->FindDeviceTags(FDT_Index, 0);
	check(NULL != device, "AmigaOS Expansion PCI interface cannot find any device");
	
	pci->ipci->FreeDevice(device);
	
	return TRUE;

on_error:
	aos_close_pci_interface(a);
	return FALSE;
}


static void aos_expansion_init(struct pci_access *a)
{
	// to avoid flushing of version tag
	static STRPTR USED ver = (STRPTR)VERSTAG;

	if (!aos_open_pci_interface(a))
	{
		a->debug("\n");
		a->error("AmigaOS Expansion PCI interface cannot be accessed.");
	}
}


static void aos_expansion_cleanup(struct pci_access *a)
{
	aos_close_pci_interface(a);
}

/*
#define BYTE_LEN 1
#define WORD_LEN 2
#define LONG_LEN 4
*/

static void aos_expansion_scan(struct pci_access *a)
{
	struct PCIDevice *device = NULL;
	PCIAccess *pci = NULL;
	UBYTE bus_num;
	UBYTE dev_num;
	UBYTE fn_num;
	struct pci_dev *d;
	int found_devs = 0;

	check(NULL != a, "Null pci_access");
	pci = a->backend_data;
	check((NULL != pci) && (PCIACCESS_TAG == pci->tag), "PCIData struct not available");

	// X1000 has a bug which left shifts secondary bus by one bit, so we don't scan but get all devices identified by the system
	device = pci->ipci->FindDeviceTags(FDT_Index, found_devs);
	while(NULL != device)
	{
		d = pci_alloc_dev(a);
		check_mem(d);
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

on_error:
	if((NULL != device) && (NULL != pci))
	{
		pci->ipci->FreeDevice(device);
	}

	return;
}


static int aos_expansion_read(struct pci_dev *d, int pos, byte *buf, int len)
{
	byte *ptr = buf;
	int i;

	check(NULL != d, "Null pci_dev");
	if(NULL != d->backend_data)
	{
 		for(i = 0; i < len; i++)
		{
			// byte by byte to avoid endianness troubles
			*ptr = ((struct PCIDevice *)(d->backend_data))->ReadConfigByte(pos + i);
			ptr++;
		}
	}

	return TRUE;

on_error:
	return FALSE;
}


static int aos_expansion_write(struct pci_dev *d, int pos, byte *buf, int len)
{
	byte *ptr = buf;
	int i;

	check(NULL != d, "Null pci_dev");
	if(NULL != d->backend_data)
	{
 		for(i = 0; i < len; i++)
		{
			// byte by byte to avoid endianness troubles
			((struct PCIDevice *)(d->backend_data))->WriteConfigByte(pos + i, *ptr);
			ptr++;
		}
	}

	return TRUE;

on_error:
	return FALSE;
}


static void aos_expansion_init_dev(struct pci_dev *d)
{
	if(NULL != d)
	{
		d->backend_data = NULL; // struct PCIDevice * to be obtained
	}
}

static void aos_expansion_cleanup_dev(struct pci_dev *d)
{
	PCIAccess *pci;

	if((NULL != d) && (NULL != d->backend_data) && (NULL != d->access) && (NULL != d->access->backend_data))
	{
		pci = d->access->backend_data;
		pci->ipci->FreeDevice((struct PCIDevice *)d->backend_data);
		d->backend_data = NULL;
	}
}


struct pci_methods pm_aos_expansion = {
  "aos-expansion",
  "The Expansion.library on AmigaOS 4.x",
  NULL, // config, called after allocation of pci_access, if assigned
  aos_expansion_detect,  // detect, mandatory because called without check
  aos_expansion_init,    // init, called once access chosen, eventually after detect
  aos_expansion_cleanup, // cleanup, called at the end
  aos_expansion_scan, // scan,
  pci_generic_fill_info, // fill_info,
  aos_expansion_read, // read,
  aos_expansion_write, // write,
  NULL, // read_vpd,
  aos_expansion_init_dev, // init_dev,
  aos_expansion_cleanup_dev	 // cleanup_dev,
};
