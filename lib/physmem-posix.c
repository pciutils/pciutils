/*
 *      The PCI Library -- Physical memory mapping for POSIX systems
 *
 *      Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Tell 32-bit platforms that we are interested in 64-bit variant of off_t type
 * as 32-bit variant of off_t type is signed and so it cannot represent all
 * possible 32-bit offsets. It is required because off_t type is used by mmap().
 */
#define _FILE_OFFSET_BITS 64

#include "internal.h"
#include "physmem.h"

#include <limits.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef OFF_MAX
#define OFF_MAX ((((off_t)1 << (sizeof(off_t) * CHAR_BIT - 2)) - 1) * 2 + 1)
#endif

struct physmem {
  int fd;
};

void
physmem_init_config(struct pci_access *a)
{
  pci_define_param(a, "devmem.path", PCI_PATH_DEVMEM_DEVICE, "Path to the /dev/mem device");
}

int
physmem_access(struct pci_access *a, int w)
{
  const char *devmem = pci_get_param(a, "devmem.path");
  a->debug("checking access permission of physical memory device %s for %s mode...", devmem, w ? "read/write" : "read-only");
  return access(devmem, R_OK | (w ? W_OK : 0));
}

struct physmem *
physmem_open(struct pci_access *a, int w)
{
  const char *devmem = pci_get_param(a, "devmem.path");
  struct physmem *physmem = pci_malloc(a, sizeof(struct physmem));

  a->debug("trying to open physical memory device %s in %s mode...", devmem, w ? "read/write" : "read-only");
  physmem->fd = open(devmem, (w ? O_RDWR : O_RDONLY) | O_DSYNC); /* O_DSYNC bypass CPU cache for mmap() on Linux */
  if (physmem->fd < 0)
    {
      pci_mfree(physmem);
      return NULL;
    }

  return physmem;
}

void
physmem_close(struct physmem *physmem)
{
  close(physmem->fd);
  pci_mfree(physmem);
}

long
physmem_get_pagesize(struct physmem *physmem UNUSED)
{
  return sysconf(_SC_PAGESIZE);
}

void *
physmem_map(struct physmem *physmem, u64 addr, size_t length, int w)
{
  if (addr > OFF_MAX)
    {
      errno = EOVERFLOW;
      return (void *)-1;
    }
  return mmap(NULL, length, PROT_READ | (w ? PROT_WRITE : 0), MAP_SHARED, physmem->fd, addr);
}

int
physmem_unmap(struct physmem *physmem UNUSED, void *ptr, size_t length)
{
  return munmap(ptr, length);
}
