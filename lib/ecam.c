/*
 *      The PCI Library -- Direct Configuration access via PCIe ECAM
 *
 *      Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "internal.h"
#include "physmem.h"
#include "physmem-access.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <glob.h>
#include <unistd.h>

#if defined (__FreeBSD__) || defined (__DragonFly__) || defined(__NetBSD__)
#include <sys/sysctl.h>
#endif

#if defined (__FreeBSD__) || defined (__DragonFly__)
#include <kenv.h>
#endif

struct acpi_rsdp {
  char signature[8];
  u8 checksum;
  char oem_id[6];
  u8 revision;
  u32 rsdt_address;
  struct {
    u32 length;
    u64 xsdt_address;
    u8 ext_checksum;
    u8 reserved[3];
  } rsdp20[0];
} PCI_PACKED;

struct acpi_sdt {
  char signature[4];
  u32 length;
  u8 revision;
  u8 checksum;
  char oem_id[6];
  char oem_table_id[8];
  u32 oem_revision;
  char asl_compiler_id[4];
  u32 asl_compiler_revision;
} PCI_PACKED;

struct acpi_rsdt {
  struct acpi_sdt sdt;
  u32 sdt_addresses[0];
} PCI_PACKED;

struct acpi_xsdt {
  struct acpi_sdt sdt;
  u64 sdt_addresses[0];
} PCI_PACKED;

struct acpi_mcfg {
  struct acpi_sdt sdt;
  u64 reserved;
  struct {
    u64 address;
    u16 pci_segment;
    u8 start_bus_number;
    u8 end_bus_number;
    u32 reserved;
  } allocations[0];
} PCI_PACKED;

struct mmap_cache {
  void *map;
  u64 addr;
  u32 length;
  int domain;
  u8 bus;
  int w;
};

// Back-end data linked to struct pci_access
struct ecam_access {
  struct acpi_mcfg *mcfg;
  struct mmap_cache *cache;
  struct physmem *physmem;
  long pagesize;
};

static unsigned int
get_rsdt_addresses_count(struct acpi_rsdt *rsdt)
{
  return (rsdt->sdt.length - ((unsigned char*)&rsdt->sdt_addresses - (unsigned char *)rsdt)) / sizeof(rsdt->sdt_addresses[0]);
}

static unsigned int
get_xsdt_addresses_count(struct acpi_xsdt *xsdt)
{
  return (xsdt->sdt.length - ((unsigned char*)&xsdt->sdt_addresses - (unsigned char *)xsdt)) / sizeof(xsdt->sdt_addresses[0]);
}

static unsigned int
get_mcfg_allocations_count(struct acpi_mcfg *mcfg)
{
  return (mcfg->sdt.length - ((unsigned char *)&mcfg->allocations - (unsigned char *)mcfg)) / sizeof(mcfg->allocations[0]);
}

static u8
calculate_checksum(const u8 *bytes, int len)
{
  u8 checksum = 0;

  while (len-- > 0)
    checksum -= *(bytes++);
  return checksum;
}

static struct acpi_sdt *
check_and_map_sdt(struct physmem *physmem, long pagesize, u64 addr, const char *signature, void **map_addr, u32 *map_length)
{
  struct acpi_sdt *sdt;
  char sdt_signature[sizeof(sdt->signature)];
  u32 length;
  void *map;

  if (addr + sizeof(*sdt) < addr)
    return NULL;

  map = physmem_map(physmem, addr & ~(pagesize-1), sizeof(*sdt) + (addr & (pagesize-1)), 0);
  if (map == (void *)-1)
    return NULL;

  sdt = (struct acpi_sdt *)((unsigned char *)map + (addr & (pagesize-1)));
  length = sdt->length;
  memcpy(sdt_signature, sdt->signature, sizeof(sdt->signature));

  physmem_unmap(physmem, map, sizeof(*sdt) + (addr & (pagesize-1)));

  if (memcmp(sdt_signature, signature, sizeof(sdt_signature)) != 0)
    return NULL;
  if (length < sizeof(*sdt))
    return NULL;

  map = physmem_map(physmem, addr & ~(pagesize-1), length + (addr & (pagesize-1)), 0);
  if (map == (void *)-1)
    return NULL;

  sdt = (struct acpi_sdt *)((unsigned char *)map + (addr & (pagesize-1)));

  if (calculate_checksum((u8 *)sdt, sdt->length) != 0)
    {
      physmem_unmap(physmem, map, length + (addr & (pagesize-1)));
      return NULL;
    }

  *map_addr = map;
  *map_length = length + (addr & (pagesize-1));
  return sdt;
}

static int
check_rsdp(struct acpi_rsdp *rsdp)
{
  if (memcmp(rsdp->signature, "RSD PTR ", sizeof(rsdp->signature)) != 0)
    return 0;
  if (calculate_checksum((u8 *)rsdp, sizeof(*rsdp)) != 0)
    return 0;
  return 1;
}

static int
check_and_parse_rsdp(struct physmem *physmem, long pagesize, u64 addr, u32 *rsdt_address, u64 *xsdt_address)
{
  struct acpi_rsdp *rsdp;
  unsigned char buf[sizeof(*rsdp) + sizeof(*rsdp->rsdp20)];
  void *map;

  map = physmem_map(physmem, addr & ~(pagesize-1), sizeof(buf) + (addr & (pagesize-1)), 0);
  if (map == (void *)-1)
    return 0;

  rsdp = (struct acpi_rsdp *)buf;
  memcpy(rsdp, (unsigned char *)map + (addr & (pagesize-1)), sizeof(buf));

  physmem_unmap(physmem, map, sizeof(buf));

  if (!check_rsdp(rsdp))
    return 0;

  *rsdt_address = rsdp->rsdt_address;

  if (rsdp->revision != 0 &&
      (*rsdp->rsdp20).length == sizeof(*rsdp) + sizeof(*rsdp->rsdp20) &&
      calculate_checksum((u8 *)rsdp, (*rsdp->rsdp20).length) == 0)
    *xsdt_address = (*rsdp->rsdp20).xsdt_address;
  else
    *xsdt_address = 0;

  return 1;
}

static u64
find_rsdp_address(struct pci_access *a, const char *efisystab, int use_bsd UNUSED, int use_x86bios UNUSED)
{
  u64 ullnum;
#if defined (__FreeBSD__) || defined (__DragonFly__) || defined(__NetBSD__)
  unsigned long ulnum;
#endif
  char buf[1024];
  char *endptr;
  u64 acpi20;
  u64 acpi;
#if defined(__amd64__) || defined(__i386__)
  struct ecam_access *eacc = a->backend_data;
  struct physmem *physmem = eacc->physmem;
  long pagesize = eacc->pagesize;
  u64 rsdp_addr;
  u64 addr;
  void *map;
  u64 ebda;
#endif
  size_t len;
  FILE *f;

  if (efisystab[0])
    {
      acpi = 0;
      acpi20 = 0;
      a->debug("reading EFI system table: %s...", efisystab);
      f = fopen(efisystab, "r");
      if (f)
        {
          while (fgets(buf, sizeof(buf), f))
            {
              len = strlen(buf);
              while (len > 0 && buf[len-1] == '\n')
                buf[--len] = '\0';
              if (strncmp(buf, "ACPI20=", 7) == 0 && isxdigit(buf[7]))
                {
                  errno = 0;
                  ullnum = strtoull(buf+7, &endptr, 16);
                  if (!errno && !*endptr)
                    acpi20 = ullnum;
                }
              else if (strncmp(buf, "ACPI=", 5) == 0 && isxdigit(buf[5]))
                {
                  errno = 0;
                  ullnum = strtoull(buf+5, &endptr, 16);
                  if (!errno && !*endptr)
                    acpi = ullnum;
                }
            }
          fclose(f);
        }
      else
        a->debug("opening failed: %s...", strerror(errno));

      if (acpi20)
        return acpi20;
      else if (acpi)
        return acpi;
    }

#if defined (__FreeBSD__) || defined (__DragonFly__)
  if (use_bsd)
    {
      /* First try FreeBSD kenv hint.acpi.0.rsdp */
      a->debug("calling kenv hint.acpi.0.rsdp...");
      if (kenv(KENV_GET, "hint.acpi.0.rsdp", buf, sizeof(buf)) > 0)
        {
          errno = 0;
          ullnum = strtoull(buf, &endptr, 16);
          if (!errno && !*endptr)
            return ullnum;
        }

      /* Then try FreeBSD sysctl machdep.acpi_root */
      a->debug("calling sysctl machdep.acpi_root...");
      len = sizeof(ulnum);
      if (sysctlbyname("machdep.acpi_root", &ulnum, &len, NULL, 0) == 0)
        return ulnum;
    }
