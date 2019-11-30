/*
 *	The PCI Library -- Hurd access via RPCs
 *
 *	Copyright (c) 2017 Joan Lledó <jlledom@member.fsf.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
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
  d->aux = pci_malloc(d->access, sizeof(mach_port_t));
}

/* Deallocate the port and free its space */
static void
hurd_cleanup_dev(struct pci_dev *d)
{
  mach_port_t device_port;

  device_port = *((mach_port_t *) d->aux);
  mach_port_deallocate(mach_task_self(), device_port);

  pci_mfree(d->aux);
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
  char server[NAME_MAX];
  uint32_t vd;
  uint8_t ht;
  mach_port_t device_port;
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
	      a->error("Wrong directory name: %s (number expected) probably \
                not connected to an arbiter", entry->d_name);
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
	      a->error("Wrong directory tree, probably not connected \
                to an arbiter");
	    }

	  enum_devices(path, a, domain, bus, dev, func, lev + 1);
	}
      else
	{
	  if (strncmp(entry->d_name, FILE_CONFIG_NAME, NAME_MAX))
	    /* We are looking for the config file */
	    continue;

	  /* We found an available virtual device, add it to our list */
	  snprintf(server, NAME_MAX, "%s/%04x/%02x/%02x/%01u/%s",
		   _SERVERS_BUS_PCI, domain, bus, dev, func, entry->d_name);
	  device_port = file_name_lookup(server, 0, 0);
	  if (device_port == MACH_PORT_NULL)
	    {
	      if (closedir(dir) < 0)
		a->warning("Cannot close directory: %s (%s)", parent,
			   strerror(errno));
	      a->error("Cannot open %s", server);
	    }

	  d = pci_alloc_dev(a);
	  *((mach_port_t *) d->aux) = device_port;
	  d->bus = bus;
	  d->dev = dev;
	  d->func = func;
	  pci_link_dev(a, d);

	  vd = pci_read_long(d, PCI_VENDOR_ID);
	  ht = pci_read_byte(d, PCI_HEADER_TYPE);

	  d->vendor_id = vd & 0xffff;
	  d->device_id = vd >> 16U;
	  d->known_fields = PCI_FILL_IDENT;
	  d->hdrtype = ht;
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
  mach_port_t device_port;

  nread = len;
  device_port = *((mach_port_t *) d->aux);
  if (len > 4)
    err = !pci_generic_block_read(d, pos, buf, nread);
  else
    {
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
    }
  if (err)
    return 0;

  return nread == (size_t) len;
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
  mach_port_t device_port;

  nwrote = len;
  device_port = *((mach_port_t *) d->aux);
  if (len > 4)
    err = !pci_generic_block_write(d, pos, buf, len);
  else
    err = pci_conf_write(device_port, pos, (char *) buf, len, &nwrote);
  if (err)
    return 0;

  return nwrote == (size_t) len;
}

/* Get requested info from the server */
static int
hurd_fill_info(struct pci_dev *d, int flags)
{
  int err, i;
  struct pci_bar regions[6];
  struct pci_xrom_bar rom;
  size_t size;
  char *buf;
  mach_port_t device_port;

  device_port = *((mach_port_t *) d->aux);

  if (flags & PCI_FILL_BASES)
    {
      buf = (char *) &regions;
      size = sizeof(regions);

      err = pci_get_dev_regions(device_port, &buf, &size);
      if (err)
	return err;

      if ((char *) &regions != buf)
	{
	  /* Sanity check for bogus server.  */
	  if (size > sizeof(regions))
	    {
	      vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
	      return EGRATUITOUS;
	    }

	  memcpy(&regions, buf, size);
	  vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
	}

      for (i = 0; i < 6; i++)
	{
	  if (regions[i].size == 0)
	    continue;

	  d->base_addr[i] = regions[i].base_addr;
	  d->base_addr[i] |= regions[i].is_IO;
	  d->base_addr[i] |= regions[i].is_64 << 2;
	  d->base_addr[i] |= regions[i].is_prefetchable << 3;

	  if (flags & PCI_FILL_SIZES)
	    d->size[i] = regions[i].size;
	}
    }

  if (flags & PCI_FILL_ROM_BASE)
    {
      /* Get rom info */
      buf = (char *) &rom;
      size = sizeof(rom);
      err = pci_get_dev_rom(device_port, &buf, &size);
      if (err)
	return err;

      if ((char *) &rom != buf)
	{
	  /* Sanity check for bogus server.  */
	  if (size > sizeof(rom))
	    {
	      vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
	      return EGRATUITOUS;
	    }

	  memcpy(&rom, buf, size);
	  vm_deallocate(mach_task_self(), (vm_address_t) buf, size);
	}

      d->rom_base_addr = rom.base_addr;
      d->rom_size = rom.size;
    }

  return pci_generic_fill_info(d, flags);
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
