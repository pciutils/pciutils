/*
 *	The PCI Utilities -- Decode bits and bit fields
 *
 *	Copyright (c) 2023 Martin Mares <mj@ucw.cz>
 *	Copyright (c) 2023 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _BITOPS_H
#define _BITOPS_H

#ifndef _PCI_LIB_H
#error Import only from pci.h
#endif

/* Useful macros for decoding of bits and bit fields */

#define FLAG(x, y) ((x & y) ? '+' : '-')

// Generate mask

#define BIT(at) ((u64)1 << (at))
// Boundaries inclusive
#define MASK(h, l)   ((((u64)1 << ((h) + 1)) - 1) & ~(((u64)1 << (l)) - 1))

// Get/set from register

#define BITS(x, at, width)      (((x) >> (at)) & ((1 << (width)) - 1))
#define GET_REG_MASK(reg, mask) (((reg) & (mask)) / ((mask) & ~((mask) << 1)))
#define SET_REG_MASK(reg, mask, val)                                                               \
  (((reg) & ~(mask)) | (((val) * ((mask) & ~((mask) << 1))) & (mask)))

#define TABLE(tab, x, buf)                                                                         \
  ((x) < sizeof(tab) / sizeof((tab)[0]) ? (tab)[x] : (sprintf((buf), "??%d", (x)), (buf)))

#endif