#endif

#if defined(__NetBSD__)
  if (use_bsd)
    {
      /* Try NetBSD sysctl hw.acpi.root */
      a->debug("calling sysctl hw.acpi.root...");
      len = sizeof(ulnum);
      if (sysctlbyname("hw.acpi.root", &ulnum, &len, NULL, 0) == 0)
        return ulnum;
    }
#endif

#if defined(__amd64__) || defined(__i386__)
  if (use_x86bios)
    {
      rsdp_addr = 0;

      /* Scan first kB of Extended BIOS Data Area */
      a->debug("reading EBDA location from BDA...");
      map = physmem_map(physmem, 0, 0x40E + 2, 0);
      if (map != (void *)-1)
        {
          ebda = (u64)physmem_readw((unsigned char *)map + 0x40E) << 4;
          if (physmem_unmap(physmem, map, 0x40E + 2) != 0)
            a->debug("unmapping of BDA failed: %s...", strerror(errno));
          if (ebda >= 0x400)
            {
              a->debug("scanning first kB of EBDA at 0x%" PCI_U64_FMT_X "...", ebda);
              map = physmem_map(physmem, ebda & ~(pagesize-1), 1024 + (ebda & (pagesize-1)), 0);
              if (map != (void *)-1)
                {
                  for (addr = ebda & (pagesize-1); addr < (ebda & (pagesize-1)) + 1024; addr += 16)
                    {
                      if (check_rsdp((struct acpi_rsdp *)((unsigned char *)map + addr)))
                        {
                          rsdp_addr = (ebda & ~(pagesize-1)) + addr;
                          break;
                        }
                    }
                  if (physmem_unmap(physmem, map, 1024 + (ebda & (pagesize-1))) != 0)
                    a->debug("unmapping of EBDA failed: %s...", strerror(errno));
                }
              else
                a->debug("mapping of EBDA failed: %s...", strerror(errno));
            }
          else
            a->debug("EBDA location 0x%" PCI_U64_FMT_X " is insane...", ebda);
        }
      else
        a->debug("mapping of BDA failed: %s...", strerror(errno));


      if (rsdp_addr)
        return rsdp_addr;

      /* Scan the main BIOS area below 1 MB */
      a->debug("scanning BIOS below 1 MB...");
      map = physmem_map(physmem, 0xE0000, 0x20000, 0);
      if (map != (void *)-1)
        {
          for (addr = 0x0; addr < 0x20000; addr += 16)
            {
              if (check_rsdp((struct acpi_rsdp *)((unsigned char *)map + addr)))
                {
                  rsdp_addr = 0xE0000 + addr;
                  break;
                }
            }
          if (physmem_unmap(physmem, map, 0x20000) != 0)
            a->debug("unmapping of BIOS failed: %s...", strerror(errno));
        }
      else
        a->debug("mapping of BIOS failed: %s...", strerror(errno));

      if (rsdp_addr)
        return rsdp_addr;
    }
