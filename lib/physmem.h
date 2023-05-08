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
void *physmem_map(struct physmem *physmem, u64 addr, size_t length, int w);
int physmem_unmap(struct physmem *physmem, void *ptr, size_t length);
