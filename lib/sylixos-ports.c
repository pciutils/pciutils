/*
 *  The PCI Library -- Direct Configuration access via SylixOS Ports
 *
 *  Copyright (c) 2018 YuJian.Gong <gongyujian@acoinfo.com>
 *
 *  Can be freely distributed and used under the terms of the GNU GPL.
 */

#define _GNU_SOURCE
#define  __SYLIXOS_KERNEL
#define  __SYLIXOS_PCI_DRV
#include <SylixOS.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define PCI_VENDOR_ID_IS_INVALIDATE(vendor)     \
        (((vendor) == 0xffff) || ((vendor) == 0x0000))

typedef struct {
    struct pci_access *a;
    byte *busmap;
    int bus;
} pci_dev_scan;

int  sylixos_pci_traversal (int (*func)(), void *arg, int min_bus_num, int max_bus_num)
{
    INT     ibus, islot, ifunc;
    UINT8   header;
    UINT16  vendor;

    if (!func || (min_bus_num < 0) || (max_bus_num < 0)) {
        return  (PX_ERROR);
    }

    min_bus_num = (min_bus_num > (PCI_MAX_BUS - 1)) ? (PCI_MAX_BUS - 1) : min_bus_num;
    max_bus_num = (max_bus_num > (PCI_MAX_BUS - 1)) ? (PCI_MAX_BUS - 1) : max_bus_num;

    for (ibus = min_bus_num; ibus <= max_bus_num; ibus++) {
        for (islot = 0; islot < PCI_MAX_SLOTS; islot++) {
            for (ifunc = 0; ifunc < PCI_MAX_FUNCTIONS; ifunc++) {
                pciConfigInWord(ibus, islot, ifunc, PCI_VENDOR_ID, &vendor);

                if (PCI_VENDOR_ID_IS_INVALIDATE(vendor)) {
                    if (ifunc == 0) {
                        break;
                    }
                    continue;
                }

                if (func(ibus, islot, ifunc, arg) != ERROR_NONE) {
                    goto    __out;
                }

                if (ifunc == 0) {
                    pciConfigInByte(ibus, islot, ifunc, PCI_HEADER_TYPE, &header);
                    if ((header & PCI_HEADER_MULTI_FUNC) != PCI_HEADER_MULTI_FUNC) {
                        break;
                    }
                }
            }
        }
    }

__out:
    return  (ERROR_NONE);
}

static int
pci_dev_list_create (int  bus, int  dev, int  func, void *arg)
{
    pci_dev_scan *f = (pci_dev_scan *)arg;
    struct pci_dev *d;
    u32 vd;

    f->busmap[bus] = 1;
    d = pci_alloc_dev(f->a);
    d->bus = bus;
    d->dev = dev;
    d->func = func;

    vd = pci_read_long(d, PCI_VENDOR_ID);
    d->vendor_id = vd & 0xffff;
    d->device_id = vd >> 16U;
    d->known_fields = PCI_FILL_IDENT;
    d->hdrtype = pci_read_byte(d, PCI_HEADER_TYPE);
    pci_link_dev(f->a, d);

    return  (ERROR_NONE);
}

static void
pci_generic_scan_bus_tbl(struct pci_access *a, byte *busmap, int bus)
{
    pci_dev_scan    f;

    f.a = a;
    f.busmap = busmap;
    f.bus = bus;

    sylixos_pci_traversal(pci_dev_list_create, &f, bus, 255);
}

static void
sylixos_pci_generic_scan(struct pci_access *a)
{
    int se;
    byte busmap[256];
    char *env;

    memset(busmap, 0, sizeof(busmap));

    env = getenv(PCI_SCAN_FUNC);
    if (!env) {
        pci_generic_scan_bus(a, busmap, 0);
        return;
    }
    se = atoi(env);
    if (se) {
        pci_generic_scan_bus_tbl(a, busmap, 0);
    } else {
        pci_generic_scan_bus(a, busmap, 0);
    }
}

static void
sylixos_config(struct pci_access *a)
{
    pci_define_param(a, "sylixos.path", PCI_PATH_SYLIXOS_DEVICE, "Path to the SylixOS PCI device");
}

static int
sylixos_detect(struct pci_access *a)
{
    char *name = pci_get_param(a, "sylixos.path");

    if (access(name, R_OK)) {
        a->warning("Cannot open %s", name);
        return 0;
    }

    a->debug("...using %s", name);

    return 1;
}

static void
sylixos_init(struct pci_access *a)
{
    a->fd = -1;
}

static void
sylixos_cleanup(struct pci_access *a)
{
    if (a->fd >= 0) {
        close(a->fd);
        a->fd = -1;
    }
}

