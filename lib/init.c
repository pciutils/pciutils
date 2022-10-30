/*
 *	The PCI Library -- Initialization and related things
 *
 *	Copyright (c) 1997--2018 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "internal.h"

#ifdef PCI_OS_WINDOWS
#include <windows.h>
#endif

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
#ifdef PCI_HAVE_PM_DARWIN_DEVICE
  &pm_darwin,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_SYLIXOS_DEVICE
  &pm_sylixos_device,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_HURD_CONF
  &pm_hurd,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_WIN32_CFGMGR32
  &pm_win32_cfgmgr32,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_WIN32_SYSDBG
  &pm_win32_sysdbg,
#else
  NULL,
#endif
};

// If PCI_ACCESS_AUTO is selected, we probe the access methods in this order
static int probe_sequence[] = {
  // System-specific methods
  PCI_ACCESS_SYS_BUS_PCI,
  PCI_ACCESS_PROC_BUS_PCI,
  PCI_ACCESS_FBSD_DEVICE,
  PCI_ACCESS_AIX_DEVICE,
  PCI_ACCESS_NBSD_LIBPCI,
  PCI_ACCESS_OBSD_DEVICE,
  PCI_ACCESS_DARWIN,
  PCI_ACCESS_SYLIXOS_DEVICE,
  PCI_ACCESS_HURD,
  PCI_ACCESS_WIN32_CFGMGR32,
  PCI_ACCESS_WIN32_SYSDBG,
  // Low-level methods poking the hardware directly
  PCI_ACCESS_I386_TYPE1,
  PCI_ACCESS_I386_TYPE2,
  -1,
};

static void PCI_NONRET
pci_generic_error(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  fputs("pcilib: ", stderr);
  vfprintf(stderr, msg, args);
  va_end(args);
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
  va_end(args);
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

// Memory allocation functions are safe to call if pci_access is not fully initalized or even NULL

void *
pci_malloc(struct pci_access *a, int size)
{
  void *x = malloc(size);

  if (!x)
    (a && a->error ? a->error : pci_generic_error)("Out of memory (allocation of %d bytes failed)", size);
  return x;
}

void
pci_mfree(void *x)
{
  if (x)
    free(x);
}

char *
pci_strdup(struct pci_access *a, const char *s)
{
  int len = strlen(s) + 1;
  char *t = pci_malloc(a, len);
  memcpy(t, s, len);
  return t;
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

#ifdef PCI_OS_WINDOWS

static void
pci_init_name_list_path(struct pci_access *a)
{
  if ((PCI_PATH_IDS_DIR)[0])
    pci_set_name_list_path(a, PCI_PATH_IDS_DIR "\\" PCI_IDS, 0);
  else
    {
      char *path, *sep;
      DWORD len;

      path = pci_malloc(a, MAX_PATH+1);
      len = GetModuleFileNameA(NULL, path, MAX_PATH+1);
      sep = (len > 0) ? strrchr(path, '\\') : NULL;
      if (len == 0 || len == MAX_PATH+1 || !sep || MAX_PATH-(size_t)(sep+1-path) < sizeof(PCI_IDS))
        {
          free(path);
          pci_set_name_list_path(a, PCI_IDS, 0);
        }
      else
        {
          memcpy(sep+1, PCI_IDS, sizeof(PCI_IDS));
          pci_set_name_list_path(a, path, 1);
        }
    }
}

#else

static void
pci_init_name_list_path(struct pci_access *a)
{
  pci_set_name_list_path(a, PCI_PATH_IDS_DIR "/" PCI_IDS, 0);
}

#endif

struct pci_access *
pci_alloc(void)
{
  struct pci_access *a = pci_malloc(NULL, sizeof(struct pci_access));
  int i;

  memset(a, 0, sizeof(*a));
  pci_init_name_list_path(a);
#ifdef PCI_USE_DNS
  pci_define_param(a, "net.domain", PCI_ID_DOMAIN, "DNS domain used for resolving of ID's");
  pci_define_param(a, "net.cache_name", "~/.pciids-cache", "Name of the ID cache file");
  a->id_lookup_mode = PCI_LOOKUP_CACHE;
#endif
#ifdef PCI_HAVE_HWDB
  pci_define_param(a, "hwdb.disable", "0", "Do not look up names in UDEV's HWDB if non-zero");
#endif
  for (i=0; i<PCI_ACCESS_MAX; i++)
    if (pci_methods[i] && pci_methods[i]->config)
      pci_methods[i]->config(a);
  return a;
}

void
pci_init_v35(struct pci_access *a)
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
      for (i=0; probe_sequence[i] >= 0; i++)
	{
	  struct pci_methods *m = pci_methods[probe_sequence[i]];
	  if (!m)
	    continue;
	  a->debug("Trying method %s...", m->name);
	  if (m->detect(a))
	    {
	      a->debug("...OK\n");
	      a->methods = m;
	      a->method = probe_sequence[i];
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

STATIC_ALIAS(void pci_init(struct pci_access *a), pci_init_v35(a));
DEFINE_ALIAS(void pci_init_v30(struct pci_access *a), pci_init_v35);
SYMBOL_VERSION(pci_init_v30, pci_init@LIBPCI_3.0);
SYMBOL_VERSION(pci_init_v35, pci_init@@LIBPCI_3.5);

void
pci_cleanup(struct pci_access *a)
{
  struct pci_dev *d, *e;

  for (d=a->devices; d; d=e)
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