#endif

  return 0;
}

static struct acpi_mcfg *
find_mcfg(struct pci_access *a, const char *acpimcfg, const char *efisystab, int use_bsd, int use_x86bios)
{
  struct ecam_access *eacc = a->backend_data;
  struct physmem *physmem = eacc->physmem;
  long pagesize = eacc->pagesize;
  struct acpi_xsdt *xsdt;
  struct acpi_rsdt *rsdt;
  struct acpi_mcfg *mcfg;
  struct acpi_sdt *sdt;
  unsigned int i, count;
  u64 rsdp_address;
  u64 xsdt_address;
  u32 rsdt_address;
  void *map_addr;
  u32 map_length;
  void *map2_addr;
  u32 map2_length;
  long length;
  FILE *mcfg_file;
  const char *path;
  glob_t mcfg_glob;
  int ret;

  if (acpimcfg[0])
    {
      ret = glob(acpimcfg, GLOB_NOCHECK, NULL, &mcfg_glob);
      if (ret == 0)
        {
          path = mcfg_glob.gl_pathv[0];
          a->debug("reading acpi mcfg file: %s...", path);
          mcfg_file = fopen(path, "rb");
          globfree(&mcfg_glob);
          if (mcfg_file)
            {
              if (fseek(mcfg_file, 0, SEEK_END) == 0)
                length = ftell(mcfg_file);
              else
                length = -1;
              if (length > 0 && (size_t)length > sizeof(*mcfg))
                {
                  rewind(mcfg_file);
                  mcfg = pci_malloc(a, length);
                  if (fread(mcfg, 1, length, mcfg_file) == (size_t)length &&
                      memcmp(mcfg->sdt.signature, "MCFG", 4) == 0 &&
                      mcfg->sdt.length <= (size_t)length &&
                      calculate_checksum((u8 *)mcfg, mcfg->sdt.length) == 0)
                    {
                      fclose(mcfg_file);
                      return mcfg;
                    }
                }
              fclose(mcfg_file);
            }
          a->debug("failed...");
        }
      else
        a->debug("glob(%s) failed: %d...", acpimcfg, ret);
    }

  a->debug("searching for ACPI RSDP...");
  rsdp_address = find_rsdp_address(a, efisystab, use_bsd, use_x86bios);
  if (!rsdp_address)
    {
      a->debug("not found...");
      return NULL;
    }
  a->debug("found at 0x%" PCI_U64_FMT_X "...", rsdp_address);

  if (!check_and_parse_rsdp(physmem, pagesize, rsdp_address, &rsdt_address, &xsdt_address))
    {
      a->debug("invalid...");
      return NULL;
    }

  mcfg = NULL;
  a->debug("searching for ACPI MCFG (XSDT=0x%" PCI_U64_FMT_X ", RSDT=0x%lx)...", xsdt_address, (unsigned long)rsdt_address);

  xsdt = xsdt_address ? (struct acpi_xsdt *)check_and_map_sdt(physmem, pagesize, xsdt_address, "XSDT", &map_addr, &map_length) : NULL;
  if (xsdt)
    {
      a->debug("via XSDT...");
      count = get_xsdt_addresses_count(xsdt);
      for (i = 0; i < count; i++)
        {
          sdt = check_and_map_sdt(physmem, pagesize, xsdt->sdt_addresses[i], "MCFG", &map2_addr, &map2_length);
          if (sdt)
            {
              mcfg = pci_malloc(a, sdt->length);
              memcpy(mcfg, sdt, sdt->length);
              physmem_unmap(physmem, map2_addr, map2_length);
              break;
            }
        }
      physmem_unmap(physmem, map_addr, map_length);
      if (mcfg)
        {
          a->debug("found...");
          return mcfg;
        }
    }

  rsdt = (struct acpi_rsdt *)check_and_map_sdt(physmem, pagesize, rsdt_address, "RSDT", &map_addr, &map_length);
  if (rsdt)
    {
      a->debug("via RSDT...");
      count = get_rsdt_addresses_count(rsdt);
      for (i = 0; i < count; i++)
        {
          sdt = check_and_map_sdt(physmem, pagesize, rsdt->sdt_addresses[i], "MCFG", &map2_addr, &map2_length);
          if (sdt)
            {
              mcfg = pci_malloc(a, sdt->length);
              memcpy(mcfg, sdt, sdt->length);
              physmem_unmap(physmem, map2_addr, map2_length);
              break;
            }
        }
      physmem_unmap(physmem, map_addr, map_length);
      if (mcfg)
        {
          a->debug("found...");
          return mcfg;
        }
    }

  a->debug("not found...");
  return NULL;
}

