/*
 *	The PCI Library -- Configuration Access via /sys/bus/pci
 *
 *	Copyright (c) 2003 Matthew Wilcox <matthew@wil.cx>
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
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
  pci_define_param(a, "sysfs.path", PCI_PATH_SYS_BUS_PCI, "Path to the sysfs device tree");
}

static inline char *
sysfs_name(struct pci_access *a)
{
  return pci_get_param(a, "sysfs.path");
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
  a->fd_vpd = -1;
}

static void
sysfs_flush_cache(struct pci_access *a)
{
  if (a->fd >= 0)
    {
      close(a->fd);
      a->fd = -1;
    }
  if (a->fd_vpd >= 0)
    {
      close(a->fd_vpd);
      a->fd_vpd = -1;
    }
  a->cached_dev = NULL;
}

static void
sysfs_cleanup(struct pci_access *a)
{
  sysfs_flush_cache(a);
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

#define OBJBUFSIZE 1024

static int
sysfs_get_string(struct pci_dev *d, char *object, char *buf, int mandatory)
{
  struct pci_access *a = d->access;
  int fd, n;
  char namebuf[OBJNAMELEN];
  void (*warn)(char *msg, ...) = (mandatory ? a->error : a->warning);

  sysfs_obj_name(d, object, namebuf);
  fd = open(namebuf, O_RDONLY);
  if (fd < 0)
    {
      if (mandatory || errno != ENOENT)
	warn("Cannot open %s: %s", namebuf, strerror(errno));
      return 0;
    }
  n = read(fd, buf, OBJBUFSIZE);
  close(fd);
  if (n < 0)
    {
      warn("Error reading %s: %s", namebuf, strerror(errno));
      return 0;
     }
  if (n >= OBJBUFSIZE)
    {
      warn("Value in %s too long", namebuf);
      return 0;
    }
  buf[n] = 0;
  return 1;
}

static char *
sysfs_deref_link(struct pci_dev *d, char *link_name)
{
  char path[2*OBJNAMELEN], rel_path[OBJNAMELEN];

  sysfs_obj_name(d, link_name, path);
  memset(rel_path, 0, sizeof(rel_path));

  if (readlink(path, rel_path, sizeof(rel_path)) < 0)
    return NULL;

  sysfs_obj_name(d, "", path);
  strcat(path, rel_path);

  // Returns a pointer to malloc'ed memory
  return realpath(path, NULL);
}

static int
sysfs_get_value(struct pci_dev *d, char *object, int mandatory)
{
  char buf[OBJBUFSIZE];

  if (sysfs_get_string(d, object, buf, mandatory))
    return strtol(buf, NULL, 0);
  else
    return -1;
}

static void
sysfs_get_resources(struct pci_dev *d)
{
  struct pci_access *a = d->access;
  char namebuf[OBJNAMELEN], buf[256];
  struct { pciaddr_t flags, base_addr, size; } lines[10];
  int have_bar_bases, have_rom_base, have_bridge_bases;
  FILE *file;
  int i;

  have_bar_bases = have_rom_base = have_bridge_bases = 0;
  sysfs_obj_name(d, "resource", namebuf);
  file = fopen(namebuf, "r");
  if (!file)
    a->error("Cannot open %s: %s", namebuf, strerror(errno));
  for (i = 0; i < 7+6+4+1; i++)
    {
      unsigned long long start, end, size, flags;
      if (!fgets(buf, sizeof(buf), file))
	break;
      if (sscanf(buf, "%llx %llx %llx", &start, &end, &flags) != 3)
	a->error("Syntax error in %s", namebuf);
      if (end > start)
	size = end - start + 1;
      else
	size = 0;
      if (i < 6)
	{
	  d->flags[i] = flags;
	  flags &= PCI_ADDR_FLAG_MASK;
	  d->base_addr[i] = start | flags;
	  d->size[i] = size;
	  have_bar_bases = 1;
	}
      else if (i == 6)
	{
	  d->rom_flags = flags;
	  flags &= PCI_ADDR_FLAG_MASK;
	  d->rom_base_addr = start | flags;
	  d->rom_size = size;
	  have_rom_base = 1;
	}
      else if (i < 7+6+4)
        {
          /*
           * If kernel was compiled without CONFIG_PCI_IOV option then after
           * the ROM line for configured bridge device (that which had set
           * subordinary bus number to non-zero value) are four additional lines
           * which describe resources behind bridge. For PCI-to-PCI bridges they
           * are: IO, MEM, PREFMEM and empty. For CardBus bridges they are: IO0,
           * IO1, MEM0 and MEM1. For unconfigured bridges and other devices
           * there is no additional line after the ROM line. If kernel was
           * compiled with CONFIG_PCI_IOV option then after the ROM line and
           * before the first bridge resource line are six additional lines
           * which describe IOV resources. Read all remaining lines in resource
           * file and based on the number of remaining lines (0, 4, 6, 10) parse
           * resources behind bridge.
           */
          lines[i-7].flags = flags;
          lines[i-7].base_addr = start;
          lines[i-7].size = size;
        }
    }
  if (i == 7+4 || i == 7+6+4)
    {
      int offset = (i == 7+6+4) ? 6 : 0;
      for (i = 0; i < 4; i++)
        {
          d->bridge_flags[i] = lines[offset+i].flags;
          d->bridge_base_addr[i] = lines[offset+i].base_addr;
          d->bridge_size[i] = lines[offset+i].size;
        }
      have_bridge_bases = 1;
    }
  fclose(file);
  if (!have_bar_bases)
    clear_fill(d, PCI_FILL_BASES | PCI_FILL_SIZES | PCI_FILL_IO_FLAGS);
  if (!have_rom_base)
    clear_fill(d, PCI_FILL_ROM_BASE);
  if (!have_bridge_bases)
    clear_fill(d, PCI_FILL_BRIDGE_BASES);
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

      /* Ensure kernel provided domain that fits in a signed integer */
      if (dom > 0x7fffffff)
	a->error("sysfs_scan: Invalid domain %x", dom);

      d->domain = dom;
      d->bus = bus;
      d->dev = dev;
      d->func = func;
      pci_link_dev(a, d);
    }
  closedir(dir);
}

