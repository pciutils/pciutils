/*
 *	The PCI Library -- System-Dependent Stuff
 *
 *	Copyright (c) 1997--2020 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#define NONRET __attribute__((noreturn))
#define FORMAT_CHECK(x,y,z) __attribute__((format(x,y,z)))
#else
#define UNUSED
#define NONRET
#define FORMAT_CHECK(x,y,z)
#define inline
#endif

typedef u8 byte;
typedef u16 word;

#ifdef PCI_OS_WINDOWS
#define strcasecmp _strcmpi
#define strncasecmp _strnicmp
#if defined(_MSC_VER) && _MSC_VER < 1800
#if _MSC_VER < 1300
#define strtoull strtoul
#else
#define strtoull _strtoui64
#endif
#endif
#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif
#endif

#ifdef PCI_HAVE_LINUX_BYTEORDER_H

#include <asm/byteorder.h>
#define cpu_to_le16 __cpu_to_le16
#define cpu_to_le32 __cpu_to_le32
#define le16_to_cpu __le16_to_cpu
#define le32_to_cpu __le32_to_cpu

#else

#ifdef PCI_OS_LINUX
#include <endian.h>
#define BYTE_ORDER __BYTE_ORDER
#define BIG_ENDIAN __BIG_ENDIAN
#endif

#ifdef PCI_OS_SUNOS
#include <sys/byteorder.h>
#if defined(__i386) && defined(LITTLE_ENDIAN)
# define BYTE_ORDER LITTLE_ENDIAN
#elif defined(__sparc) && defined(BIG_ENDIAN)
# define BYTE_ORDER BIG_ENDIAN
#else
#define BIG_ENDIAN 4321
#endif
#ifndef BYTE_ORDER
#ifdef _LITTLE_ENDIAN
#define BYTE_ORDER 1234
#else
#define BYTE_ORDER 4321
#endif
#endif /* BYTE_ORDER */
#endif /* PCI_OS_SUNOS */

#ifdef PCI_OS_WINDOWS
#ifdef __MINGW32__
  #include <sys/param.h>
#else
  #include <io.h>
  #define BIG_ENDIAN 4321
  #define LITTLE_ENDIAN	1234
  #define BYTE_ORDER LITTLE_ENDIAN
#endif
#endif

#ifdef PCI_OS_SYLIXOS
#include <endian.h>
#endif

#ifdef PCI_OS_DJGPP
  #define BIG_ENDIAN 4321
  #define LITTLE_ENDIAN	1234
  #define BYTE_ORDER LITTLE_ENDIAN
#endif

#ifdef PCI_OS_AMIGAOS
  #include <machine/endian.h>
#endif

#if !defined(BYTE_ORDER)
#error "BYTE_ORDER not defined for your platform"
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define cpu_to_le16 swab16
#define cpu_to_le32 swab32
#define le16_to_cpu swab16
#define le32_to_cpu swab32

static inline word swab16(word w)
{
  return (w << 8) | ((w >> 8) & 0xff);
}

static inline u32 swab32(u32 w)
{
  return ((w & 0xff000000) >> 24) |
         ((w & 0x00ff0000) >> 8) |
         ((w & 0x0000ff00) << 8)  |
         ((w & 0x000000ff) << 24);
}
#else
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#endif

#endif
