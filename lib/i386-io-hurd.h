/*
 *	The PCI Library -- Access to i386 I/O ports on GNU Hurd
 *
 *	Copyright (c) 2003 Marco Gerards <metgerards@student.han.nl>
 *	Copyright (c) 2003 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <sys/io.h>

static inline int
intel_setup_io(void)
{
  return 1;
}

static inline int
intel_cleanup_io(void)
{
}
