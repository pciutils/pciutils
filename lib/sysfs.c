/*
 *	The PCI Library -- Configuration Access via /sys/bus/pci
 *
 * 	Copyright (c) 2003 Matthew Wilcox <willy@fc.hp.com>
 *	Copyright (c) 1997--2003 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>

#include "internal.h"
#include "pread.h"

static void
sysfs_config(struct pci_access *a)
{
  a->method_params[PCI_ACCESS_SYS_BUS_PCI] = PATH_SYS_BUS_PCI;
}

static inline char *
sysfs_name(struct pci_access *a)
{
  return a->method_params[PCI_ACCESS_SYS_BUS_PCI];
}

static int
sysfs_detect(struct pci_access *a)
{
  if (access(sysfs_name(a), R_OK))
    {
      a->debug("...cannot open %s", sysfs_name(a));
      return 0;
    }
  a->debug("...using %s", sysfs_name(a));
  return 1;
}

static void
sysfs_init(struct pci_access *a)
{
  a->fd = -1;
}

static void
sysfs_cleanup(struct pci_access *a)
{
  if (a->fd >= 0)
    {
      close(a->fd);
      a->fd = -1;
    }
}

#define OBJNAMELEN 1024
static void
sysfs_obj_name(struct pci_dev *d, char *object, char *buf)
{
  int n = snprintf(buf, OBJNAMELEN, "%s/devices/%04x:%02x:%02x.%d/%s",
		   sysfs_name(d->access), d->domain, d->bus, d->dev, d->func, object);
  if (n < 0 || n >= OBJNAMELEN)
    d->access->error("File name too long");
}

static int
sysfs_get_value(struct pci_dev *d, char *object)
{
  struct pci_access *a = d->access;
  int fd, n;
  char namebuf[OBJNAMELEN], buf[256];

  sysfs_obj_name(d, object, namebuf);
  fd = open(namebuf, O_RDONLY);
  if (fd < 0)
    a->error("Cannot open %s: %s", namebuf, strerror(errno));
  n = read(fd, buf, sizeof(buf));
  close(fd);
  if (n < 0)
    a->error("Error reading %s: %s", namebuf, strerror(errno));
  if (n >= (int) sizeof(buf))
    a->error("Value in %s too long", namebuf);
  buf[n] = 0;
  return strtol(buf, NULL, 0);
}

static void
sysfs_get_resources(struct pci_dev *d)
{
  struct pci_access *a = d->access;
  char namebuf[OBJNAMELEN], buf[256];
  FILE *file;
  int i;

  sysfs_obj_name(d, "resource", namebuf);
  file = fopen(namebuf, "r");
  if (!file)
    a->error("Cannot open %s: %s", namebuf, strerror(errno));
  for (i = 0; i < 8; i++)
    {
      unsigned long long start, end, size;
      if (!fgets(buf, sizeof(buf), file))
	break;
      if (sscanf(buf, "%llx %llx", &start, &end) != 2)
	a->error("Syntax error in %s", namebuf);
      if (start != (unsigned long long)(pciaddr_t) start ||
	  end != (unsigned long long)(pciaddr_t) end)
	{
	  a->warning("Resource %d in %s has a 64-bit address, ignoring", namebuf);
	  start = end = 0;
	}
      if (start)
	size = end - start + 1;
      else
	size = 0;
      if (i < 7)
	{
	  d->base_addr[i] = start;
	  d->size[i] = size;
	}
      else
	{
	  d->rom_base_addr = start;
	  d->rom_size = size;
	}
    }
  fclose(file);
}

static void sysfs_scan(struct pci_access *a)
{
  char dirname[1024];
  DIR *dir;
  struct dirent *entry;
  int n;

  n = snprintf(dirname, sizeof(dirname), "%s/devices", sysfs_name(a));
  if (n < 0 || n >= (int) sizeof(dirname))
    a->error("Directory name too long");
  dir = opendir(dirname);
  if (!dir)
    a->error("Cannot open %s", dirname);
  while ((entry = readdir(dir)))
    {
      struct pci_dev *d;
      unsigned int dom, bus, dev, func;

      /* ".", ".." or a special non-device perhaps */
      if (entry->d_name[0] == '.')
	continue;

      d = pci_alloc_dev(a);
      if (sscanf(entry->d_name, "%x:%x:%x.%d", &dom, &bus, &dev, &func) < 4)
	a->error("sysfs_scan: Couldn't parse entry name %s", entry->d_name);
      d->domain = dom;
      d->bus = bus;
      d->dev = dev;
      d->func = func;
      if (!a->buscentric)
	{
	  sysfs_get_resources(d);
	  d->irq = sysfs_get_value(d, "irq");
	  d->known_fields = PCI_FILL_IRQ | PCI_FILL_BASES | PCI_FILL_ROM_BASE | PCI_FILL_SIZES;
#if 0
	  /*
	   *  We prefer reading these from the config registers, it's faster.
	   *  However, it would be possible and maybe even useful to hack the kernel
	   *  to believe that some device has a different ID. If you do it, just
	   *  enable this piece of code.  --mj
	   */
	  d->vendor_id = sysfs_get_value(d, "vendor");
	  d->device_id = sysfs_get_value(d, "device");
	  d->known_fields |= PCI_FILL_IDENT;
#endif
	}
      pci_link_dev(a, d);
    }
  closedir(dir);
}

static int
sysfs_setup(struct pci_dev *d, int rw)
{
  struct pci_access *a = d->access;

  if (a->cached_dev != d || a->fd_rw < rw)
    {
      char namebuf[OBJNAMELEN];
      if (a->fd >= 0)
	close(a->fd);
      sysfs_obj_name(d, "config", namebuf);
      a->fd_rw = a->writeable || rw;
      a->fd = open(namebuf, a->fd_rw ? O_RDWR : O_RDONLY);
      if (a->fd < 0)
	a->warning("Cannot open %s", namebuf);
      a->cached_dev = d;
      a->fd_pos = 0;
    }
  return a->fd;
}

static int sysfs_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = sysfs_setup(d, 0);
  int res;

  if (fd < 0)
    return 0;
  res = do_read(d, fd, buf, len, pos);
  if (res < 0)
    {
      d->access->warning("sysfs_read: read failed: %s", strerror(errno));
      return 0;
    }
  else if (res != len)
    return 0;
  return 1;
}

static int sysfs_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = sysfs_setup(d, 1);
  int res;

  if (fd < 0)
    return 0;
  res = do_write(d, fd, buf, len, pos);
  if (res < 0)
    {
      d->access->warning("sysfs_write: write failed: %s", strerror(errno));
      return 0;
    }
  else if (res != len)
    {
      d->access->warning("sysfs_write: tried to write %d bytes at %d, but only %d succeeded", len, pos, res);
      return 0;
    }
  return 1;
}

static void sysfs_cleanup_dev(struct pci_dev *d)
{
  struct pci_access *a = d->access;

  if (a->cached_dev == d)
    {
      a->cached_dev = NULL;
      close(a->fd);
      a->fd = -1;
    }
}

struct pci_methods pm_linux_sysfs = {
  "Linux-sysfs",
  sysfs_config,
  sysfs_detect,
  sysfs_init,
  sysfs_cleanup,
  sysfs_scan,
  pci_generic_fill_info,
  sysfs_read,
  sysfs_write,
  NULL,					/* init_dev */
  sysfs_cleanup_dev
};