static void
sysfs_fill_slots(struct pci_access *a)
{
  char dirname[1024];
  DIR *dir;
  struct dirent *entry;
  int n;

  n = snprintf(dirname, sizeof(dirname), "%s/slots", sysfs_name(a));
  if (n < 0 || n >= (int) sizeof(dirname))
    a->error("Directory name too long");
  dir = opendir(dirname);
  if (!dir)
    return;

  while (entry = readdir(dir))
    {
      char namebuf[OBJNAMELEN], buf[16];
      FILE *file;
      unsigned int dom, bus, dev;
      int res = 0;
      struct pci_dev *d;

      /* ".", ".." or a special non-device perhaps */
      if (entry->d_name[0] == '.')
	continue;

      n = snprintf(namebuf, OBJNAMELEN, "%s/%s/%s", dirname, entry->d_name, "address");
      if (n < 0 || n >= OBJNAMELEN)
	a->error("File name too long");
      file = fopen(namebuf, "r");
      /*
       * Old versions of Linux had a fakephp which didn't have an 'address'
       * file.  There's no useful information to be gleaned from these
       * devices, pretend they're not there.
       */
      if (!file)
	continue;

      if (!fgets(buf, sizeof(buf), file) || (res = sscanf(buf, "%x:%x:%x", &dom, &bus, &dev)) < 3)
	{
	  /*
	   * In some cases, the slot is not tied to a specific device before
	   * a card gets inserted. This happens for example on IBM pSeries
	   * and we need not warn about it.
	   */
	  if (res != 2)
	    a->warning("sysfs_fill_slots: Couldn't parse entry address %s", buf);
	}
      else
	{
	  for (d = a->devices; d; d = d->next)
	    if (dom == (unsigned)d->domain && bus == d->bus && dev == d->dev && !d->phy_slot)
	      d->phy_slot = pci_set_property(d, PCI_FILL_PHYS_SLOT, entry->d_name);
	}
      fclose(file);
    }
  closedir(dir);
}

