/*
 *	The PCI Library -- Hurd access via RPCs
 *
 *	Copyright (c) 2017 Joan Lledó <jlledom@member.fsf.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE

#include "internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <hurd.h>
#include <hurd/pci.h>
#include <hurd/paths.h>

/* Server path */
#define _SERVERS_BUS_PCI	_SERVERS_BUS "/pci"

/* File names */
#define FILE_CONFIG_NAME "config"
#define FILE_ROM_NAME "rom"

/* Level in the fs tree */
typedef enum
{
  LEVEL_NONE,
  LEVEL_DOMAIN,
  LEVEL_BUS,
  LEVEL_DEV,
  LEVEL_FUNC
} tree_level;

/* Check whether there's a pci server */
static int
hurd_detect(struct pci_access *a)
{
  int err;
  struct stat st;

  err = stat(_SERVERS_BUS_PCI, &st);
  if (err)
    {
      a->error("Could not open file `%s'", _SERVERS_BUS_PCI);
      return 0;
    }

  /* The node must be a directory and a translator */
  return S_ISDIR(st.st_mode) && ((st.st_mode & S_ITRANS) == S_IROOT);
}

/* Empty callbacks, we don't need any special init or cleanup */
static void
hurd_init(struct pci_access *a UNUSED)
{
}

static void
hurd_cleanup(struct pci_access *a UNUSED)
{
}

/* Each device has its own server path. Allocate space for the port. */
static void
hurd_init_dev(struct pci_dev *d)
{
  d->backend_data = pci_malloc(d->access, sizeof(mach_port_t));
  *((mach_port_t *) d->backend_data) = MACH_PORT_NULL;
}

/* Deallocate the port and free its space */
static void
hurd_cleanup_dev(struct pci_dev *d)
{
  mach_port_t device_port;

  device_port = *((mach_port_t *) d->backend_data);
  mach_port_deallocate(mach_task_self(), device_port);

  pci_mfree(d->backend_data);
  d->backend_data = NULL;
}

static mach_port_t
device_port_lookup(struct pci_dev *d)
{
  char server[NAME_MAX];
  mach_port_t device_port = *((mach_port_t *) d->backend_data);

  if (device_port != MACH_PORT_NULL)
    return device_port;

  snprintf(server, NAME_MAX, "%s/%04x/%02x/%02x/%01u/%s",
    _SERVERS_BUS_PCI, d->domain, d->bus, d->dev, d->func,
    FILE_CONFIG_NAME);
  device_port = file_name_lookup(server, 0, 0);

  if (device_port == MACH_PORT_NULL)
    d->access->error("Cannot find the PCI arbiter");

  *((mach_port_t *) d->backend_data) = device_port;
  return device_port;
}

/* Walk through the FS tree to see what is allowed for us */
static void
enum_devices(const char *parent, struct pci_access *a, int domain, int bus,
	     int dev, int func, tree_level lev)
{
  int ret;
  DIR *dir;
  struct dirent *entry;
  char path[NAME_MAX];
  struct pci_dev *d;

  dir = opendir(parent);
  if (!dir)
    {
      if (errno == EPERM || errno == EACCES)
	/* The client lacks the permissions to access this function, skip */
	return;
      else
	a->error("Cannot open directory: %s (%s)", parent, strerror(errno));
    }

  while ((entry = readdir(dir)) != 0)
    {
      snprintf(path, NAME_MAX, "%s/%s", parent, entry->d_name);
      if (entry->d_type == DT_DIR)
	{
	  if (!strncmp(entry->d_name, ".", NAME_MAX)
	      || !strncmp(entry->d_name, "..", NAME_MAX))
	    continue;

	  errno = 0;
	  ret = strtol(entry->d_name, 0, 16);
	  if (errno)
	    {
	      if (closedir(dir) < 0)
		a->warning("Cannot close directory: %s (%s)", parent,
			   strerror(errno));
	      a->error("Wrong directory name: %s (number expected) probably "
		       "not connected to an arbiter", entry->d_name);
	    }

	  /*
	   * We found a valid directory.
	   * Update the address and switch to the next level.
	   */
	  switch (lev)
	    {
	    case LEVEL_DOMAIN:
	      domain = ret;
	      break;
	    case LEVEL_BUS:
	      bus = ret;
	      break;
	    case LEVEL_DEV:
	      dev = ret;
	      break;
	    case LEVEL_FUNC:
	      func = ret;
	      break;
	    default:
	      if (closedir(dir) < 0)
		a->warning("Cannot close directory: %s (%s)", parent,
			   strerror(errno));
	      a->error("Wrong directory tree, probably not connected to an arbiter");
	    }

	  enum_devices(path, a, domain, bus, dev, func, lev + 1);
	}
      else
	{
	  if (strncmp(entry->d_name, FILE_CONFIG_NAME, NAME_MAX))
	    /* We are looking for the config file */
	    continue;

	  /* We found an available virtual device, add it to our list */
	  d = pci_alloc_dev(a);
	  d->domain = domain;
	  d->bus = bus;
	  d->dev = dev;
	  d->func = func;
	  pci_link_dev(a, d);
	}
    }

  if (closedir(dir) < 0)
    a->error("Cannot close directory: %s (%s)", parent, strerror(errno));
}

