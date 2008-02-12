/*
 *	The PCI Utilities -- Common Functions
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#include "pciutils.h"

void NONRET
die(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  fprintf(stderr, "%s: ", program_name);
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

void *
xrealloc(void *ptr, unsigned int howmuch)
{
  void *p = realloc(ptr, howmuch);
  if (!p)
    die("Unable to allocate %d bytes of memory", howmuch);
  return p;
}

static void
set_pci_option(struct pci_access *pacc, char *arg)
{
  if (!strcmp(arg, "help"))
    {
      struct pci_param *p;
      printf("Known PCI access parameters:\n\n");
      for (p=NULL; p=pci_walk_params(pacc, p);)
	printf("%-20s %s (%s)\n", p->param, p->help, p->value);
      exit(0);
    }
  else
    {
      char *sep = strchr(arg, '=');
      if (!sep)
	die("Invalid PCI access parameter syntax: %s", arg);
      *sep++ = 0;
      if (pci_set_param(pacc, arg, sep) < 0)
	die("Unrecognized PCI access parameter: %s", arg);
    }
}

int
parse_generic_option(int i, struct pci_access *pacc, char *optarg)
{
  switch (i)
    {
#ifdef PCI_HAVE_PM_LINUX_PROC
    case 'P':
      pci_set_param(pacc, "proc.path", optarg);
      pacc->method = PCI_ACCESS_PROC_BUS_PCI;
      break;
#endif
#ifdef PCI_HAVE_PM_INTEL_CONF
    case 'H':
      if (!strcmp(optarg, "1"))
	pacc->method = PCI_ACCESS_I386_TYPE1;
      else if (!strcmp(optarg, "2"))
	pacc->method = PCI_ACCESS_I386_TYPE2;
      else
	die("Unknown hardware configuration type %s", optarg);
      break;
#endif
#ifdef PCI_HAVE_PM_DUMP
    case 'F':
      pci_set_param(pacc, "dump.name", optarg);
      pacc->method = PCI_ACCESS_DUMP;
      break;
#endif
    case 'G':
      pacc->debugging++;
      break;
    case 'O':
      set_pci_option(pacc, optarg);
      break;
    default:
      return 0;
    }
  return 1;
}