static void
sysfs_fill_info(struct pci_dev *d, unsigned int flags)
{
  if (!d->access->buscentric)
    {
      /*
       *  These fields can be read from the config registers, but we want to show
       *  the kernel's view, which has regions and IRQs remapped and other fields
       *  (most importantly classes) possibly fixed if the device is known broken.
       */
      if (want_fill(d, flags, PCI_FILL_IDENT))
	{
	  d->vendor_id = sysfs_get_value(d, "vendor", 1);
	  d->device_id = sysfs_get_value(d, "device", 1);
	}
      if (want_fill(d, flags, PCI_FILL_CLASS))
	d->device_class = sysfs_get_value(d, "class", 1) >> 8;
      if (want_fill(d, flags, PCI_FILL_IRQ))
	  d->irq = sysfs_get_value(d, "irq", 1);
      if (want_fill(d, flags, PCI_FILL_BASES | PCI_FILL_ROM_BASE | PCI_FILL_SIZES | PCI_FILL_IO_FLAGS | PCI_FILL_BRIDGE_BASES))
	  sysfs_get_resources(d);
    }

  if (want_fill(d, flags, PCI_FILL_PHYS_SLOT))
    {
      struct pci_dev *pd;
      sysfs_fill_slots(d->access);
      for (pd = d->access->devices; pd; pd = pd->next)
	pd->known_fields |= PCI_FILL_PHYS_SLOT;
    }

  if (want_fill(d, flags, PCI_FILL_MODULE_ALIAS))
    {
      char buf[OBJBUFSIZE];
      if (sysfs_get_string(d, "modalias", buf, 0))
	d->module_alias = pci_set_property(d, PCI_FILL_MODULE_ALIAS, buf);
    }

  if (want_fill(d, flags, PCI_FILL_LABEL))
    {
      char buf[OBJBUFSIZE];
      if (sysfs_get_string(d, "label", buf, 0))
	d->label = pci_set_property(d, PCI_FILL_LABEL, buf);
    }

  if (want_fill(d, flags, PCI_FILL_NUMA_NODE))
    d->numa_node = sysfs_get_value(d, "numa_node", 0);

  if (want_fill(d, flags, PCI_FILL_IOMMU_GROUP))
    {
      char *group_link = sysfs_deref_link(d, "iommu_group");
      if (group_link)
        {
          pci_set_property(d, PCI_FILL_IOMMU_GROUP, basename(group_link));
          free(group_link);
        }
    }

  if (want_fill(d, flags, PCI_FILL_DT_NODE))
    {
      char *node = sysfs_deref_link(d, "of_node");
      if (node)
	{
	  pci_set_property(d, PCI_FILL_DT_NODE, node);
	  free(node);
	}
    }

  pci_generic_fill_info(d, flags);
}

/* Intent of the sysfs_setup() caller */
enum
  {
    SETUP_READ_CONFIG = 0,
    SETUP_WRITE_CONFIG = 1,
    SETUP_READ_VPD = 2
  };

static int
sysfs_setup(struct pci_dev *d, int intent)
{
  struct pci_access *a = d->access;
  char namebuf[OBJNAMELEN];

  if (a->cached_dev != d || (intent == SETUP_WRITE_CONFIG && !a->fd_rw))
    {
      sysfs_flush_cache(a);
      a->cached_dev = d;
    }

  if (intent == SETUP_READ_VPD)
    {
      if (a->fd_vpd < 0)
	{
	  sysfs_obj_name(d, "vpd", namebuf);
	  a->fd_vpd = open(namebuf, O_RDONLY);
	  /* No warning on error; vpd may be absent or accessible only to root */
	}
      return a->fd_vpd;
    }

  if (a->fd < 0)
    {
      sysfs_obj_name(d, "config", namebuf);
      a->fd_rw = a->writeable || intent == SETUP_WRITE_CONFIG;
      a->fd = open(namebuf, a->fd_rw ? O_RDWR : O_RDONLY);
      if (a->fd < 0)
	a->warning("Cannot open %s", namebuf);
      a->fd_pos = 0;
    }
  return a->fd;
}

static int sysfs_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = sysfs_setup(d, SETUP_READ_CONFIG);
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
  int fd = sysfs_setup(d, SETUP_WRITE_CONFIG);
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

#ifdef PCI_HAVE_DO_READ

/* pread() is not available and do_read() only works for a single fd, so we
 * cannot implement read_vpd properly. */
static int sysfs_read_vpd(struct pci_dev *d, int pos, byte *buf, int len)
{
  return 0;
}

#else /* !PCI_HAVE_DO_READ */

static int sysfs_read_vpd(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = sysfs_setup(d, SETUP_READ_VPD);
  int res;

  if (fd < 0)
    return 0;
  res = pread(fd, buf, len, pos);
  if (res < 0)
    {
      d->access->warning("sysfs_read_vpd: read failed: %s", strerror(errno));
      return 0;
    }
  else if (res != len)
    return 0;
  return 1;
}

#endif /* PCI_HAVE_DO_READ */

static void sysfs_cleanup_dev(struct pci_dev *d)
{
  struct pci_access *a = d->access;

  if (a->cached_dev == d)
    sysfs_flush_cache(a);
}

struct pci_methods pm_linux_sysfs = {
  "linux-sysfs",
  "The sys filesystem on Linux",
  sysfs_config,
  sysfs_detect,
  sysfs_init,
  sysfs_cleanup,
  sysfs_scan,
  sysfs_fill_info,
  sysfs_read,
  sysfs_write,
  sysfs_read_vpd,
  NULL,					/* init_dev */
  sysfs_cleanup_dev
};