static int
sylixos_read(struct pci_dev *d, int pos, byte *buf, int len)
{
    int     ret        = PX_ERROR;
    u8      data_byte  = -1;
    u16     data_word  = -1;
    u32     data_dword = -1;

    if (!(len == 1 || len == 2 || len == 4)) {
        return pci_generic_block_read(d, pos, buf, len);
    }

    if (pos >= 256) {
        return 0;
    }

    switch (len) {

    case 1:
        ret = pciConfigInByte(d->bus, d->dev, d->func, pos, &data_byte);
        if (ret != ERROR_NONE) {
            return  (0);
        }
        buf[0] = (u8)data_byte;
        break;

    case 2:
        ret = pciConfigInWord(d->bus, d->dev, d->func, pos, &data_word);
        if (ret != ERROR_NONE) {
            return  (0);
        }
        ((u16 *) buf)[0] = cpu_to_le16(data_word);
        break;

    case 4:
        ret = pciConfigInDword(d->bus, d->dev, d->func, pos, &data_dword);
        if (ret != ERROR_NONE) {
            return  (0);
        }
        ((u32 *) buf)[0] = cpu_to_le32(data_dword);
        break;
    }

    return 1;
}

static int
sylixos_write(struct pci_dev *d, int pos, byte *buf, int len)
{
    int     ret = PX_ERROR;
    u8      data_byte;
    u16     data_word;
    u32     data_dword;

    if (!(len == 1 || len == 2 || len == 4)) {
        return pci_generic_block_write(d, pos, buf, len);
    }

    if (pos >= 256) {
        return 0;
    }

    switch (len) {

    case 1:
        data_byte = buf[0];
        ret = pciConfigOutByte(d->bus, d->dev, d->func, pos, data_byte);
        if (ret != ERROR_NONE) {
            return  (0);
        }
        break;

    case 2:
        data_word = le16_to_cpu(((u16 *) buf)[0]);
        ret = pciConfigOutWord(d->bus, d->dev, d->func, pos, data_word);
        if (ret != ERROR_NONE) {
            return  (0);
        }
        break;

    case 4:
        data_dword = le32_to_cpu(((u32 *) buf)[0]);
        ret = pciConfigOutDword(d->bus, d->dev, d->func, pos, data_dword);
        if (ret != ERROR_NONE) {
            return  (0);
        }
        break;
    }

  return 1;
}

static void
sylixos_scan(struct pci_access *a)
{
    return  (sylixos_pci_generic_scan(a));
}

static int
sylixos_fill_info(struct pci_dev *d, int flags)
{
    int i;
    int ret;
    int cnt;

    PCI_DEV_HANDLE      hDevHandle = LW_NULL;
    PCI_RESOURCE_HANDLE hResource  = LW_NULL;

    ret = pci_generic_fill_info(d, flags);

    switch (d->hdrtype) {

    case PCI_HEADER_TYPE_NORMAL:
        cnt = 6;
        break;

    case PCI_HEADER_TYPE_BRIDGE:
        cnt = 2;
        break;

    case PCI_HEADER_TYPE_CARDBUS:
        cnt = 1;
        break;

    default:
        cnt = 0;
        break;
    }

    hDevHandle = pciDevHandleGet(d->bus, d->dev, d->func);
    if (!hDevHandle) {
        return (ret);
    }

    for (i = 0; i <= cnt; i++) {
        hResource = &hDevHandle->PCIDEV_tResource[i];
        if (hResource) {
            if (hResource->PCIRS_stEnd == hResource->PCIRS_stStart) {
                continue;
            }

            if (hResource->PCIRS_ulFlags & PCI_IORESOURCE_READONLY) {
                d->rom_size = (pciaddr_t)PCI_RESOURCE_SIZE(hResource);
                d->known_fields |= PCI_FILL_SIZES;
                d->known_fields |= PCI_FILL_IO_FLAGS;
                d->rom_flags = hResource->PCIRS_ulFlags;
            } else {
                d->size[i] = (pciaddr_t)PCI_RESOURCE_SIZE(hResource);
                d->known_fields |= PCI_FILL_SIZES;
            }
        } else {
            d->size[i] = 0;
        }
    }

    return (ret);
}

struct pci_methods pm_sylixos_device = {
  "SylixOS-PCI",
  "SylixOS /proc/pci device",
  sylixos_config,					                                    /* config                       */
  sylixos_detect,
  sylixos_init,
  sylixos_cleanup,
  sylixos_scan,
  sylixos_fill_info,
  sylixos_read,
  sylixos_write,
  NULL,					                                                /* read_vpd                     */
  NULL,					                                                /* init_dev                     */
  NULL					                                                /* cleanup_dev                  */
};
