/*
 *	$Id: access.c,v 1.7 2000/04/21 11:58:00 mj Exp $
 *
 *	The PCI Library -- User Access
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "internal.h"

static struct pci_methods *pci_methods[PCI_ACCESS_MAX] = {
  NULL,
#ifdef HAVE_PM_LINUX_PROC
  &pm_linux_proc,
#else
  NULL,
#endif
#ifdef HAVE_PM_SYSCALLS
  &pm_syscalls,
#else
  NULL,
#endif
#ifdef HAVE_PM_INTEL_CONF
  &pm_intel_conf1,
  &pm_intel_conf2,
#else
  NULL,
  NULL,
#endif
#ifdef HAVE_PM_FBSD_DEVICE
  &pm_fbsd_device,
#else
  NULL,
#endif
#ifdef HAVE_PM_AIX_DEVICE
  &pm_aix_device,
#else
  NULL,
#endif
#ifdef HAVE_PM_DUMP
  &pm_dump,
#else
  NULL,
#endif
};

struct pci_access *
pci_alloc(void)
{
  struct pci_access *a = malloc(sizeof(struct pci_access));
  int i;

  bzero(a, sizeof(*a));
  a->id_file_name = PATH_PCI_IDS;
  for(i=0; i<PCI_ACCESS_MAX; i++)
    if (pci_methods[i] && pci_methods[i]->config)
      pci_methods[i]->config(a);
  return a;
}

void *
pci_malloc(struct pci_access *a, int size)
{
  void *x = malloc(size);

  if (!x)
    a->error("Out of memory (allocation of %d bytes failed)", size);
  return x;
}

void
pci_mfree(void *x)
{
  if (x)
    free(x);
}

static void
pci_generic_error(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  fputs("pcilib: ", stderr);
  vfprintf(stderr, msg, args);
  fputc('\n', stderr);
  exit(1);
}

static void
pci_generic_warn(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  fputs("pcilib: ", stderr);
  vfprintf(stderr, msg, args);
  fputc('\n', stderr);
}

static void
pci_generic_debug(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  vfprintf(stdout, msg, args);
  va_end(args);
}

static void
pci_null_debug(char * UNUSED msg, ...)
{
}

void
pci_init(struct pci_access *a)
{
  if (!a->error)
    a->error = pci_generic_error;
  if (!a->warning)
    a->warning = pci_generic_warn;
  if (!a->debug)
    a->debug = pci_generic_debug;
  if (!a->debugging)
    a->debug = pci_null_debug;

  if (a->method)
    {
      if (a->method >= PCI_ACCESS_MAX || !pci_methods[a->method])
	a->error("This access method is not supported.");
      a->methods = pci_methods[a->method];
    }
  else
    {
      unsigned int i;
      for(i=0; i<PCI_ACCESS_MAX; i++)
	if (pci_methods[i])
	  {
	    a->debug("Trying method %d...", i);
	    if (pci_methods[i]->detect(a))
	      {
		a->debug("...OK\n");
		a->methods = pci_methods[i];
		a->method = i;
		break;
	      }
	    a->debug("...No.\n");
	  }
      if (!a->methods)
	a->error("Cannot find any working access method.");
    }
  a->debug("Decided to use %s\n", a->methods->name);
  a->methods->init(a);
}

void
pci_cleanup(struct pci_access *a)
{
  struct pci_dev *d, *e;

  for(d=a->devices; d; d=e)
    {
      e = d->next;
      pci_free_dev(d);
    }
  if (a->methods)
    a->methods->cleanup(a);
  pci_free_name_list(a);
  pci_mfree(a);
}

void
pci_scan_bus(struct pci_access *a)
{
  a->methods->scan(a);
}

struct pci_dev *
pci_alloc_dev(struct pci_access *a)
{
  struct pci_dev *d = pci_malloc(a, sizeof(struct pci_dev));

  bzero(d, sizeof(*d));
  d->access = a;
  d->methods = a->methods;
  if (d->methods->init_dev)
    d->methods->init_dev(d);
  return d;
}

int
pci_link_dev(struct pci_access *a, struct pci_dev *d)
{
  d->next = a->devices;
  a->devices = d;

  return 1;
}

struct pci_dev *
pci_get_dev(struct pci_access *a, int bus, int dev, int func)
{
  struct pci_dev *d = pci_alloc_dev(a);

  d->bus = bus;
  d->dev = dev;
  d->func = func;
  return d;
}

void pci_free_dev(struct pci_dev *d)
{
  if (d->methods->cleanup_dev)
    d->methods->cleanup_dev(d);
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

int
pci_fill_info(struct pci_dev *d, int flags)
{
  if (flags & PCI_FILL_RESCAN)
    {
      flags &= ~PCI_FILL_RESCAN;
      d->known_fields = 0;
    }
  if (flags & ~d->known_fields)
    d->known_fields |= d->methods->fill_info(d, flags & ~d->known_fields);
  return d->known_fields;
}

void
pci_setup_cache(struct pci_dev *d, byte *cache, int len)
{
  d->cache = cache;
  d->cache_len = len;
}
