/*
 *	The PCI Library -- Access to i386 I/O ports on Linux
 *
 *	Copyright (c) 1997--2003 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifdef __GLIBC__
#include <sys/io.h>
#else
#include <asm/io.h>
#endif

static int intel_iopl_set = -1;

static int
intel_setup_io(void)
{
  if (intel_iopl_set < 0)
    intel_iopl_set = (iopl(3) < 0) ? 0 : 1;
  return intel_iopl_set;
}

static inline void
intel_cleanup_io(void)
{
  if (intel_iopl_set > 0)
    iopl(3);
  intel_iopl_set = -1;
}
