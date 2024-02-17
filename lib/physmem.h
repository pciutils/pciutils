/*
 *      The PCI Library -- Physical memory mapping API
 *
 *      Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

struct physmem;

void physmem_init_config(struct pci_access *a);
int physmem_access(struct pci_access *a, int w);
struct physmem *physmem_open(struct pci_access *a, int w);
void physmem_close(struct physmem *physmem);
long physmem_get_pagesize(struct physmem *physmem);

/*
 * physmem_map returns ptr on success, (void *)-1 on error and sets errno compatible with POSIX mmap():
 * - EBADF - invalid physmem argument
 * - EINVAL - invalid or unaligned addr argument
 * - EACCES - write access requested but physmem was opened without write access
 * - ENOSYS - physmem argument does not support physical address mapping at all
 * - ENXIO - addr/length range was rejected by system (e.g. range not accessible or not available)
 * - EOVERFLOW - addr/length range is out of the physical address space (e.g. does not fit into signed 32-bit off_t type on 32-bit systems)
 * - EACCES - generic unknown error for djgpp and windows
 */
void *physmem_map(struct physmem *physmem, u64 addr, size_t length, int w);

/*
 * Unmap physical memory mapping, ptr and length must be exactly same as for physmem_map(), unmapping just subrange is not allowed.
 * physmem_unmap returns 0 on success, -1 on error and sets errno:
 * - EBADF - invalid physmem argument
 * - EINVAL - invalid ptr/length argument
 * - EPERM - ptr/length range cannot be unmapped due to access permission checks (e.g. page marked as immutable)
 * - ENOSYS - physmem argument does not support physical address unmapping at all (e.g. every mapping stays active until application terminates)
 * - EACCES - generic unknown error for djgpp and windows
 */
int physmem_unmap(struct physmem *physmem, void *ptr, size_t length);
