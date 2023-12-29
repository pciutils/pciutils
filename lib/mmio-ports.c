/*
 *      The PCI Library -- Direct Configuration access via memory mapped ports
 *
 *      Copyright (c) 2022 Pali Rohár <pali@kernel.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Tell 32-bit platforms that we are interested in 64-bit variant of off_t type
 * as 32-bit variant of off_t type is signed and so it cannot represent all
 * possible 32-bit offsets. It is required because off_t type is used by mmap().
 */
#define _FILE_OFFSET_BITS 64

#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef OFF_MAX
#define OFF_MAX (off_t)((1ULL << (sizeof(off_t) * CHAR_BIT - 1)) - 1)
#endif

struct mmio_cache
{
  off_t addr_page;
  off_t data_page;
  void *addr_map;
  void *data_map;
};

static long pagesize;

static void
munmap_regs(struct pci_access *a)
{
  struct mmio_cache *cache = a->backend_data;

  if (!cache)
    return;

  munmap(cache->addr_map, pagesize);
  if (cache->addr_page != cache->data_page)
    munmap(cache->data_map, pagesize);

  pci_mfree(a->backend_data);
  a->backend_data = NULL;
}

static int
mmap_regs(struct pci_access *a, off_t addr_reg, off_t data_reg, int data_off, volatile void **addr, volatile void **data)
{
  struct mmio_cache *cache = a->backend_data;
  off_t addr_page = addr_reg & ~(pagesize-1);
  off_t data_page = data_reg & ~(pagesize-1);
  void *addr_map = MAP_FAILED;
  void *data_map = MAP_FAILED;

  if (cache && cache->addr_page == addr_page)
    addr_map = cache->addr_map;

  if (cache && cache->data_page == data_page)
    data_map = cache->data_map;

  if (addr_map == MAP_FAILED)
    addr_map = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, a->fd, addr_page);

  if (addr_map == MAP_FAILED)
    return 0;

  if (data_map == MAP_FAILED)
    {
      if (data_page == addr_page)
        data_map = addr_map;
      else
        data_map = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, a->fd, data_page);
    }

  if (data_map == MAP_FAILED)
    {
      if (!cache || cache->addr_map != addr_map)
        munmap(addr_map, pagesize);
      return 0;
    }

  if (cache && cache->addr_page != addr_page)
    munmap(cache->addr_map, pagesize);

  if (cache && cache->data_page != data_page && cache->data_page != cache->addr_page)
    munmap(cache->data_map, pagesize);

  if (!cache)
    cache = a->backend_data = pci_malloc(a, sizeof(*cache));

  cache->addr_page = addr_page;
  cache->data_page = data_page;
  cache->addr_map = addr_map;
  cache->data_map = data_map;

  *addr = (unsigned char *)addr_map + (addr_reg & (pagesize-1));
  *data = (unsigned char *)data_map + (data_reg & (pagesize-1)) + data_off;
  return 1;
}

static void
writeb(unsigned char value, volatile void *addr)
{
  *(volatile unsigned char *)addr = value;
}

static void
writew(unsigned short value, volatile void *addr)
{
  *(volatile unsigned short *)addr = value;
}

static void
writel(u32 value, volatile void *addr)
{
  *(volatile u32 *)addr = value;
}

static unsigned char
readb(volatile void *addr)
{
  return *(volatile unsigned char *)addr;
}

static unsigned short
readw(volatile void *addr)
{
  return *(volatile unsigned short *)addr;
}

static u32
readl(volatile void *addr)
{
  return *(volatile u32 *)addr;
}

static int
validate_addrs(const char *addrs)
{
  const char *sep, *next;
  unsigned long long num;
  char *endptr;

  if (!*addrs)
    return 0;

  while (1)
    {
      next = strchr(addrs, ',');
      if (!next)
        next = addrs + strlen(addrs);

      sep = strchr(addrs, '/');
      if (!sep)
        return 0;

      if (!isxdigit(*addrs) || !isxdigit(*(sep+1)))
        return 0;

      errno = 0;
      num = strtoull(addrs, &endptr, 16);
      if (errno || endptr != sep || (num & 3) || num > OFF_MAX)
        return 0;

      errno = 0;
      num = strtoull(sep+1, &endptr, 16);
      if (errno || endptr != next || (num & 3) || num > OFF_MAX)
        return 0;

      if (!*next)
        return 1;

      addrs = next + 1;
    }
}

