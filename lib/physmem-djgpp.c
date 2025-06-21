/*
 *      The PCI Library -- Physical memory mapping for DJGPP
 *
 *      Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "internal.h"
#include "physmem.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h> /* for __DJGPP__ and __DJGPP_MINOR__, available since DJGPP v2.02 and defined indirectly via sys/version.h */
#include <string.h> /* for ffs() */
#include <malloc.h> /* for memalign() */

#include <dpmi.h>
#include <crt0.h> /* for _crt0_startup_flags, __djgpp_memory_handle_list, __djgpp_memory_handle_size and __djgpp_memory_handle() */
#include <sys/nearptr.h> /* for __djgpp_conventional_base, __djgpp_nearptr_enable() and __djgpp_nearptr_disable() */

#ifndef EOVERFLOW
#define EOVERFLOW 40 /* defined since DJGPP v2.04 */
#endif

/*
 * For using __djgpp_conventional_base it is needed to ensure that Unix-like
 * sbrk algorithm is not active (by setting _CRT0_FLAG_NONMOVE_SBRK startup flag)
 * and avoiding to call functions like system, spawn*, or exec*.
 */
int _crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK;

static void *
aligned_alloc(size_t alignment, size_t size)
{
  /*
   * Unfortunately DJGPP prior to 2.6 has broken memalign() function,
   * so for older DJGPP versions use malloc() with manual aligning.
   */
#if !defined(__DJGPP__) || __DJGPP__ < 2 || (__DJGPP__ == 2 && __DJGPP_MINOR__ < 6)
  void *ptr_alloc, *ptr_aligned;

  if (alignment < 8)
    alignment = 8;

  ptr_alloc = malloc(size + alignment);
  if (!ptr_alloc)
    return NULL;

  ptr_aligned = (void *)(((unsigned long)ptr_alloc & ~(alignment-1)) + alignment);

  /*
   * Store original pointer from malloc() before our aligned pointer.
   * DJGPP malloc()'ed ptr_alloc is aligned to 8 bytes, our ptr_alloc is
   * aligned at least to 8 bytes, so we have always 4 bytes of free space
   * before memory where is pointing ptr_alloc.
   */
  *((unsigned long *)ptr_aligned-1) = (unsigned long)ptr_alloc;

  return ptr_aligned;
#else
  return memalign(alignment, size);
#endif
}

static void
aligned_free(void *ptr)
{
#if !defined(__DJGPP__) || __DJGPP__ < 2 || (__DJGPP__ == 2 && __DJGPP_MINOR__ < 6)
  /* Take original pointer returned by malloc() for releasing memory. */
  ptr = (void *)*((unsigned long *)ptr-1);
#endif
  free(ptr);
}

static int
find_sbrk_memory_handle(void *ptr, unsigned long max_length UNUSED /*pre-v2.04*/, unsigned long pagesize UNUSED /*pre-v2.04*/, const __djgpp_sbrk_handle **sh, unsigned long *sh_size)
{
  /*
   * Find a DJGPP's sbrk memory handle which belongs to the ptr address pointer
   * and detects size of this memory handle. DJGPP since v2.04 has arrays
   * __djgpp_memory_handle_list[] and __djgpp_memory_handle_size[] with sbrk
   * ranges which can be simple traversed. Older DJGPP versions have only
   * __djgpp_memory_handle() function which returns information to which handle
   * passed pointer belongs. So finding the size of the memory handle for DJGPP
   * pre-v2.04 version is slower, its time complexity is O(N^2).
   */
#if !defined(__DJGPP__) || __DJGPP__ < 2 || (__DJGPP__ == 2 && __DJGPP_MINOR__ < 4)

  const __djgpp_sbrk_handle *sh2;
  unsigned long end_offset;

  *sh = __djgpp_memory_handle((unsigned long)ptr);

  for (end_offset = max_length-1; end_offset != 0; end_offset = end_offset > pagesize ? end_offset - pagesize : 0)
    {
      sh2 = __djgpp_memory_handle((unsigned long)ptr + end_offset);
      if (!*sh || !sh2)
        {
          /*
           * If sh or sh2 is NULL then it is probably a memory corruption in
           * DJGPP's __djgpp_memory_handle_list[] structure.
           */
          return 0;
        }
      if ((*sh)->handle == sh2->handle)
        break;
    }

  if (end_offset == 0)
    {
      /*
       * If end page of the sh handle was not found then it is probably a memory
       * corruption in DJGPP's __djgpp_memory_handle_list[] structure.
       */
      return 0;
    }

  *sh_size = (unsigned long)ptr + end_offset+1 - (*sh)->address;
  return 1;

#else

  size_t i;

  for (i = 0; i < sizeof(__djgpp_memory_handle_list)/sizeof(__djgpp_memory_handle_list[0]) && (i == 0 || __djgpp_memory_handle_list[i].address != 0); i++)
    {
      if ((unsigned long)ptr >= __djgpp_memory_handle_list[i].address &&
          (unsigned long)ptr < __djgpp_memory_handle_list[i].address + __djgpp_memory_handle_size[i])
        break;
    }

  if ((i != 0 && __djgpp_memory_handle_list[i].address == 0) || __djgpp_memory_handle_size[i] == 0)
    {
      /*
       * If address range was not found in __djgpp_memory_handle_list[]
       * then it is probably memory corruption in this list.
       */
      return 0;
    }

  *sh = &__djgpp_memory_handle_list[i];
  *sh_size = __djgpp_memory_handle_size[i];
  return 1;

#endif
}

