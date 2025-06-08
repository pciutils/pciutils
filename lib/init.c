/*
 *	The PCI Library -- Initialization and related things
 *
 *	Copyright (c) 1997--2024 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "internal.h"

#ifdef PCI_OS_DJGPP
#include <crt0.h> /* for __dos_argv0 */
#endif

#ifdef PCI_OS_WINDOWS

#include <windows.h>

/* Force usage of ANSI (char*) variant of GetModuleFileName() function */
#ifdef _WIN32
#ifdef GetModuleFileName
#undef GetModuleFileName
#endif
#define GetModuleFileName GetModuleFileNameA
#endif

/* Define __ImageBase for all linkers */
#ifdef _WIN32
/* GNU LD provides __ImageBase symbol since 2.19, in previous versions it is
 * under name _image_base__, so add weak alias for compatibility. */
#ifdef __GNUC__
asm(".weak\t" PCI_STRINGIFY(__MINGW_USYMBOL(__ImageBase)) "\n\t"
    ".set\t"  PCI_STRINGIFY(__MINGW_USYMBOL(__ImageBase)) "," PCI_STRINGIFY(__MINGW_USYMBOL(_image_base__)));
#endif
/*
 * MSVC link.exe provides __ImageBase symbol since 12.00 (MSVC 6.0), for
 * previous versions resolve it at runtime via GetModuleHandleA() which
 * returns base for main executable or via VirtualQuery() for DLL builds.
 */
#if defined(_MSC_VER) && _MSC_VER < 1200
static HMODULE
get_current_module_handle(void)
{
#ifdef PCI_SHARED_LIB
  MEMORY_BASIC_INFORMATION info;
  size_t len = VirtualQuery(&get_current_module_handle, &info, sizeof(info));
  if (len != sizeof(info))
    return NULL;
  return (HMODULE)info.AllocationBase;
#else
  return GetModuleHandleA(NULL);
#endif
}
#define __ImageBase (*(IMAGE_DOS_HEADER *)get_current_module_handle())
#else
extern IMAGE_DOS_HEADER __ImageBase;
#endif
#endif

#if defined(_WINDLL)
extern HINSTANCE _hModule;
#elif defined(_WINDOWS)
extern HINSTANCE _hInstance;
#endif

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
#ifdef PCI_HAVE_PM_WIN32_KLDBG
  &pm_win32_kldbg,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_WIN32_SYSDBG
  &pm_win32_sysdbg,
#else
  NULL,
#endif
#ifdef PCI_HAVE_PM_MMIO_CONF
  &pm_mmio_conf1,
  &pm_mmio_conf1_ext,
#else
  NULL,
  NULL,
#endif
#if defined(PCI_HAVE_PM_ECAM)
  &pm_ecam,
#else
  NULL,
#endif
#if defined(PCI_HAVE_PM_AOS_EXPANSION)
  &pm_aos_expansion,
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
  PCI_ACCESS_WIN32_KLDBG,
  PCI_ACCESS_WIN32_SYSDBG,
  PCI_ACCESS_AOS_EXPANSION,
  // Low-level methods poking the hardware directly
  PCI_ACCESS_ECAM,
  PCI_ACCESS_I386_TYPE1,
  PCI_ACCESS_I386_TYPE2,
  PCI_ACCESS_MMIO_TYPE1_EXT,
  PCI_ACCESS_MMIO_TYPE1,
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

#if defined(PCI_OS_WINDOWS) || defined(PCI_OS_DJGPP)

