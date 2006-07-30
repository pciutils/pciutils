/*
 *	The PCI Library -- Access to i386 I/O ports on GNU Hurd
 *
 *	Copyright (c) 2003 Marco Gerards <metgerards@student.han.nl>
 *	Copyright (c) 2003 Martin Mares <mj@ucw.cz>
 *	Copyright (c) 2006 Samuel Thibault <samuel.thibault@ens-lyon.org> and
 *	                   Thomas Schwinge <tschwinge@gnu.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <sys/io.h>

#include <mach/machine/mach_i386.h>
#include <device/device.h>
#include <hurd.h>

#include <stdio.h>

static mach_port_t io_port;

static inline int
intel_setup_io(struct pci_access *a)
{
  mach_port_t device;

  if ((errno = get_privileged_ports(NULL, &device)))
    a->warn("i386-io-hurd: Can't get_privileged_ports(): %m");

  if (!errno && (errno = device_open(device, D_READ | D_WRITE, "io", &io_port)))
    a->warn("i386-io-hurd: Can't device_open(): %m");

  mach_port_deallocate(mach_task_self(), device);

  if (!errno && (errno = i386_io_port_add(mach_thread_self(), io_port)))
    a->warn("i386-io-hurd: Can't i386_io_port_add(): %m");

  return errno ? 0 : 1;
}

static inline int
intel_cleanup_io(struct pci_access *a)
{
  if ((errno = i386_io_port_remove(mach_thread_self(), io_port)))
    a->warn("i386-io-hurd: Can't i386_io_port_remove(): %m");

  mach_port_deallocate(mach_task_self(), io_port);

  return -1;
}