static int
set_and_get_page_attributes(__dpmi_meminfo *mi, short *attributes)
{
  unsigned long size;
  int error;
  size_t i;

  /* __dpmi_set_page_attributes modifies mi.size */
  size = mi->size;
  if (__dpmi_set_page_attributes(mi, attributes) != 0)
    {
      error = __dpmi_error;
      free(attributes);
      switch (error)
        {
        case 0x0000: /* Unsupported function (returned by Windows NTVDM, error number is cleared) */
        case 0x0507: /* Unsupported function (returned by DPMI 0.9 host, error number is same as DPMI function number) */
        case 0x8001: /* Unsupported function (returned by DPMI 1.0 host) */
          errno = ENOSYS;
          break;
        case 0x8010: /* Resource unavailable (DPMI host cannot allocate internal resources to complete an operation) */
        case 0x8013: /* Physical memory unavailable */
        case 0x8014: /* Backing store unavailable */
          errno = ENOMEM;
          break;
        case 0x8002: /* Invalid state (page in wrong state for request) */
        case 0x8021: /* Invalid value (illegal request in bits 0-2 of one or more page attribute words) */
        case 0x8023: /* Invalid handle (in ESI) */
        case 0x8025: /* Invalid linear address (specified range is not within specified block) */
          errno = EINVAL;
          break;
        default: /* Other unspecified error */
          errno = EACCES;
          break;
        }
      return -1;
    }
  mi->size = size;

  /* Cleanup output buffer. */
  for (i = 0; i < mi->size; i++)
    attributes[i] = 0;

  if (__dpmi_get_page_attributes(mi, attributes) != 0)
    {
      error = __dpmi_error;
      free(attributes);
      switch (error)
        {
        case 0x0000: /* Unsupported function (returned by Windows NTVDM, error number is cleared) */
        case 0x0506: /* Unsupported function (returned by DPMI 0.9 host, error number is same as DPMI function number) */
        case 0x8001: /* Unsupported function (returned by DPMI 1.0 host) */
          errno = ENOSYS;
          break;
        case 0x8010: /* Resource unavailable (DPMI host cannot allocate internal resources to complete an operation) */
          errno = ENOMEM;
          break;
        case 0x8023: /* Invalid handle (in ESI) */
        case 0x8025: /* Invalid linear address (specified range is not within specified block) */
          errno = EINVAL;
          break;
        default: /* Other unspecified error */
          errno = EACCES;
          break;
        }
      return -1;
    }

  return 0;
}

void
physmem_init_config(struct pci_access *a)
{
  pci_define_param(a, "devmem.path", "auto", "DJGPP physical memory access method: auto, devmap, physmap");
}

int
physmem_access(struct pci_access *a UNUSED, int w UNUSED)
{
  return 0;
}

#define PHYSMEM_DEVICE_MAPPING ((struct physmem *)1)
#define PHYSMEM_PHYSADDR_MAPPING ((struct physmem *)2)

static int fat_ds_count;

