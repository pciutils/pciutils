/*
 *	The PCI Library -- Initialization and related things
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "internal.h"

static struct pci_methods *pci_methods[PCI_ACCESS_MAX] = {
  NULL,
#ifdef PCI_HAVE_PM_LINUX_SYSFS
  &pm_linux_sysfs,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_LINUX_PROC
  &pm_linux_proc,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_INTEL_CONF
  &pm_intel_conf1,
  &pm_intel_conf2,
#else
  NULL,
  NULL,
#endif
#ifdef PCI_HAVE_PM_FBSD_DEVICE
  &pm_fbsd_device,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_AIX_DEVICE
  &pm_aix_device,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_NBSD_LIBPCI
  &pm_nbsd_libpci,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_OBSD_DEVICE
  &pm_obsd_device,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_DUMP
  &pm_dump,
#else
  NULL,
#endif
};

void *
pci_malloc(struct pci_access *a, int size)
{
  void *x = malloc(size);

  if (!x)
    a->error("Out of memory (allocation of %d bytes failed)", size);
  return x;
}

void
pci_mfree(void *x)
{
  if (x)
    free(x);
}

char *
pci_strdup(struct pci_access *a, char *s)
{
  int len = strlen(s) + 1;
  char *t = pci_malloc(a, len);
  memcpy(t, s, len);
  return t;
}

static void
pci_generic_error(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  fputs("pcilib: ", stderr);
  vfprintf(stderr, msg, args);
  fputc('\n', stderr);
  exit(1);
}

static void
pci_generic_warn(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  fputs("pcilib: ", stderr);
  vfprintf(stderr, msg, args);
  fputc('\n', stderr);
}

static void
pci_generic_debug(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  vfprintf(stdout, msg, args);
  va_end(args);
}

static void
pci_null_debug(char *msg UNUSED, ...)
{
}

int
pci_lookup_method(char *name)
{
  int i;

  for (i=0; i<PCI_ACCESS_MAX; i++)
    if (pci_methods[i] && !strcmp(pci_methods[i]->name, name))
      return i;
  return -1;
}

char *
pci_get_method_name(int index)
{
  if (index < 0 || index >= PCI_ACCESS_MAX)
    return NULL;
  else if (!pci_methods[index])
    return "";
  else
    return pci_methods[index]->name;
}

struct pci_access *
pci_alloc(void)
{
  struct pci_access *a = malloc(sizeof(struct pci_access));
  int i;

  memset(a, 0, sizeof(*a));
  pci_set_name_list_path(a, PCI_PATH_IDS_DIR "/" PCI_IDS, 0);
#ifdef PCI_USE_DNS
  pci_define_param(a, "net.domain", PCI_ID_DOMAIN, "DNS domain used for resolving of ID's");
  pci_define_param(a, "net.cache_name", "~/.pciids-cache", "Name of the ID cache file");
  a->id_lookup_mode = PCI_LOOKUP_CACHE;
#endif
  for(i=0; i<PCI_ACCESS_MAX; i++)
    if (pci_methods[i] && pci_methods[i]->config)
      pci_methods[i]->config(a);
  return a;
}

void
pci_init(struct pci_access *a)
{
  if (!a->error)
    a->error = pci_generic_error;
  if (!a->warning)
    a->warning = pci_generic_warn;
  if (!a->debug)
    a->debug = pci_generic_debug;
  if (!a->debugging)
    a->debug = pci_null_debug;

  if (a->method)
    {
      if (a->method >= PCI_ACCESS_MAX || !pci_methods[a->method])
	a->error("This access method is not supported.");
      a->methods = pci_methods[a->method];
    }
  else
    {
      unsigned int i;
      for(i=0; i<PCI_ACCESS_MAX; i++)
	if (pci_methods[i])
	  {
	    a->debug("Trying method %d...", i);
	    if (pci_methods[i]->detect(a))
	      {
		a->debug("...OK\n");
		a->methods = pci_methods[i];
		a->method = i;
		break;
	      }
	    a->debug("...No.\n");
	  }
      if (!a->methods)
	a->error("Cannot find any working access method.");
    }
  a->debug("Decided to use %s\n", a->methods->name);
  a->methods->init(a);
}

void
pci_cleanup(struct pci_access *a)
{
  struct pci_dev *d, *e;

  for(d=a->devices; d; d=e)
    {
      e = d->next;
      pci_free_dev(d);
    }
  if (a->methods)
    a->methods->cleanup(a);
  pci_free_name_list(a);
  pci_free_params(a);
  pci_set_name_list_path(a, NULL, 0);
  pci_mfree(a);
}
