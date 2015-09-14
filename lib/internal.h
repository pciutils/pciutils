/*
 *	The PCI Library -- Internal Stuff
 *
 *	Copyright (c) 1997--2014 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "config.h"

#ifdef PCI_SHARED_LIB
#define PCI_ABI __attribute__((visibility("default")))
// Functions, which are bound to externally visible symbols by the versioning
// mechanism, have to be declared as VERSIONED. Otherwise, GCC with global
// optimizations is happy to optimize them away, leading to linker failures.
#define VERSIONED_ABI __attribute__((used)) PCI_ABI
#ifdef __APPLE__
#define STATIC_ALIAS(_decl, _for) _decl PCI_ABI { return _for; }
#define DEFINE_ALIAS(_decl, _for)
#define SYMBOL_VERSION(_int, _ext)
#else
#define STATIC_ALIAS(_decl, _for)
#define DEFINE_ALIAS(_decl, _for) extern _decl __attribute__((alias(#_for)))
#define SYMBOL_VERSION(_int, _ext) asm(".symver " #_int "," #_ext)
#endif
#else
#define VERSIONED_ABI
#define STATIC_ALIAS(_decl, _for) _decl { return _for; }
#define DEFINE_ALIAS(_decl, _for)
#define SYMBOL_VERSION(_int, _ext)
#endif

#include "pci.h"
#include "sysdep.h"

struct pci_methods {
  char *name;
  char *help;
  void (*config)(struct pci_access *);
  int (*detect)(struct pci_access *);
  void (*init)(struct pci_access *);
  void (*cleanup)(struct pci_access *);
  void (*scan)(struct pci_access *);
  int (*fill_info)(struct pci_dev *, int flags);
  int (*read)(struct pci_dev *, int pos, byte *buf, int len);
  int (*write)(struct pci_dev *, int pos, byte *buf, int len);
  int (*read_vpd)(struct pci_dev *, int pos, byte *buf, int len);
  void (*init_dev)(struct pci_dev *);
  void (*cleanup_dev)(struct pci_dev *);
};

/* generic.c */
void pci_generic_scan_bus(struct pci_access *, byte *busmap, int bus);
void pci_generic_scan(struct pci_access *);
int pci_generic_fill_info(struct pci_dev *, int flags);
int pci_generic_block_read(struct pci_dev *, int pos, byte *buf, int len);
int pci_generic_block_write(struct pci_dev *, int pos, byte *buf, int len);

/* init.c */
void *pci_malloc(struct pci_access *, int);
void pci_mfree(void *);
char *pci_strdup(struct pci_access *a, const char *s);

/* access.c */
struct pci_dev *pci_alloc_dev(struct pci_access *);
int pci_link_dev(struct pci_access *, struct pci_dev *);

int pci_fill_info_v30(struct pci_dev *, int flags) VERSIONED_ABI;
int pci_fill_info_v31(struct pci_dev *, int flags) VERSIONED_ABI;
int pci_fill_info_v32(struct pci_dev *, int flags) VERSIONED_ABI;
int pci_fill_info_v33(struct pci_dev *, int flags) VERSIONED_ABI;
int pci_fill_info_v34(struct pci_dev *, int flags) VERSIONED_ABI;

/* params.c */
void pci_define_param(struct pci_access *acc, char *param, char *val, char *help);
int pci_set_param_internal(struct pci_access *acc, char *param, char *val, int copy);
void pci_free_params(struct pci_access *acc);

/* caps.c */
unsigned int pci_scan_caps(struct pci_dev *, unsigned int want_fields);
void pci_free_caps(struct pci_dev *);

extern struct pci_methods pm_intel_conf1, pm_intel_conf2, pm_linux_proc,
	pm_fbsd_device, pm_aix_device, pm_nbsd_libpci, pm_obsd_device,
	pm_dump, pm_linux_sysfs, pm_darwin;