/* Enumerate devices */
static void
hurd_scan(struct pci_access *a)
{
  enum_devices(_SERVERS_BUS_PCI, a, -1, -1, -1, -1, LEVEL_DOMAIN);
}

/*
 * Read `len' bytes to `buf'.
 *
 * Returns error when the number of read bytes does not match `len'.
 */
static int
hurd_read(struct pci_dev *d, int pos, byte * buf, int len)
{
  int err;
  size_t nread;
  char *data;
  mach_port_t device_port = device_port_lookup(d);

  if (len > 4)
    return pci_generic_block_read(d, pos, buf, len);

  data = (char *) buf;
  err = pci_conf_read(device_port, pos, &data, &nread, len);

  if (data != (char *) buf)
    {
      if (nread > (size_t) len)	/* Sanity check for bogus server.  */
	{
	  vm_deallocate(mach_task_self(), (vm_address_t) data, nread);
	  return 0;
	}

      memcpy(buf, data, nread);
      vm_deallocate(mach_task_self(), (vm_address_t) data, nread);
    }

  return !err && nread == (size_t) len;
}

/*
 * Write `len' bytes from `buf'.
 *
 * Returns error when the number of written bytes does not match `len'.
 */
static int
hurd_write(struct pci_dev *d, int pos, byte * buf, int len)
{
  int err;
  size_t nwrote;
  mach_port_t device_port = device_port_lookup(d);

  if (len > 4)
    return pci_generic_block_write(d, pos, buf, len);

  err = pci_conf_write(device_port, pos, (char *) buf, len, &nwrote);

  return !err && nwrote == (size_t) len;
}

/* Get requested info from the server */

static int
hurd_fill_regions(struct pci_dev *d)
{
  mach_port_t device_port = device_port_lookup(d);
  struct pci_bar regions[6];
  char *buf = (char *) &regions;
  size_t size = sizeof(regions);

  int err = pci_get_dev_regions(device_port, &buf, &size);
  if (err)
    return 0;

  if ((char *) &regions != buf)
    {
      /* Sanity check for bogus server.  */
      if (size > sizeof(regions))
	{
	  vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
	  return 0;
	}

      memcpy(&regions, buf, size);
      vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
    }

  for (int i = 0; i < 6; i++)
    {
      if (regions[i].size == 0)
	continue;

      d->base_addr[i] = regions[i].base_addr;
      d->base_addr[i] |= regions[i].is_IO;
      d->base_addr[i] |= regions[i].is_64 << 2;
      d->base_addr[i] |= regions[i].is_prefetchable << 3;

      d->size[i] = regions[i].size;
    }

  return 1;
}

static int
hurd_fill_rom(struct pci_dev *d)
{
  struct pci_xrom_bar rom;
  mach_port_t device_port = device_port_lookup(d);
  char *buf = (char *) &rom;
  size_t size = sizeof(rom);

  int err = pci_get_dev_rom(device_port, &buf, &size);
  if (err)
    return 0;

  if ((char *) &rom != buf)
    {
      /* Sanity check for bogus server.  */
      if (size > sizeof(rom))
	{
	  vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
	  return 0;
	}

      memcpy(&rom, buf, size);
      vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
    }

  d->rom_base_addr = rom.base_addr;
  d->rom_size = rom.size;

  return 1;
}

static void
hurd_fill_info(struct pci_dev *d, unsigned int flags)
{
  if (!d->access->buscentric)
    {
      if (want_fill(d, flags, PCI_FILL_BASES | PCI_FILL_SIZES))
	{
	  if (hurd_fill_regions(d))
	    clear_fill(d, PCI_FILL_BASES | PCI_FILL_SIZES);
	}
      if (want_fill(d, flags, PCI_FILL_ROM_BASE))
	{
	  if (hurd_fill_rom(d))
	    clear_fill(d, PCI_FILL_ROM_BASE);
	}
    }

  pci_generic_fill_info(d, flags);
}

struct pci_methods pm_hurd = {
  "hurd",
  "Hurd access using RPCs",
  NULL,				/* config */
  hurd_detect,
  hurd_init,
  hurd_cleanup,
  hurd_scan,
  hurd_fill_info,
  hurd_read,
  hurd_write,
  NULL,				/* read_vpd */
  hurd_init_dev,
  hurd_cleanup_dev
};