static void
get_mcfg_allocation(struct acpi_mcfg *mcfg, unsigned int i, int *domain, u8 *start_bus, u8 *end_bus, u64 *addr, u32 *length)
{
  int buses = (int)mcfg->allocations[i].end_bus_number - (int)mcfg->allocations[i].start_bus_number + 1;

  if (domain)
    *domain = mcfg->allocations[i].pci_segment;
  if (start_bus)
    *start_bus = mcfg->allocations[i].start_bus_number;
  if (end_bus)
    *end_bus = mcfg->allocations[i].end_bus_number;
  if (addr)
    *addr = mcfg->allocations[i].address;
  if (length)
    *length = (buses > 0) ? (buses * 32 * 8 * 4096) : 0;
}

static int
parse_next_addrs(const char *addrs, const char **next, int *domain, u8 *start_bus, u8 *end_bus, u64 *addr, u32 *length)
{
  u64 ullnum;
  const char *sep1, *sep2;
  int addr_len;
  char *endptr;
  long num;
  int buses;
  u64 start_addr;

  if (!*addrs)
    {
      if (next)
        *next = NULL;
      return 0;
    }

  endptr = strchr(addrs, ',');
  if (endptr)
    addr_len = endptr - addrs;
  else
    addr_len = strlen(addrs);

  if (next)
    *next = endptr ? (endptr+1) : NULL;

  sep1 = memchr(addrs, ':', addr_len);
  if (!sep1)
    return 0;

  sep2 = memchr(sep1+1, ':', addr_len - (sep1+1 - addrs));
  if (!sep2)
    {
      sep2 = sep1;
      sep1 = NULL;
    }

  if (!sep1)
    {
      if (domain)
        *domain = 0;
    }
  else
    {
      if (!isxdigit(*addrs))
        return 0;
      errno = 0;
      num = strtol(addrs, &endptr, 16);
      if (errno || endptr != sep1 || num < 0 || num > INT_MAX)
        return 0;
      if (domain)
        *domain = num;
    }

  errno = 0;
  num = strtol(sep1 ? (sep1+1) : addrs, &endptr, 16);
  if (errno || num < 0 || num > 0xff)
    return 0;
  if (start_bus)
    *start_bus = num;

  buses = -num;

  if (endptr != sep2)
    {
      if (*endptr != '-')
        return 0;
      errno = 0;
      num = strtol(endptr+1, &endptr, 16);
      if (errno || endptr != sep2 || num < 0 || num > 0xff)
        return 0;
      buses = num - -buses + 1;
      if (buses <= 0)
        return 0;
      if (end_bus)
        *end_bus = num;
    }

  if (!isxdigit(*(sep2+1)))
    return 0;

  errno = 0;
  ullnum = strtoull(sep2+1, &endptr, 16);
  if (errno || (ullnum & 3))
    return 0;
  if (addr)
    *addr = ullnum;
  start_addr = ullnum;

  if (endptr == addrs + addr_len)
    {
      if (buses <= 0)
        {
          buses = 0xff - -buses + 1;
          if (end_bus)
            *end_bus = 0xff;
        }
      if (start_addr + (unsigned)buses * 32 * 8 * 4096 < start_addr)
        return 0;
      if (length)
        *length = buses * 32 * 8 * 4096;
    }
  else
    {
      if (*endptr != '+' || !isxdigit(*(endptr+1)))
        return 0;
      errno = 0;
      ullnum = strtoull(endptr+1, &endptr, 16);
      if (errno || endptr != addrs + addr_len || (ullnum & 3) || ullnum > 256 * 32 * 8 * 4096)
        return 0;
      if (start_addr + ullnum < start_addr)
        return 0;
      if (buses > 0 && ullnum > (unsigned)buses * 32 * 8 * 4096)
        return 0;
      if (buses <= 0 && ullnum > (0xff - (unsigned)-buses + 1) * 32 * 8 * 4096)
        return 0;
      if (length)
        *length = ullnum;
      if (buses <= 0 && end_bus)
        *end_bus = -buses + (ullnum + 32 * 8 * 4096 - 1) / (32 * 8 * 4096);
    }

  return 1;
}

