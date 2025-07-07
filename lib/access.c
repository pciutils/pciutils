/*
 *	The PCI Library -- User Access
 *
 *	Copyright (c) 1997--2022 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "internal.h"

void
pci_scan_bus(struct pci_access *a)
{
  a->methods->scan(a);
}

struct pci_dev *
pci_alloc_dev(struct pci_access *a)
{
  struct pci_dev *d = pci_malloc(a, sizeof(struct pci_dev));

  memset(d, 0, sizeof(*d));
  d->access = a;
  d->methods = a->methods;
  d->hdrtype = -1;
  d->numa_node = -1;
  if (d->methods->init_dev)
    d->methods->init_dev(d);
  return d;
}

int
pci_link_dev(struct pci_access *a, struct pci_dev *d)
{
  d->next = a->devices;
  a->devices = d;

  /*
   * Applications compiled with older versions of libpci do not expect
   * 32-bit domain numbers. To keep them working, we keep a 16-bit
   * version of the domain number at the previous location in struct
   * pci_dev. This will keep backward compatibility on systems which
   * don't require large domain numbers.
   */
  if (d->domain > 0xffff)
    d->domain_16 = 0xffff;
  else
    d->domain_16 = d->domain;

  return 1;
}

struct pci_dev *
pci_get_dev(struct pci_access *a, int domain, int bus, int dev, int func)
{
  struct pci_dev *d = pci_alloc_dev(a);

  d->domain = domain;
  d->bus = bus;
  d->dev = dev;
  d->func = func;
  return d;
}

static void
pci_free_properties(struct pci_dev *d)
{
  struct pci_property *p;

  while (p = d->properties)
    {
      d->properties = p->next;
      pci_mfree(p);
    }
}

void pci_free_dev(struct pci_dev *d)
{
  if (d->methods->cleanup_dev)
    d->methods->cleanup_dev(d);

  pci_free_caps(d);
  pci_free_properties(d);
  pci_mfree(d);
}

static inline void
pci_read_data(struct pci_dev *d, void *buf, int pos, int len)
{
  if (pos & (len-1))
    d->access->error("Unaligned read: pos=%02x, len=%d", pos, len);
  if (pos + len <= d->cache_len)
    memcpy(buf, d->cache + pos, len);
  else if (!d->methods->read(d, pos, buf, len))
    memset(buf, 0xff, len);
}

byte
pci_read_byte(struct pci_dev *d, int pos)
{
  byte buf;
  pci_read_data(d, &buf, pos, 1);
  return buf;
}

word
pci_read_word(struct pci_dev *d, int pos)
{
  word buf;
  pci_read_data(d, &buf, pos, 2);
  return le16_to_cpu(buf);
}

u32
pci_read_long(struct pci_dev *d, int pos)
{
  u32 buf;
  pci_read_data(d, &buf, pos, 4);
  return le32_to_cpu(buf);
}

int
pci_read_block(struct pci_dev *d, int pos, byte *buf, int len)
{
  return d->methods->read(d, pos, buf, len);
}

int
pci_read_vpd(struct pci_dev *d, int pos, byte *buf, int len)
{
  return d->methods->read_vpd ? d->methods->read_vpd(d, pos, buf, len) : 0;
}

static inline int
pci_write_data(struct pci_dev *d, void *buf, int pos, int len)
{
  if (pos & (len-1))
    d->access->error("Unaligned write: pos=%02x,len=%d", pos, len);
  if (pos + len <= d->cache_len)
    memcpy(d->cache + pos, buf, len);
  return d->methods->write(d, pos, buf, len);
}

int
pci_write_byte(struct pci_dev *d, int pos, byte data)
{
  return pci_write_data(d, &data, pos, 1);
}

int
pci_write_word(struct pci_dev *d, int pos, word data)
{
  word buf = cpu_to_le16(data);
  return pci_write_data(d, &buf, pos, 2);
}

int
pci_write_long(struct pci_dev *d, int pos, u32 data)
{
  u32 buf = cpu_to_le32(data);
  return pci_write_data(d, &buf, pos, 4);
}