struct physmem *
physmem_open(struct pci_access *a, int w UNUSED)
{
  const char *devmem = pci_get_param(a, "devmem.path");
  __dpmi_version_ret version;
  char vendor[128];
  int capabilities;
  int try_devmap;
  int try_physmap;
  int ret;

  if (strcmp(devmem, "auto") == 0)
    {
      try_devmap = 1;
      try_physmap = 1;
    }
  else if (strcmp(devmem, "devmap") == 0)
    {
      try_devmap = 1;
      try_physmap = 0;
    }
  else if (strcmp(devmem, "physmap") == 0)
    {
      try_devmap = 0;
      try_physmap = 1;
    }
  else
    {
      try_devmap = 0;
      try_physmap = 0;
    }

  ret = __dpmi_get_version(&version);
  if (ret != 0)
    a->debug("detected unknown DPMI host...");
  else
    {
      /*
       * Call DPMI 1.0 function __dpmi_get_capabilities() for detecting if DPMI
       * host supports Device mapping. Some DPMI 0.9 hosts like Windows's NTVDM
       * do not support this function, so does not fill capabilities and vendor
       * buffer, but returns success. Detect this kind of failure by checking
       * if AX register (low 16-bits of capabilities variable) was not modified
       * and contains the number of called DPMI function (0x0401).
       */
      vendor[0] = vendor[1] = vendor[2] = 0;
      ret = __dpmi_get_capabilities(&capabilities, vendor);
      if (ret == 0 && (capabilities & 0xffff) == 0x0401)
        ret = -1;

      if (ret == 0)
        a->debug("detected DPMI %u.%02u host %.126s %u.%u with flags 0x%x and capabilities 0x%x...",
                  (unsigned)version.major, (unsigned)version.minor, vendor+2,
                  (unsigned)(unsigned char)vendor[0], (unsigned)(unsigned char)vendor[1],
                  (unsigned)version.flags, capabilities);
      else
        a->debug("detected DPMI %u.%02u host with flags 0x%x...",
                  (unsigned)version.major, (unsigned)version.minor, (unsigned)version.flags);
    }

  /*
   * If device mapping was selected then use __dpmi_map_device_in_memory_block()
   * for physical memory mapping. Does not have to be supported by DPMI 0.9 host.
   * Device mapping is supported when capability bit 2 is set.
   */
  if (try_devmap)
    {
      if (ret == 0 && (capabilities & (1<<2)))
        {
          a->debug("using physical memory access via Device Mapping...");
          return PHYSMEM_DEVICE_MAPPING;
        }
      a->debug("DPMI Device Mapping not supported...");
    }

  /*
   * If device mapping was not tried or not supported by DPMI host then fallback
   * to __dpmi_physical_address_mapping(). But this requires Fat DS descriptor,
   * meaning to increase DS descriptor limit to 4 GB, which does not have to be
   * supported by some DPMI hosts.
   */
  if (try_physmap)
    {
      if (fat_ds_count != 0 || __djgpp_nearptr_enable())
        {
          fat_ds_count++;
          a->debug("using physical memory access via Physical Address Mapping...");
          return PHYSMEM_PHYSADDR_MAPPING;
        }

      /*
       * DJGPP prior to 2.6 has semi-broken __djgpp_nearptr_enable() function.
       * On failure it may let DS descriptor limit in semi-broken state. So for
       * older DJGPP versions call __djgpp_nearptr_disable() which fixes it.
       */
#if !defined(__DJGPP__) || __DJGPP__ < 2 || (__DJGPP__ == 2 && __DJGPP_MINOR__ < 6)
      __djgpp_nearptr_disable();
#endif
      a->debug("DPMI Physical Address Mapping not usable because Fat DS descriptor not supported...");
    }

  /*
   * Otherwise we do not have access to physical memory mapping. Theoretically
   * it could be possible to use __dpmi_physical_address_mapping() and then
   * create new segment where mapped linear address would be available, but this
   * would require to access memory in newly created segment via far pointers,
   * which is not only mess in the native 32-bit application but also these far
   * pointers are not supported by gcc. If DPMI host does not allow us to change
   * DS descriptor limit to 4 GB then it is mostly due to security reasons and
   * probably does not allow access to physical memory mapping. This applies
   * for non-DOS OS systems with integrated DPMI hosts like in Windows NT NTVDM
   * or older version of Linux dosemu.
   */
  a->debug("physical memory access not allowed...");
  errno = EACCES;
  return NULL;
}

void
physmem_close(struct physmem *physmem)
{
  /* Disable 4 GB limit on DS descriptor if it was the last user. */
  if (physmem == PHYSMEM_PHYSADDR_MAPPING)
    {
      fat_ds_count--;
      if (fat_ds_count == 0)
        __djgpp_nearptr_disable();
    }
}