static void
pci_init_name_list_path(struct pci_access *a)
{
  if ((PCI_PATH_IDS_DIR)[0])
    pci_set_name_list_path(a, PCI_PATH_IDS_DIR "\\" PCI_IDS, 0);
  else
    {
      char *path, *sep;
      size_t len;

#if defined(PCI_OS_WINDOWS) && (defined(_WIN32) || defined(_WINDLL) || defined(_WINDOWS))

      HMODULE module;
      size_t size;

#if defined(_WIN32)
      module = (HINSTANCE)&__ImageBase;
#elif defined(_WINDLL)
      module = _hModule;
#elif defined(_WINDOWS)
      module = _hInstance;
#endif

      /*
       * Module file name can have arbitrary length despite all MS examples say
       * about MAX_PATH upper limit. This limit does not apply for example when
       * executable is running from network disk with very long UNC paths or
       * when using "\\??\\" prefix for specifying executable binary path.
       * Function GetModuleFileName() returns passed size argument when passed
       * buffer is too small and does not signal any error. In this case retry
       * again with larger buffer.
       */
      size = 256; /* initial buffer size (more than sizeof(PCI_IDS)-4) */
retry:
      path = pci_malloc(a, size);
      len = GetModuleFileName(module, path, size-sizeof(PCI_IDS)-4); /* 4 for "\\\\?\\" */
      if (len >= size-sizeof(PCI_IDS)-4)
        {
          free(path);
          size *= 2;
          goto retry;
        }
      else if (len == 0)
        path[0] = '\0';

      /*
       * GetModuleFileName() has bugs. On Windows 10 it prepends current drive
       * letter if path is just pure NT namespace (with "\\??\\" prefix). Such
       * extra drive letter makes path fully invalid and unusable. So remove
       * extra drive letter to make path valid again.
       * Reproduce: CreateProcessW("\\??\\C:\\lspci.exe", ...)
       */
      if (((path[0] >= 'a' && path[0] <= 'z') ||
           (path[0] >= 'A' && path[0] <= 'Z')) &&
          strncmp(path+1, ":\\??\\", 5) == 0)
        {
          memmove(path, path+2, len-2);
          len -= 2;
          path[len] = '\0';
        }

      /*
       * GetModuleFileName() has bugs. On Windows 10 it does not add "\\\\?\\"
       * prefix when path is in native NT UNC namespace. Such path is treated by
       * WinAPI/DOS functions as standard DOS path relative to the current
       * directory, hence something completely different. So prepend missing
       * "\\\\?\\" prefix to make path valid again.
       * Reproduce: CreateProcessW("\\??\\UNC\\10.0.2.4\\qemu\\lspci.exe", ...)
       *
       * If path starts with DOS drive letter and with appended PCI_IDS is
       * longer than 260 bytes and is without "\\\\?\\" prefix then append it.
       * This prefix is required for paths and file names with DOS drive letter
       * longer than 260 bytes.
       */
      if (strncmp(path, "\\UNC\\", 5) == 0 ||
          strncmp(path, "UNC\\", 4) == 0 ||
          (((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) &&
           len + sizeof(PCI_IDS) >= 260))
        {
          memmove(path+4, path, len);
          memcpy(path, "\\\\?\\", 4);
          len += 4;
          path[len] = '\0';
        }

#elif defined(PCI_OS_DJGPP) || defined(PCI_OS_WINDOWS)

      const char *exe_path;

#ifdef PCI_OS_DJGPP
      exe_path = __dos_argv0;
#else
      exe_path = _pgmptr;
#endif

      len = strlen(exe_path);
      path = pci_malloc(a, len+sizeof(PCI_IDS));
      memcpy(path, exe_path, len+1);

#endif

      sep = strrchr(path, '\\');
      if (!sep)
        {
          /*
           * If current module path (current executable for static builds or
           * current DLL library for shared build) cannot be determined then
           * fallback to the current directory.
           */
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

#elif defined PCI_OS_AMIGAOS

static void
pci_init_name_list_path(struct pci_access *a)
{
  int len = strlen(PCI_PATH_IDS_DIR);

  if (!len)
    pci_set_name_list_path(a, PCI_IDS, 0);
  else
    {
      char last_char = PCI_PATH_IDS_DIR[len - 1];
      if (last_char == ':' || last_char == '/')  // root or parent char
	pci_set_name_list_path(a, PCI_PATH_IDS_DIR PCI_IDS, 0);
      else
	pci_set_name_list_path(a, PCI_PATH_IDS_DIR "/" PCI_IDS, 0);
    }
}

#else

static void
pci_init_name_list_path(struct pci_access *a)
{
  pci_set_name_list_path(a, PCI_PATH_IDS_DIR "/" PCI_IDS, 0);
}

#endif

#ifdef PCI_USE_DNS

static void
pci_init_dns(struct pci_access *a)
{
  pci_define_param(a, "net.domain", PCI_ID_DOMAIN, "DNS domain used for resolving of ID's");
  a->id_lookup_mode = PCI_LOOKUP_CACHE;

  char *cache_dir = getenv("XDG_CACHE_HOME");
  if (!cache_dir)
    cache_dir = "~/.cache";

  int name_len = strlen(cache_dir) + 32;
  char *cache_name = pci_malloc(NULL, name_len);
  snprintf(cache_name, name_len, "%s/pci-ids", cache_dir);
  struct pci_param *param = pci_define_param(a, "net.cache_name", cache_name, "Name of the ID cache file");
  param->value_malloced = 1;
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
  pci_init_dns(a);
#endif
#ifdef PCI_HAVE_HWDB
  pci_define_param(a, "hwdb.disable", "0", "Do not look up names in UDEV's HWDB if non-zero");
#endif
  for (i=0; i<PCI_ACCESS_MAX; i++)
    if (pci_methods[i] && pci_methods[i]->config)
      pci_methods[i]->config(a);
  return a;
}

int
pci_init_internal(struct pci_access *a, int skip_method)
{
  if (!a->error)
    a->error = pci_generic_error;
  if (!a->warning)
    a->warning = pci_generic_warn;
  if (!a->debug)
    a->debug = pci_generic_debug;
  if (!a->debugging)
    a->debug = pci_null_debug;

  if (a->method != PCI_ACCESS_AUTO)
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
	  if (skip_method == probe_sequence[i])
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
	return 0;
    }
  a->debug("Decided to use %s\n", a->methods->name);
  a->methods->init(a);
  return 1;
}

void
pci_init_v35(struct pci_access *a)
{
  if (!pci_init_internal(a, -1))
    a->error("Cannot find any working access method.");
}

STATIC_ALIAS(void pci_init(struct pci_access *a), pci_init_v35(a));
DEFINE_ALIAS(void pci_init_v30(struct pci_access *a), pci_init_v35);
DEFINE_ALIAS(void pci_init_v32(struct pci_access *a), pci_init_v35);
SYMBOL_VERSION(pci_init_v30, pci_init@LIBPCI_3.0);
SYMBOL_VERSION(pci_init_v32, pci_init@LIBPCI_3.2);
SYMBOL_VERSION(pci_init_v35, pci_init@@LIBPCI_3.5);

struct pci_access *
pci_clone_access(struct pci_access *a)
{
  struct pci_access *b = pci_alloc();

  b->writeable = a->writeable;
  b->buscentric = a->buscentric;
  b->debugging = a->debugging;
  b->error = a->error;
  b->warning = a->warning;
  b->debug = a->debug;

  return b;
}

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