int
pci_write_block(struct pci_dev *d, int pos, byte *buf, int len)
{
  if (pos < d->cache_len)
    {
      int l = (pos + len >= d->cache_len) ? (d->cache_len - pos) : len;
      memcpy(d->cache + pos, buf, l);
    }
  return d->methods->write(d, pos, buf, len);
}

static void
pci_reset_properties(struct pci_dev *d)
{
  d->known_fields = 0;
  d->phy_slot = NULL;
  d->module_alias = NULL;
  d->label = NULL;
  pci_free_caps(d);
  pci_free_properties(d);
}

int
pci_fill_info_v313(struct pci_dev *d, int flags)
{
  unsigned int uflags = flags;
  if (uflags & PCI_FILL_RESCAN)
    {
      uflags &= ~PCI_FILL_RESCAN;
      pci_reset_properties(d);
    }
  if (uflags & ~d->known_fields)
    d->methods->fill_info(d, uflags);
  return d->known_fields;
}

/* In version 3.1, pci_fill_info got new flags => versioned alias */
/* In versions 3.2, 3.3, 3.4, 3.5, 3.8 and 3.12, the same has happened */
STATIC_ALIAS(int pci_fill_info(struct pci_dev *d, int flags), pci_fill_info_v313(d, flags));
DEFINE_ALIAS(int pci_fill_info_v30(struct pci_dev *d, int flags), pci_fill_info_v313);
DEFINE_ALIAS(int pci_fill_info_v31(struct pci_dev *d, int flags), pci_fill_info_v313);
DEFINE_ALIAS(int pci_fill_info_v32(struct pci_dev *d, int flags), pci_fill_info_v313);
DEFINE_ALIAS(int pci_fill_info_v33(struct pci_dev *d, int flags), pci_fill_info_v313);
DEFINE_ALIAS(int pci_fill_info_v34(struct pci_dev *d, int flags), pci_fill_info_v313);
DEFINE_ALIAS(int pci_fill_info_v35(struct pci_dev *d, int flags), pci_fill_info_v313);
DEFINE_ALIAS(int pci_fill_info_v38(struct pci_dev *d, int flags), pci_fill_info_v313);
SYMBOL_VERSION(pci_fill_info_v30, pci_fill_info@LIBPCI_3.0);
SYMBOL_VERSION(pci_fill_info_v31, pci_fill_info@LIBPCI_3.1);
SYMBOL_VERSION(pci_fill_info_v32, pci_fill_info@LIBPCI_3.2);
SYMBOL_VERSION(pci_fill_info_v33, pci_fill_info@LIBPCI_3.3);
SYMBOL_VERSION(pci_fill_info_v34, pci_fill_info@LIBPCI_3.4);
SYMBOL_VERSION(pci_fill_info_v35, pci_fill_info@LIBPCI_3.5);
SYMBOL_VERSION(pci_fill_info_v38, pci_fill_info@LIBPCI_3.8);
SYMBOL_VERSION(pci_fill_info_v313, pci_fill_info@@@LIBPCI_3.13);

void
pci_setup_cache(struct pci_dev *d, byte *cache, int len)
{
  d->cache = cache;
  d->cache_len = len;
}

char *
pci_set_property(struct pci_dev *d, u32 key, char *value)
{
  struct pci_property *p;
  struct pci_property **pp = &d->properties;

  while (p = *pp)
    {
      if (p->key == key)
	{
	  *pp = p->next;
	  pci_mfree(p);
	}
      else
	pp = &p->next;
    }

  if (!value)
    return NULL;

  p = pci_malloc(d->access, sizeof(*p) + strlen(value));
  *pp = p;
  p->next = NULL;
  p->key = key;
  strcpy(p->value, value);

  return p->value;
}

char *
pci_get_string_property(struct pci_dev *d, u32 prop)
{
  struct pci_property *p;

  for (p = d->properties; p; p = p->next)
    if (p->key == prop)
      return p->value;

  return NULL;
}