long
physmem_get_pagesize(struct physmem *physmem UNUSED)
{
  static unsigned long pagesize;
  if (!pagesize)
    {
      if (__dpmi_get_page_size(&pagesize) != 0)
        pagesize = 0;
      if (pagesize & (pagesize-1))
        pagesize = 0;
      if (!pagesize)
        pagesize = 4096; /* Fallback value, the most commonly used on x86. */
    }
  return pagesize;
}

void *
physmem_map(struct physmem *physmem, u64 addr, size_t length, int w)
{
  long pagesize = physmem_get_pagesize(physmem);
  unsigned pagesize_shift = ffs(pagesize)-1;
  const __djgpp_sbrk_handle *sh;
  unsigned long sh_size;
  unsigned long size;
  __dpmi_meminfo mi;
  short *attributes;
  short one_pg_attr;
  size_t offset;
  int error;
  void *ptr;
  size_t i;

  /* Align length to page size. */
  if (length & (pagesize-1))
    length = (length & ~(pagesize-1)) + pagesize;

  /* Mapping of physical memory above 4 GB is not possible. */
  if (addr >= 0xffffffffUL || addr + length > 0xffffffffUL)
    {
      errno = EOVERFLOW;
      return (void *)-1;
    }

  if (physmem == PHYSMEM_DEVICE_MAPPING)
    {
      /*
       * __dpmi_map_device_in_memory_block() maps physical memory to any
       * page-aligned linear address for which we have DPMI memory handle. But
       * DPMI host does not have to support mapping of memory below 1 MB which
       * lies in RAM, and is not device memory.
       *
       * __djgpp_map_physical_memory() function is DJGPP wrapper around
       * __dpmi_map_device_in_memory_block() which properly handles memory
       * range that span multiple DPMI memory handles. It is common that
       * DJGPP sbrk() or malloc() allocator returns continuous memory range
       * which is backed by two or more DPMI memory handles which represents
       * consecutive memory ranges without any gap.
       *
       * __dpmi_map_conventional_memory_in_memory_block() aliases memory range
       * specified by page-aligned linear address to another page-aligned linear
       * address. This can be used for mapping memory below 1 MB which lies in
       * RAM and for which cannot be used __dpmi_map_device_in_memory_block().
       * This function calls takes (virtual) linear address as opposite of the
       * __dpmi_map_device_in_memory_block() which takes physical address.
       *
       * Unfortunately __djgpp_map_physical_memory() internally calls only
       * __djgpp_map_physical_memory() function and does not return information
       * for which memory range the call failed. So it cannot be used for
       * generic memory mapping requests.
       *
       * Also it does not return usefull errno. And even in the latest released
       * DJGPP version v2.5 this function has suboptimal implementation. Its
       * time complexity is O(N^2) (where N is number of pages).
       *
       * So do not use __djgpp_map_physical_memory() function and instead write
       * own logic handling virtual memory ranges which spans multiple DPMI
       * memory handles and manually calls __dpmi_map_device_in_memory_block()
       * or __dpmi_map_conventional_memory_in_memory_block() per every handle.
       *
       * We can easily access only linear addresses in our DS segment which
       * is managed by DJGPP sbrk allocator. So allocate page-aligned range
       * by aligned_alloc() (our wrapper around malloc()/memalign()) and then
       * for every subrange which is backed by different DPMI memory handle
       * call appropriate mapping function which correctly calculated offset
       * and length to have continuous representation of physical memory range.
       *
       * This approach has disadvantage that for each mapping it is required
       * to reserve and allocate committed memory in RAM with the size of the
       * mapping itself. This has negative impact for mappings of large sizes.
       * Unfortunately this is the only way because DJGPP sbrk allocator does
       * not have any (public) function for directly allocating uncommitted
       * memory which is not backed by the RAM. Even if DJGPP sbrk code is
       * extended for this functionality, the corresponding DPMI function
       * __dpmi_allocate_linear_memory() is DPMI 1.0 function and not widely
       * supported by DPMI hosts, even the default DJGPP's CWSDPMI does not
       * support it.
       */

      ptr = aligned_alloc(pagesize, length);
      if (!ptr)
        {
          errno = ENOMEM;
          return (void *)-1;
        }

      for (offset = 0; offset < length; offset += (mi.size << pagesize_shift))
        {
          /*
           * Find a memory handle with its size which belongs to the pointer
           * address ptr+offset. Base address and size of the memory handle
           * must be page aligned for memory mapping support.
           */
          if (!find_sbrk_memory_handle(ptr + offset, length - offset, pagesize, &sh, &sh_size) ||
              (sh->address & (pagesize-1)) || (sh_size & (pagesize-1)))
            {
              /*
               * Failure detected. If we have some partial mapping, try to undo
               * it via physmem_unmap() which also release ptr. If we do not
               * have partial mapping, just release ptr.
               */
              if (offset != 0)
                physmem_unmap(physmem, ptr, offset);
              else
                aligned_free(ptr);
              errno = EINVAL;
              return (void *)-1;
            }

          mi.handle = sh->handle;
          mi.address = (unsigned long)ptr + offset - sh->address;
          mi.size = (length - offset) >> pagesize_shift;
          if (mi.size > ((sh_size - mi.address) >> pagesize_shift))
            mi.size = (sh_size - mi.address) >> pagesize_shift;
          if (__dpmi_map_device_in_memory_block(&mi, addr + offset) != 0)
            {
              /*
               * __dpmi_map_device_in_memory_block() may fail for memory range
               * which belongs to non-device memory below 1 MB. DPMI host in
               * this case returns DPMI error code 0x8003 (System integrity -
               * invalid device address). For example this is behavior of DPMI
               * host HX HDPMI32, which strictly differs between non-device and
               * device memory. If the physical memory range belongs to the
               * non-device conventional memory and DPMI host uses 1:1 mappings
               * for memory below 1 MB then we can try to alias range of linear
               * address below 1 MB to DJGPP's accessible linear address range.
               * For this aliasing of linear (not the physical) memory address
               * ranges below 1 MB boundary is there an additional DPMI 1.0
               * function __dpmi_map_conventional_memory_in_memory_block().
               * But DPMI host does not have to support it. HDPMI32 supports it.
               * If the memory range crosses 1 MB boundary then call it only for
               * the subrange of memory which below 1 MB boundary and let the
               * remaining subrange for the next iteration of the outer loop.
               * Because the remaining memory range is above 1 MB limit, only
               * the __dpmi_map_device_in_memory_block() would be used. This
               * approach makes continues linear range of the mapped memory.
               */
              if (__dpmi_error == 0x8003 && addr + offset < 1*1024*1024UL)
                {
                  /* reuse mi */
                  if (addr + offset + (mi.size << pagesize_shift) > 1*1024*1024UL)
                    mi.size = (1*1024*1024UL - addr - offset) >> pagesize_shift;
                  if (__dpmi_map_conventional_memory_in_memory_block(&mi, addr + offset) != 0)
                    {
                      /*
                       * Save __dpmi_error because any DJGPP function may change
                       * it. If we have some partial mapping, try to undo it via
                       * physmem_unmap() which also release ptr. If we do not
                       * have partial mapping, just release ptr.
                       */
                      error = __dpmi_error;
                      if (offset != 0)
                        physmem_unmap(physmem, ptr, offset);
                      else
                        aligned_free(ptr);
                      switch (error)
                        {
                        case 0x0000: /* Unsupported function (returned by Windows NTVDM, error number is cleared) */
                        case 0x0509: /* Unsupported function (returned by DPMI 0.9 host, error number is same as DPMI function number) */
                        case 0x8001: /* Unsupported function (returned by DPMI 1.0 host) */
                          /*
                           * Conventional Memory Mapping is not supported.
                           * Device Mapping is supported, but DPMI host rejected
                           * Device Mapping request. So reports same errno value
                           * as from the failed Device Mapping switch case,
                           * which is ENXIO (because __dpmi_error == 0x8003).
                           */
                          errno = ENXIO;
                          break;
                        case 0x8003: /* System integrity (invalid conventional memory address) */
                          errno = ENXIO;
                          break;
                        case 0x8010: /* Resource unavailable (DPMI host cannot allocate internal resources to complete an operation) */
                          errno = ENOMEM;
                          break;
                        case 0x8023: /* Invalid handle (in ESI) */
                        case 0x8025: /* Invalid linear address (specified range is not within specified block, or EBX/EDX is not page aligned) */
                          errno = EINVAL;
                          break;
                        default: /* Other unspecified error */
                          errno = EACCES;
                          break;
                        }
                      return (void *)-1;
                    }
                }
              else
                {
                  /*
                   * Save __dpmi_error because any DJGPP function may change
                   * it. If we have some partial mapping, try to undo it via
                   * physmem_unmap() which also release ptr. If we do not
                   * have partial mapping, just release ptr.
                   */
                  error = __dpmi_error;
                  if (offset != 0)
                    physmem_unmap(physmem, ptr, offset);
                  else
                    aligned_free(ptr);
                  switch (error)
                    {
                    case 0x0000: /* Unsupported function (returned by Windows NTVDM, error number is cleared) */
                    case 0x0508: /* Unsupported function (returned by DPMI 0.9 host, error number is same as DPMI function number) */
                    case 0x8001: /* Unsupported function (returned by DPMI 1.0 host) */
                      errno = ENOSYS;
                      break;
                    case 0x8003: /* System integrity (invalid device address) */
                      errno = ENXIO;
                      break;
                    case 0x8010: /* Resource unavailable (DPMI host cannot allocate internal resources to complete an operation) */
                      errno = ENOMEM;
                      break;
                    case 0x8023: /* Invalid handle (in ESI) */
                    case 0x8025: /* Invalid linear address (specified range is not within specified block or EBX/EDX is not page-aligned) */
                      errno = EINVAL;
                      break;
                    default: /* Other unspecified error */
                      errno = EACCES;
                      break;
                    }
                  return (void *)-1;
                }
            }

          /*
           * For read-only mapping try to change page attributes with not changing
           * page type (3) and setting read-only access (bit 3 unset). Ignore any
           * failure as this function requires DPMI 1.0 host and so it does not have
           * to be supported by other DPMI 0.9 hosts. Note that by default newly
           * created mapping has read/write access and so we can use it also for
           * mappings which were requested as read-only too.
           */
          if (!w)
            {
              attributes = malloc(mi.size * sizeof(*attributes));
              if (attributes)
                {
                  /* reuse mi */
                  for (i = 0; i < mi.size; i++)
                    attributes[i] = (0<<3) | 3;

                  /* __dpmi_set_page_attributes modifies mi.size */
                  size = mi.size;
                  __dpmi_set_page_attributes(&mi, attributes);
                  mi.size = size;

                  free(attributes);
                }
            }
        }

      return ptr;
    }
  else if (physmem == PHYSMEM_PHYSADDR_MAPPING)
    {
      /*
       * __dpmi_physical_address_mapping() is DPMI 0.9 function and so does not
       * require device mapping support. But DPMI hosts often allow to used it
       * only for memory above 1 MB and also we do not have control where DPMI
       * host maps physical memory. Because this is DPMI 0.9 function, error
       * code on failure does not have to be provided. If DPMI host does not
       * provide error code then in __dpmi_error variable is stored the called
       * DPMI function number (0x0800 is for Physical Address Mapping).
       * Error codes are provided only by DPMI 1.0 hosts.
       */

      mi.address = addr;
      mi.size = length;
      if (__dpmi_physical_address_mapping(&mi) != 0)
        {
          /*
           * __dpmi_physical_address_mapping() may fail for memory range which
           * starts below 1 MB. DPMI 1.0 host in this case returns DPMI error
           * code 0x8021 (Invalid value - address is below 1 MB boundary).
           * DPMI 0.9 host does not provide error code, so __dpmi_error contains
           * value 0x0800. For example this is behavior of the default DJGPP's
           * DPMI host CWSDPMI and also of Windows 3.x DPMI host. On the other
           * hand DPMI host HX HDPMI32 or Windows 9x DPMI host allow requests
           * for memory ranges below 1 MB and do not fail.
           */
          if ((__dpmi_error == 0x0800 || __dpmi_error == 0x8021) && addr < 1*1024*1024UL)
            {
              /*
               * Expects that conventional memory below 1 MB is always 1:1
               * mapped. On non-paging DPMI hosts it is always truth and paging
               * DPMI hosts should do it too or at least provide mapping with
               * compatible or emulated content for compatibility with existing
               * DOS applications. So check that requested range is below 1 MB.
               */
              if (addr + length > 1*1024*1024UL)
                {
                  errno = ENXIO;
                  return (void *)-1;
                }

              /*
               * Simulate successful __dpmi_physical_address_mapping() call by
               * setting the 1:1 mapped address.
               */
              mi.address = addr;
            }
          else
            {
              switch (__dpmi_error)
                {
                case 0x0800: /* Error code was not provided (returned by DPMI 0.9 host, error number is same as DPMI function number) */
                  errno = EACCES;
                  break;
                case 0x8003: /* System integrity (DPMI host memory region) */
                case 0x8021: /* Invalid value (address is below 1 MB boundary) */
                  errno = ENXIO;
                  break;
                case 0x8010: /* Resource unavailable (DPMI host cannot allocate internal resources to complete an operation) */
                  errno = ENOMEM;
                  break;
                default: /* Other unspecified error */
                  errno = EACCES;
                  break;
                }
              return (void *)-1;
            }
        }

      /*
       * Function returns linear address of the mapping. On non-paging DPMI
       * hosts it does nothing and just returns same passed physical address.
       * With DS descriptor limit set to 4 GB (set by __djgpp_nearptr_enable())
       * we have direct access to any linear address. Direct access to specified
       * linear address is from the __djgpp_conventional_base offset. Note that
       * this is always read/write access, and there is no way to make access
       * just read-only.
       */
      ptr = (void *)(mi.address + __djgpp_conventional_base);

      /*
       * DJGPP CRT code on paging DPMI hosts enables NULL pointer protection by
       * disabling access to the zero page. If we are running on DPMI host which
       * does 1:1 mapping and we were asked for physical address range mapping
       * which includes also our zero page, then we have to disable NULL pointer
       * protection to allow access to that mapped page. Detect this by checking
       * that our zero page [0, pagesize-1] does not conflict with the returned
       * address range [ptr, ptr+length] (note that length is already multiply
       * of pagesize) and change page attributes to committed page type (1) and
       * set read/write access (bit 3 set). Ignore any failure as this function
       * requires DPMI 1.0 host and so it does not have to be supported by other
       * DPMI 0.9 hosts. In this case DJGPP CRT code did not enable NULL pointer
       * protection and so zero page can be normally accessed.
       */
      if ((unsigned long)ptr - 1 > (unsigned long)ptr - 1 + length)
        {
          mi.handle = __djgpp_memory_handle_list[0].handle;
          mi.address = 0;
          mi.size = 1; /* number of pages */
          one_pg_attr = (1<<3) | 1;
          /* __dpmi_set_page_attributes modifies mi.size */
          __dpmi_set_page_attributes(&mi, &one_pg_attr);
        }

      return ptr;
    }

  /* invalid physmem parameter */
  errno = EBADF;
  return (void *)-1;
}