static int
validate_addrs(const char *addrs)
{
  if (!*addrs)
    return 1;

  while (addrs)
    if (!parse_next_addrs(addrs, &addrs, NULL, NULL, NULL, NULL, NULL))
      return 0;

  return 1;
}

static int
calculate_bus_addr(u8 start_bus, u64 start_addr, u32 total_length, u8 bus, u64 *addr, u32 *length)
{
  u32 offset;

  offset = 32*8*4096 * (bus - start_bus);
  if (offset >= total_length)
    return 0;

  *addr = start_addr + offset;
  *length = total_length - offset;

  if (*length > 32*8*4096)
    *length = 32*8*4096;

  return 1;
}

static int
get_bus_addr(struct acpi_mcfg *mcfg, const char *addrs, int domain, u8 bus, u64 *addr, u32 *length)
{
  int cur_domain;
  u8 start_bus;
  u8 end_bus;
  u64 start_addr;
  u32 total_length;
  int i, count;

  if (mcfg)
    {
      count = get_mcfg_allocations_count(mcfg);
      for (i = 0; i < count; i++)
        {
          get_mcfg_allocation(mcfg, i, &cur_domain, &start_bus, &end_bus, &start_addr, &total_length);
          if (domain == cur_domain && bus >= start_bus && bus <= end_bus)
            return calculate_bus_addr(start_bus, start_addr, total_length, bus, addr, length);
        }
      return 0;
    }
  else
    {
      while (addrs)
        {
          if (!parse_next_addrs(addrs, &addrs, &cur_domain, &start_bus, &end_bus, &start_addr, &total_length))
            return 0;
          if (domain == cur_domain && bus >= start_bus && bus <= end_bus)
            return calculate_bus_addr(start_bus, start_addr, total_length, bus, addr, length);
        }
      return 0;
    }
}

