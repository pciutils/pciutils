/*
 *	The PCI Library -- Darwin kIOACPI access
 *
 *	Copyright (c) 2013 Apple, Inc.
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "internal.h"

#include <mach/mach_error.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOKitKeys.h>

enum {
    kACPIMethodAddressSpaceRead		= 0,
    kACPIMethodAddressSpaceWrite	= 1,
    kACPIMethodDebuggerCommand		= 2,
    kACPIMethodCount
};

#pragma pack(1)

typedef UInt32 IOACPIAddressSpaceID;

enum {
    kIOACPIAddressSpaceIDSystemMemory       = 0,
    kIOACPIAddressSpaceIDSystemIO           = 1,
    kIOACPIAddressSpaceIDPCIConfiguration   = 2,
    kIOACPIAddressSpaceIDEmbeddedController = 3,
    kIOACPIAddressSpaceIDSMBus              = 4
};

/*
 * 64-bit ACPI address
 */
union IOACPIAddress {
    UInt64 addr64;
    struct {
	unsigned int offset     :16;
	unsigned int function   :3;
	unsigned int device     :5;
	unsigned int bus        :8;
	unsigned int segment    :16;
	unsigned int reserved   :16;
    } pci;
};
typedef union IOACPIAddress IOACPIAddress;

#pragma pack()

struct AddressSpaceParam {
    UInt64			value;
    UInt32			spaceID;
    IOACPIAddress	address;
    UInt32			bitWidth;
    UInt32			bitOffset;
    UInt32			options;
};
typedef struct AddressSpaceParam AddressSpaceParam;

static void
darwin_config(struct pci_access *a UNUSED)
{
}

static int
darwin_detect(struct pci_access *a)
{
  io_registry_entry_t    service;
  io_connect_t           connect;
  kern_return_t          status;

  service = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("AppleACPIPlatformExpert"));
  if (service)
    {
      status = IOServiceOpen(service, mach_task_self(), 0, &connect);
      IOObjectRelease(service);
    }

  if (!service || (kIOReturnSuccess != status))
    {
      a->warning("Cannot open AppleACPIPlatformExpert (add boot arg debug=0x144 & run as root)");
      return 0;
    }
  a->debug("...using AppleACPIPlatformExpert");
  a->fd = connect;
  return 1;
}

static void
darwin_init(struct pci_access *a UNUSED)
{
}

static void
darwin_cleanup(struct pci_access *a UNUSED)
{
}

static int
darwin_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  if (!(len == 1 || len == 2 || len == 4))
    return pci_generic_block_read(d, pos, buf, len);

  AddressSpaceParam param;
  kern_return_t     status;

  param.spaceID   = kIOACPIAddressSpaceIDPCIConfiguration;
  param.bitWidth  = len * 8;
  param.bitOffset = 0;
  param.options   = 0;

  param.address.pci.offset   = pos;
  param.address.pci.function = d->func;
  param.address.pci.device   = d->dev;
  param.address.pci.bus      = d->bus;
  param.address.pci.segment  = d->domain;
  param.address.pci.reserved = 0;
  param.value                = -1ULL;

  size_t outSize = sizeof(param);
  status = IOConnectCallStructMethod(d->access->fd, kACPIMethodAddressSpaceRead,
    &param, sizeof(param),
    &param, &outSize);
  if ((kIOReturnSuccess != status))
    d->access->error("darwin_read: kACPIMethodAddressSpaceRead failed: %s", mach_error_string(status));

  switch (len)
    {
    case 1:
      buf[0] = (u8) param.value;
      break;
    case 2:
      ((u16 *) buf)[0] = cpu_to_le16((u16) param.value);
      break;
    case 4:
      ((u32 *) buf)[0] = cpu_to_le32((u32) param.value);
      break;
    }
  return 1;
}

static int
darwin_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  if (!(len == 1 || len == 2 || len == 4))
    return pci_generic_block_write(d, pos, buf, len);

  AddressSpaceParam param;
  kern_return_t     status;

  param.spaceID   = kIOACPIAddressSpaceIDPCIConfiguration;
  param.bitWidth  = len * 8;
  param.bitOffset = 0;
  param.options   = 0;

  param.address.pci.offset   = pos;
  param.address.pci.function = d->func;
  param.address.pci.device   = d->dev;
  param.address.pci.bus      = d->bus;
  param.address.pci.segment  = d->domain;
  param.address.pci.reserved = 0;

  switch (len)
    {
    case 1:
      param.value = buf[0];
      break;
    case 2:
      param.value = le16_to_cpu(((u16 *) buf)[0]);
      break;
    case 4:
      param.value = le32_to_cpu(((u32 *) buf)[0]);
      break;
    }

  size_t outSize = 0;
  status = IOConnectCallStructMethod(d->access->fd, kACPIMethodAddressSpaceWrite,
    &param, sizeof(param),
    NULL, &outSize);
  if ((kIOReturnSuccess != status))
    d->access->error("darwin_read: kACPIMethodAddressSpaceWrite failed: %s", mach_error_string(status));

  return 1;
}

struct pci_methods pm_darwin = {
    .name = "darwin",
    .help = "Darwin",
    .config = darwin_config,
    .detect = darwin_detect,
    .init = darwin_init,
    .cleanup = darwin_cleanup,
    .scan = pci_generic_scan,
    .fill_info = pci_generic_fill_info,
    .read = darwin_read,
    .write = darwin_write,
};