int
physmem_unmap(struct physmem *physmem, void *ptr, size_t length)
{
  long pagesize = physmem_get_pagesize(physmem);
  unsigned pagesize_shift = ffs(pagesize)-1;
  const __djgpp_sbrk_handle *sh;
  unsigned long sh_size;
  __dpmi_meminfo mi;
  short *attributes;
  size_t offset;
  size_t i;

  /* Align length to page size. */
  if (length & (pagesize-1))
    length = (length & ~(pagesize-1)) + pagesize;

  if (physmem == PHYSMEM_DEVICE_MAPPING)
    {
      /*
       * Memory mapped by __dpmi_map_conventional_memory_in_memory_block() or by
       * __dpmi_map_device_in_memory_block() can be unmapped by changing page
       * attributes back to the what allocator use: page type to committed (1),
       * access to read/write (bit 3 set) and not setting initial page access
       * and dirty bits (bit 4 unset).
       *
       * There is a DJGPP function __djgpp_set_page_attributes() which sets page
       * attributes for the memory range specified by ptr pointer, but it has
       * same disadvantages as __djgpp_map_physical_memory() function (see
       * comment in map functionality). So use __dpmi_set_page_attributes()
       * instead.
       *
       * If changing page attributes fails then do not return memory back to the
       * malloc pool because it is still mapped to physical memory and cannot be
       * used by allocator for general purpose anymore.
       *
       * Some DPMI hosts like HDPMI pre-v3.22 (part of HX pre-v2.22) or DPMIONE
       * do not support changing page type directly from mapped to committed.
       * But they support changing it indirectly: first from mapped to uncommitted
       * and then from uncommitted to committed. So if direct change from mapped
       * to committed fails then try workaround via indirect change.
       */

      static int do_indirect_change = 0;

      for (offset = 0; offset < length; offset += (mi.size << pagesize_shift))
        {
          /*
           * Find a memory handle with its size which belongs to the pointer
           * address ptr+offset. Base address and size of the memory handle
           * must be page aligned for changing page attributes.
           */
          if (!find_sbrk_memory_handle(ptr + offset, length - offset, pagesize, &sh, &sh_size) ||
              (sh->address & (pagesize-1)) || (sh_size & (pagesize-1)))
            {
              errno = EINVAL;
              return -1;
            }

          mi.handle = sh->handle;
          mi.address = (unsigned long)ptr + offset - sh->address;
          mi.size = (length - offset) >> pagesize_shift;
          if (mi.size > ((sh_size - mi.address) >> pagesize_shift))
            mi.size = (sh_size - mi.address) >> pagesize_shift;

          attributes = malloc(mi.size * sizeof(*attributes));
          if (!attributes)
            {
              errno = ENOMEM;
              return -1;
            }

retry_via_indirect_change:
          if (do_indirect_change)
            {
              for (i = 0; i < mi.size; i++)
                attributes[i] = (0<<4) | (0<<3) | 0; /* 0 = page type uncommitted */

              if (set_and_get_page_attributes(&mi, attributes) != 0)
                return -1;

              for (i = 0; i < mi.size; i++)
                {
                  /* Check that every page type is uncommitted (0). */
                  if ((attributes[i] & 0x7) != 0)
                    {
                      free(attributes);
                      errno = EACCES;
                      return -1;
                    }
                }
            }

          for (i = 0; i < mi.size; i++)
            attributes[i] = (0<<4) | (1<<3) | 1; /* 1 = page type committed */

          if (set_and_get_page_attributes(&mi, attributes) != 0)
            return -1;

          for (i = 0; i < mi.size; i++)
            {
              /* Check that every page type is committed (1) and has read/write access (bit 3 set). */
              if (((attributes[i] & 0x7) != 1) || !(attributes[i] & (1<<3)))
                {
                  if (!do_indirect_change)
                    {
                      /*
                       * Some DPMI hosts do not support changing page type
                       * from mapped to committed but for such change request
                       * do not report any error. Try following workaround:
                       * Change page type indirectly. First change page type
                       * from mapped to uncommitted and then to committed.
                       */
                      do_indirect_change = 1;
                      goto retry_via_indirect_change;
                    }
                  free(attributes);
                  errno = EACCES;
                  return -1;
                }
            }

          free(attributes);
        }

      /*
       * Now we are sure that ptr is backed by committed memory which can be
       * returned back to the DJGPP sbrk pool.
       */
      aligned_free(ptr);
      return 0;
    }
  else if (physmem == PHYSMEM_PHYSADDR_MAPPING)
    {
      /*
       * Physical address mapping done by __dpmi_physical_address_mapping() can
       * be unmapped only by __dpmi_free_physical_address_mapping() function.
       * This function takes linear address of the mapped region. Direct access
       * pointer refers to linear address from the __djgpp_conventional_base
       * offset. On non-paging DPMI hosts, physical memory cannot be unmapped at
       * all because whole physical memory is always available and so this
       * function either fails or does nothing. Moreover this unmapping function
       * requires DPMI 1.0 host as opposite of the mapping function which is
       * available also in DPMI 0.9. It means that DPMI 0.9 hosts do not provide
       * ability to unmap already mapped physical addresses. This DPMI unmapping
       * function is not commonly supported by DPMI hosts, even the default
       * DJGPP's CWSDPMI does not support it. But few alternative DPMI host like
       * PMODE/DJ, WDOSX, HDPMI32 or DPMIONE support it. So expects failure from
       * this function call, in most cases it is not possible to unmap physical
       * memory which was previously mapped by __dpmi_physical_address_mapping().
       */
      mi.address = (unsigned long)ptr - __djgpp_conventional_base;
      if (__dpmi_free_physical_address_mapping(&mi) != 0)
        {
          /*
           * Do not report error when DPMI function failed with error code
           * 0x8025 (invalid linear address) and linear address is below 1 MB.
           * First 1 MB of memory space should stay always mapped.
           */
          if (__dpmi_error != 0x8025 || mi.address >= 1*1024*1024UL)
            {
              switch (__dpmi_error)
                {
                case 0x0000: /* Unsupported function (returned by Windows NTVDM, error number is cleared) */
                case 0x0801: /* Unsupported function (returned by DPMI 0.9 host, error number is same as DPMI function number) */
                case 0x8001: /* Unsupported function (returned by DPMI 1.0 host) */
                  errno = ENOSYS;
                  break;
                case 0x8010: /* Resource unavailable (DPMI host cannot allocate internal resources to complete an operation) */
                  errno = ENOMEM;
                  break;
                case 0x8025: /* Invalid linear address */
                  errno = EINVAL;
                  break;
                default: /* Other unspecified error */
                  errno = EACCES;
                  break;
                }
              return -1;
            }
        }

      return 0;
    }

  /* invalid physmem parameter */
  errno = EBADF;
  return -1;
}