static void
munmap_reg(struct pci_access *a)
{
  struct ecam_access *eacc = a->backend_data;
  struct mmap_cache *cache = eacc->cache;
  struct physmem *physmem = eacc->physmem;
  long pagesize = eacc->pagesize;

  if (!cache)
    return;

  physmem_unmap(physmem, cache->map, cache->length + (cache->addr & (pagesize-1)));
  pci_mfree(cache);
  eacc->cache = NULL;
}

static int
mmap_reg(struct pci_access *a, int w, int domain, u8 bus, u8 dev, u8 func, int pos, volatile void **reg)
{
  struct ecam_access *eacc = a->backend_data;
  struct mmap_cache *cache = eacc->cache;
  struct physmem *physmem = eacc->physmem;
  long pagesize = eacc->pagesize;
  const char *addrs;
  void *map;
  u64 addr;
  u32 length;
  u32 offset;

  if (cache && cache->domain == domain && cache->bus == bus && !!cache->w == !!w)
    {
      map = cache->map;
      addr = cache->addr;
      length = cache->length;
    }
  else
    {
      addrs = pci_get_param(a, "ecam.addrs");
      if (!get_bus_addr(eacc->mcfg, addrs, domain, bus, &addr, &length))
        return 0;

      map = physmem_map(physmem, addr & ~(pagesize-1), length + (addr & (pagesize-1)), w);
      if (map == (void *)-1)
        return 0;

      if (cache)
        physmem_unmap(physmem, cache->map, cache->length + (cache->addr & (pagesize-1)));
      else
        cache = eacc->cache = pci_malloc(a, sizeof(*cache));

      cache->map = map;
      cache->addr = addr;
      cache->length = length;
      cache->domain = domain;
      cache->bus = bus;
      cache->w = w;
    }

  /*
   * Enhanced Configuration Access Mechanism (ECAM) offset according to:
   * PCI Express Base Specification, Revision 5.0, Version 1.0, Section 7.2.2, Table 7-1, p. 677
   */
  offset = ((dev & 0x1f) << 15) | ((func & 0x7) << 12) | (pos & 0xfff);

  if (offset + 4 > length)
    return 0;

  *reg = (unsigned char *)map + (addr & (pagesize-1)) + offset;
  return 1;
}

static void
ecam_config(struct pci_access *a)
{
  physmem_init_config(a);
  pci_define_param(a, "ecam.acpimcfg", PCI_PATH_ACPI_MCFG, "Path to the ACPI MCFG table");
  pci_define_param(a, "ecam.efisystab", PCI_PATH_EFI_SYSTAB, "Path to the EFI system table");
#if defined (__FreeBSD__) || defined (__DragonFly__) || defined(__NetBSD__)
  pci_define_param(a, "ecam.bsd", "1", "Use BSD kenv or sysctl to find ACPI MCFG table");
#endif
#if defined(__amd64__) || defined(__i386__)
  pci_define_param(a, "ecam.x86bios", "1", "Scan x86 BIOS memory for ACPI MCFG table");
#endif
  pci_define_param(a, "ecam.addrs", "", "Physical addresses of memory mapped PCIe ECAM interface"); /* format: [domain:]start_bus[-end_bus]:start_addr[+length],... */
}

