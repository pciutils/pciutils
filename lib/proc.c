/*
 *	$Id: proc.c,v 1.8 2000/04/21 11:58:00 mj Exp $
 *
 *	The PCI Library -- Configuration Access via /proc/bus/pci
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>

#include "internal.h"

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

#elif defined(i386)
/* old libc on i386 -> call syscalls directly the old way */
#include <asm/unistd.h>
static _syscall5(int, pread, unsigned int, fd, void *, buf, size_t, size, u32, where_lo, u32, where_hi);
static _syscall5(int, pwrite, unsigned int, fd, void *, buf, size_t, size, u32, where_lo, u32, where_hi);
static int do_read(struct pci_dev *d UNUSED, int fd, void *buf, size_t size, int where) { return pread(fd, buf, size, where, 0); }
static int do_write(struct pci_dev *d UNUSED, int fd, void *buf, size_t size, int where) { return pwrite(fd, buf, size, where, 0); }
#define HAVE_DO_READ

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
#define HAVE_DO_READ
#endif

#ifndef HAVE_DO_READ
#define do_read(d,f,b,l,p) pread(f,b,l,p)
#define do_write(d,f,b,l,p) pwrite(f,b,l,p)
#endif

static void
proc_config(struct pci_access *a)
{
  a->method_params[PCI_ACCESS_PROC_BUS_PCI] = PATH_PROC_BUS_PCI;
}

static int
proc_detect(struct pci_access *a)
{
  char *name = a->method_params[PCI_ACCESS_PROC_BUS_PCI];

  if (access(name, R_OK))
    {
      a->warning("Cannot open %s", name);
      return 0;
    }
  a->debug("...using %s", name);
  return 1;
}

static void
proc_init(struct pci_access *a)
{
  a->fd = -1;
}

static void
proc_cleanup(struct pci_access *a)
{
  if (a->fd >= 0)
    {
      close(a->fd);
      a->fd = -1;
    }
}

static void
proc_scan(struct pci_access *a)
{
  FILE *f;
  char buf[512];

  if (snprintf(buf, sizeof(buf), "%s/devices", a->method_params[PCI_ACCESS_PROC_BUS_PCI]) == sizeof(buf))
    a->error("File name too long");
  f = fopen(buf, "r");
  if (!f)
    a->error("Cannot open %s", buf);
  while (fgets(buf, sizeof(buf)-1, f))
    {
      struct pci_dev *d = pci_alloc_dev(a);
      unsigned int dfn, vend, cnt, known;

      cnt = sscanf(buf,
#ifdef HAVE_LONG_ADDRESS
	     "%x %x %x %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx",
#else
	     "%x %x %x %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx",
#endif
	     &dfn,
	     &vend,
	     &d->irq,
	     &d->base_addr[0],
	     &d->base_addr[1],
	     &d->base_addr[2],
	     &d->base_addr[3],
	     &d->base_addr[4],
	     &d->base_addr[5],
	     &d->rom_base_addr,
	     &d->size[0],
	     &d->size[1],
	     &d->size[2],
	     &d->size[3],
	     &d->size[4],
	     &d->size[5],
	     &d->rom_size);
      if (cnt != 9 && cnt != 10 && cnt != 17)
	a->error("proc: parse error (read only %d items)", cnt);
      d->bus = dfn >> 8U;
      d->dev = PCI_SLOT(dfn & 0xff);
      d->func = PCI_FUNC(dfn & 0xff);
      d->vendor_id = vend >> 16U;
      d->device_id = vend & 0xffff;
      known = PCI_FILL_IDENT;
      if (!a->buscentric)
	{
	  known |= PCI_FILL_IRQ | PCI_FILL_BASES;
	  if (cnt >= 10)
	    known |= PCI_FILL_ROM_BASE;
	  if (cnt >= 17)
	    known |= PCI_FILL_SIZES;
	}
      d->known_fields = known;
      pci_link_dev(a, d);
    }
  fclose(f);
}

static int
proc_setup(struct pci_dev *d, int rw)
{
  struct pci_access *a = d->access;

  if (a->cached_dev != d || a->fd_rw < rw)
    {
      char buf[256];
      if (a->fd >= 0)
	close(a->fd);
      if (snprintf(buf, sizeof(buf), "%s/%02x/%02x.%d", a->method_params[PCI_ACCESS_PROC_BUS_PCI],
		   d->bus, d->dev, d->func) == sizeof(buf))
	a->error("File name too long");
      a->fd_rw = a->writeable || rw;
      a->fd = open(buf, a->fd_rw ? O_RDWR : O_RDONLY);
      if (a->fd < 0)
	a->warning("Cannot open %s", buf);
      a->cached_dev = d;
      a->fd_pos = 0;
    }
  return a->fd;
}

static int
proc_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = proc_setup(d, 0);
  int res;

  if (fd < 0)
    return 0;
  res = do_read(d, fd, buf, len, pos);
  if (res < 0)
    {
      d->access->warning("proc_read: read failed: %s", strerror(errno));
      return 0;
    }
  else if (res != len)
    {
      d->access->warning("proc_read: tried to read %d bytes at %d, but got only %d", len, pos, res);
      return 0;
    }
  return 1;
}

static int
proc_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  int fd = proc_setup(d, 1);
  int res;

  if (fd < 0)
    return 0;
  res = do_write(d, fd, buf, len, pos);
  if (res < 0)
    {
      d->access->warning("proc_write: write failed: %s", strerror(errno));
      return 0;
    }
  else if (res != len)
    {
      d->access->warning("proc_write: tried to write %d bytes at %d, but got only %d", len, pos, res);
      return 0;
    }
  return 1;
}

static void
proc_cleanup_dev(struct pci_dev *d)
{
  if (d->access->cached_dev == d)
    d->access->cached_dev = NULL;
}

struct pci_methods pm_linux_proc = {
  "/proc/bus/pci",
  proc_config,
  proc_detect,
  proc_init,
  proc_cleanup,
  proc_scan,
  pci_generic_fill_info,
  proc_read,
  proc_write,
  NULL,					/* init_dev */
  proc_cleanup_dev
};