static int
get_domain_count(const char *addrs)
{
  int count = 1;
  while (addrs = strchr(addrs, ','))
    {
      addrs++;
      count++;
    }
  return count;
}

static int
get_domain_addr(const char *addrs, int domain, off_t *addr_reg, off_t *data_reg)
{
  char *endptr;

  while (domain-- > 0)
    {
      addrs = strchr(addrs, ',');
      if (!addrs)
        return 0;
      addrs++;
    }

  *addr_reg = strtoull(addrs, &endptr, 16);
  *data_reg = strtoull(endptr+1, NULL, 16);

  return 1;
}

static void
conf1_config(struct pci_access *a)
{
  pci_define_param(a, "devmem.path", PCI_PATH_DEVMEM_DEVICE, "Path to the /dev/mem device");
  pci_define_param(a, "mmio-conf1.addrs", "", "Physical addresses of memory mapped Intel conf1 interface"); /* format: 0xaddr1/0xdata1,0xaddr2/0xdata2,... */
}

static void
conf1_ext_config(struct pci_access *a)
{
  pci_define_param(a, "devmem.path", PCI_PATH_DEVMEM_DEVICE, "Path to the /dev/mem device");
  pci_define_param(a, "mmio-conf1-ext.addrs", "", "Physical addresses of memory mapped Intel conf1 extended interface"); /* format: 0xaddr1/0xdata1,0xaddr2/0xdata2,... */
}

static int
detect(struct pci_access *a, char *addrs_param_name)
{
  char *addrs = pci_get_param(a, addrs_param_name);
  char *devmem = pci_get_param(a, "devmem.path");

  if (!*addrs)
    {
      a->debug("%s was not specified", addrs_param_name);
      return 0;
    }

  if (!validate_addrs(addrs))
    {
      a->debug("%s has invalid address format %s", addrs_param_name, addrs);
      return 0;
    }

  if (access(devmem, R_OK | W_OK))
    {
      a->debug("cannot access %s: %s", devmem, strerror(errno));
      return 0;
    }

  a->debug("using %s with %s", devmem, addrs);
  return 1;
}

static int
conf1_detect(struct pci_access *a)
{
  return detect(a, "mmio-conf1.addrs");
}

static int
conf1_ext_detect(struct pci_access *a)
{
  return detect(a, "mmio-conf1-ext.addrs");
}

static char*
get_addrs_param_name(struct pci_access *a)
{
  if (a->methods->config == conf1_ext_config)
    return "mmio-conf1-ext.addrs";
  else
    return "mmio-conf1.addrs";
}

static void
conf1_init(struct pci_access *a)
{
  char *addrs_param_name = get_addrs_param_name(a);
  char *addrs = pci_get_param(a, addrs_param_name);
  char *devmem = pci_get_param(a, "devmem.path");

  pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize < 0)
    a->error("Cannot get page size: %s", strerror(errno));

  if (!*addrs)
    a->error("Option %s was not specified.", addrs_param_name);

  if (!validate_addrs(addrs))
    a->error("Option %s has invalid address format \"%s\".", addrs_param_name, addrs);

  a->fd = open(devmem, O_RDWR | O_DSYNC); /* O_DSYNC bypass CPU cache for mmap() on Linux */
  if (a->fd < 0)
    a->error("Cannot open %s: %s.", devmem, strerror(errno));
}

static void
conf1_cleanup(struct pci_access *a)
{
  if (a->fd < 0)
    return;

  munmap_regs(a);
  close(a->fd);
  a->fd = -1;
}