static int
ecam_detect(struct pci_access *a)
{
  int use_addrs = 1, use_acpimcfg = 1, use_efisystab = 1, use_bsd = 1, use_x86bios = 1;
  const char *acpimcfg = pci_get_param(a, "ecam.acpimcfg");
  const char *efisystab = pci_get_param(a, "ecam.efisystab");
#if defined (__FreeBSD__) || defined (__DragonFly__) || defined(__NetBSD__)
  const char *bsd = pci_get_param(a, "ecam.bsd");
#endif
#if defined(__amd64__) || defined(__i386__)
  const char *x86bios = pci_get_param(a, "ecam.x86bios");
#endif
  const char *addrs = pci_get_param(a, "ecam.addrs");
  struct ecam_access *eacc;
  glob_t mcfg_glob;
  int ret;

  if (!*addrs)
    {
      a->debug("ecam.addrs was not specified...");
      use_addrs = 0;
    }

  if (acpimcfg[0])
    {
      ret = glob(acpimcfg, GLOB_NOCHECK, NULL, &mcfg_glob);
      if (ret == 0)
        {
          if (access(mcfg_glob.gl_pathv[0], R_OK))
            {
              a->debug("cannot access acpimcfg: %s: %s...", mcfg_glob.gl_pathv[0], strerror(errno));
              use_acpimcfg = 0;
            }
          globfree(&mcfg_glob);
        }
      else
        {
          a->debug("glob(%s) failed: %d...", acpimcfg, ret);
          use_acpimcfg = 0;
        }
    }
  else
    use_acpimcfg = 0;

  if (!efisystab[0] || access(efisystab, R_OK))
    {
      if (efisystab[0])
        a->debug("cannot access efisystab: %s: %s...", efisystab, strerror(errno));
      use_efisystab = 0;
    }

#if defined (__FreeBSD__) || defined (__DragonFly__) || defined(__NetBSD__)
  if (strcmp(bsd, "0") == 0)
    {
      a->debug("not using BSD kenv/sysctl...");
      use_bsd = 0;
    }
#else
  use_bsd = 0;
#endif

#if defined(__amd64__) || defined(__i386__)
  if (strcmp(x86bios, "0") == 0)
    {
      a->debug("not using x86 BIOS...");
      use_x86bios = 0;
    }
#else
  use_x86bios = 0;
#endif

  if (!use_addrs && !use_acpimcfg && !use_efisystab && !use_bsd && !use_x86bios)
    {
      a->debug("no ecam source provided");
      return 0;
    }

  if (!validate_addrs(addrs))
    {
      a->debug("ecam.addrs has invalid format %s", addrs);
      return 0;
    }

  if (physmem_access(a, 0))
    {
      a->debug("cannot access physical memory: %s", strerror(errno));
      return 0;
    }

  if (!use_addrs)
    {
      eacc = pci_malloc(a, sizeof(*eacc));

      eacc->physmem = physmem_open(a, a->writeable);
      if (!eacc->physmem)
        {
          a->debug("cannot open physcal memory: %s.", strerror(errno));
          pci_mfree(eacc);
          return 0;
        }

      eacc->pagesize = physmem_get_pagesize(eacc->physmem);
      if (eacc->pagesize <= 0)
        {
          a->debug("Cannot get page size: %s.", strerror(errno));
          physmem_close(eacc->physmem);
          pci_mfree(eacc);
          return 0;
        }

      eacc->mcfg = NULL;
      eacc->cache = NULL;
      a->backend_data = eacc;
      eacc->mcfg = find_mcfg(a, acpimcfg, efisystab, use_bsd, use_x86bios);
      if (!eacc->mcfg)
        {
          physmem_close(eacc->physmem);
          pci_mfree(eacc);
          a->backend_data = NULL;
          return 0;
        }
    }

  if (use_addrs)
    a->debug("using with ecam addresses %s", addrs);
  else
    a->debug("using with%s%s%s%s%s%s", use_acpimcfg ? " acpimcfg=" : "", use_acpimcfg ? acpimcfg : "", use_efisystab ? " efisystab=" : "", use_efisystab ? efisystab : "", use_bsd ? " bsd" : "", use_x86bios ? " x86bios" : "");

  return 1;
}

static void
ecam_init(struct pci_access *a)
{
  const char *acpimcfg = pci_get_param(a, "ecam.acpimcfg");
  const char *efisystab = pci_get_param(a, "ecam.efisystab");
#if defined (__FreeBSD__) || defined (__DragonFly__) || defined(__NetBSD__)
  const char *bsd = pci_get_param(a, "ecam.bsd");
#endif
#if defined(__amd64__) || defined(__i386__)
  const char *x86bios = pci_get_param(a, "ecam.x86bios");
#endif
  const char *addrs = pci_get_param(a, "ecam.addrs");
  struct physmem *physmem = NULL;
  struct ecam_access *eacc = a->backend_data;
  long pagesize = 0;
  int use_bsd = 0;
  int use_x86bios = 0;
  int test_domain = 0;
  u8 test_bus = 0;
  volatile void *test_reg;

  if (!validate_addrs(addrs))
    a->error("Option ecam.addrs has invalid address format \"%s\".", addrs);

  if (!eacc)
    {
      physmem = physmem_open(a, a->writeable);
      if (!physmem)
        a->error("Cannot open physcal memory: %s.", strerror(errno));

      pagesize = physmem_get_pagesize(physmem);
      if (pagesize <= 0)
        a->error("Cannot get page size: %s.", strerror(errno));

      eacc = pci_malloc(a, sizeof(*eacc));
      eacc->mcfg = NULL;
      eacc->cache = NULL;
      eacc->physmem = physmem;
      eacc->pagesize = pagesize;
      a->backend_data = eacc;
    }

  if (!*addrs)
    {
#if defined (__FreeBSD__) || defined (__DragonFly__)
      if (strcmp(bsd, "0") != 0)
        use_bsd = 1;
#endif
#if defined(__amd64__) || defined(__i386__)
      if (strcmp(x86bios, "0") != 0)
        use_x86bios = 1;
#endif
      if (!eacc->mcfg)
        eacc->mcfg = find_mcfg(a, acpimcfg, efisystab, use_bsd, use_x86bios);
      if (!eacc->mcfg)
        a->error("Option ecam.addrs was not specified and ACPI MCFG table cannot be found.");
    }

  if (eacc->mcfg)
    get_mcfg_allocation(eacc->mcfg, 0, &test_domain, &test_bus, NULL, NULL, NULL);
  else
    parse_next_addrs(addrs, NULL, &test_domain, &test_bus, NULL, NULL, NULL);

  errno = 0;
  if (!mmap_reg(a, 0, test_domain, test_bus, 0, 0, 0, &test_reg))
    a->error("Cannot map ecam region: %s.", errno ? strerror(errno) : "Unknown error");
}

