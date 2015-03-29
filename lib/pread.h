/*
 *	The PCI Library -- Portable interface to pread() and pwrite()
 *
 *	Copyright (c) 1997--2003 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/*
 *  We'd like to use pread/pwrite for configuration space accesses, but
 *  unfortunately it isn't simple at all since all libc's until glibc 2.1
 *  don't define it.
 */

#if defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ > 0
/* glibc 2.1 or newer -> pread/pwrite supported automatically */

#elif defined(i386) && defined(__GLIBC__)
/* glibc 2.0 on i386 -> call syscalls directly */
#include <asm/unistd.h>
#include <syscall-list.h>
#ifndef SYS_pread
#define SYS_pread 180
#endif
static int pread(unsigned int fd, void *buf, size_t size, loff_t where)
{ return syscall(SYS_pread, fd, buf, size, where); }
#ifndef SYS_pwrite
#define SYS_pwrite 181
#endif
static int pwrite(unsigned int fd, void *buf, size_t size, loff_t where)
{ return syscall(SYS_pwrite, fd, buf, size, where); }

#else
/* In all other cases we use lseek/read/write instead to be safe */
#define make_rw_glue(op) \
	static int do_##op(struct pci_dev *d, int fd, void *buf, size_t size, int where)	\
	{											\
	  struct pci_access *a = d->access;							\
	  int r;										\
	  if (a->fd_pos != where && lseek(fd, where, SEEK_SET) < 0)				\
	    return -1;										\
	  r = op(fd, buf, size);								\
	  if (r < 0)										\
	    a->fd_pos = -1;									\
	  else											\
	    a->fd_pos = where + r;								\
	  return r;										\
	}
make_rw_glue(read)
make_rw_glue(write)
#define PCI_HAVE_DO_READ
#endif

#ifndef PCI_HAVE_DO_READ
#define do_read(d,f,b,l,p) pread(f,b,l,p)
#define do_write(d,f,b,l,p) pwrite(f,b,l,p)
#endif