static void
conf1_scan(struct pci_access *a)
{
  char *addrs_param_name = get_addrs_param_name(a);
  char *addrs = pci_get_param(a, addrs_param_name);
  int domain_count = get_domain_count(addrs);
  int domain;

  for (domain = 0; domain < domain_count; domain++)
    pci_generic_scan_domain(a, domain);
}

static int
conf1_ext_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  char *addrs_param_name = get_addrs_param_name(d->access);
  char *addrs = pci_get_param(d->access, addrs_param_name);
  volatile void *addr, *data;
  off_t addr_reg, data_reg;

  if (pos >= 4096)
    return 0;

  if (len != 1 && len != 2 && len != 4)
    return pci_generic_block_read(d, pos, buf, len);

  if (!get_domain_addr(addrs, d->domain, &addr_reg, &data_reg))
    return 0;

  if (!mmap_regs(d->access, addr_reg, data_reg, pos&3, &addr, &data))
    return 0;

  writel(0x80000000 | ((pos & 0xf00) << 16) | ((d->bus & 0xff) << 16) | (PCI_DEVFN(d->dev, d->func) << 8) | (pos & 0xfc), addr);
  readl(addr); /* write barrier for address */

  switch (len)
    {
    case 1:
      buf[0] = readb(data);
      break;
    case 2:
      ((u16 *) buf)[0] = readw(data);
      break;
    case 4:
      ((u32 *) buf)[0] = readl(data);
      break;
    }

  return 1;
}

static int
conf1_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  if (pos >= 256)
    return 0;

  return conf1_ext_read(d, pos, buf, len);
}

static int
conf1_ext_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  char *addrs_param_name = get_addrs_param_name(d->access);
  char *addrs = pci_get_param(d->access, addrs_param_name);
  volatile void *addr, *data;
  off_t addr_reg, data_reg;

  if (pos >= 4096)
    return 0;

  if (len != 1 && len != 2 && len != 4)
    return pci_generic_block_write(d, pos, buf, len);

  if (!get_domain_addr(addrs, d->domain, &addr_reg, &data_reg))
    return 0;

  if (!mmap_regs(d->access, addr_reg, data_reg, pos&3, &addr, &data))
    return 0;

  writel(0x80000000 | ((pos & 0xf00) << 16) | ((d->bus & 0xff) << 16) | (PCI_DEVFN(d->dev, d->func) << 8) | (pos & 0xfc), addr);
  readl(addr); /* write barrier for address */

  switch (len)
    {
    case 1:
      writeb(buf[0], data);
      break;
    case 2:
      writew(((u16 *) buf)[0], data);
      break;
    case 4:
      writel(((u32 *) buf)[0], data);
      break;
    }

  /*
   * write barrier for data
   * Note that we cannot read from data port because it may have side effect.
   * Instead we read from address port (which should not have side effect) to
   * create a barrier between two conf1_write() calls. But this does not have
   * to be 100% correct as it does not ensure barrier on data port itself.
   * Correct way is to issue CPU instruction for full hw sync barrier but gcc
   * does not provide any (builtin) function yet.
   */
  readl(addr);

  return 1;
}

static int
conf1_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  if (pos >= 256)
    return 0;

  return conf1_ext_write(d, pos, buf, len);
}

struct pci_methods pm_mmio_conf1 = {
  "mmio-conf1",
  "Raw memory mapped I/O port access using Intel conf1 interface",
  conf1_config,
  conf1_detect,
  conf1_init,
  conf1_cleanup,
  conf1_scan,
  pci_generic_fill_info,
  conf1_read,
  conf1_write,
  NULL,					/* read_vpd */
  NULL,					/* init_dev */
  NULL					/* cleanup_dev */
};

struct pci_methods pm_mmio_conf1_ext = {
  "mmio-conf1-ext",
  "Raw memory mapped I/O port access using Intel conf1 extended interface",
  conf1_ext_config,
  conf1_ext_detect,
  conf1_init,
  conf1_cleanup,
  conf1_scan,
  pci_generic_fill_info,
  conf1_ext_read,
  conf1_ext_write,
  NULL,					/* read_vpd */
  NULL,					/* init_dev */
  NULL					/* cleanup_dev */
};