static void
ecam_cleanup(struct pci_access *a)
{
  struct ecam_access *eacc = a->backend_data;

  munmap_reg(a);
  physmem_close(eacc->physmem);
  pci_mfree(eacc->mcfg);
  pci_mfree(eacc);
  a->backend_data = NULL;
}

static void
ecam_scan(struct pci_access *a)
{
  const char *addrs = pci_get_param(a, "ecam.addrs");
  struct ecam_access *eacc = a->backend_data;
  u32 *segments;
  int i, j, count;
  int domain;

  segments = pci_malloc(a, 0xFFFF/8);
  memset(segments, 0, 0xFFFF/8);

  if (eacc->mcfg)
    {
      count = get_mcfg_allocations_count(eacc->mcfg);
      for (i = 0; i < count; i++)
        segments[eacc->mcfg->allocations[i].pci_segment / 32] |= 1 << (eacc->mcfg->allocations[i].pci_segment % 32);
    }
  else
    {
      while (addrs)
        {
          if (parse_next_addrs(addrs, &addrs, &domain, NULL, NULL, NULL, NULL))
            segments[domain / 32] |= 1 << (domain % 32);
        }
    }

  for (i = 0; i < 0xFFFF/32; i++)
    {
      if (!segments[i])
        continue;
      for (j = 0; j < 32; j++)
        if (segments[i] & (1 << j))
          pci_generic_scan_domain(a, 32*i + j);
    }

  pci_mfree(segments);
}

static int
ecam_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  volatile void *reg;

  if (pos >= 4096)
    return 0;

  if (len != 1 && len != 2 && len != 4)
    return pci_generic_block_read(d, pos, buf, len);

  if (!mmap_reg(d->access, 0, d->domain, d->bus, d->dev, d->func, pos, &reg))
    return 0;

  switch (len)
    {
    case 1:
      buf[0] = physmem_readb(reg);
      break;
    case 2:
      ((u16 *) buf)[0] = physmem_readw(reg);
      break;
    case 4:
      ((u32 *) buf)[0] = physmem_readl(reg);
      break;
    }

  return 1;
}

static int
ecam_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  volatile void *reg;

  if (pos >= 4096)
    return 0;

  if (len != 1 && len != 2 && len != 4)
    return pci_generic_block_read(d, pos, buf, len);

  if (!mmap_reg(d->access, 1, d->domain, d->bus, d->dev, d->func, pos, &reg))
    return 0;

  switch (len)
    {
    case 1:
      physmem_writeb(buf[0], reg);
      break;
    case 2:
      physmem_writew(((u16 *) buf)[0], reg);
      break;
    case 4:
      physmem_writel(((u32 *) buf)[0], reg);
      break;
    }

  return 1;
}

struct pci_methods pm_ecam = {
  .name = "ecam",
  .help = "Raw memory mapped access using PCIe ECAM interface",
  .config = ecam_config,
  .detect = ecam_detect,
  .init = ecam_init,
  .cleanup = ecam_cleanup,
  .scan = ecam_scan,
  .fill_info = pci_generic_fill_info,
  .read = ecam_read,
  .write = ecam_write,
};
