/*
 *	$Id: common.c,v 1.1 1999/01/22 21:04:50 mj Exp $
 *
 *	Linux PCI Utilities -- Common Functions
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#include "pciutils.h"

void __attribute__((noreturn))
die(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  fputs("lspci: ", stderr);
  vfprintf(stderr, msg, args);
  fputc('\n', stderr);
  exit(1);
}

void *
xmalloc(unsigned int howmuch)
{
  void *p = malloc(howmuch);
  if (!p)
    die("Unable to allocate %d bytes of memory", howmuch);
  return p;
}

int
parse_generic_option(int i, struct pci_access *pacc, char *optarg)
{
  switch (i)
    {
#ifdef HAVE_PM_LINUX_PROC
    case 'P':
      pacc->method_params[PCI_ACCESS_PROC_BUS_PCI] = optarg;
      pacc->method = PCI_ACCESS_PROC_BUS_PCI;
      break;
#endif
#ifdef HAVE_PM_INTEL_CONF
    case 'H':
      if (!strcmp(optarg, "1"))
	pacc->method = PCI_ACCESS_I386_TYPE1;
      else if (!strcmp(optarg, "2"))
	pacc->method = PCI_ACCESS_I386_TYPE2;
      else
	die("Unknown hardware configuration type %s", optarg);
      break;
#endif
#ifdef HAVE_PM_SYSCALLS
    case 'S':
      pacc->method = PCI_ACCESS_SYSCALLS;
      break;
#endif
#ifdef HAVE_PM_DUMP
    case 'F':
      pacc->method_params[PCI_ACCESS_DUMP] = optarg;
      pacc->method = PCI_ACCESS_DUMP;
      break;
#endif
    case 'G':
      pacc->debugging++;
      break;
    default:
      return 0;
    }
  return 1;
}
