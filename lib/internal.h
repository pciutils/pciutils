/*
 *	The PCI Library -- Internal Stuff
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "pci.h"
#include "sysdep.h"

struct pci_methods {
  char *name;
  void (*config)(struct pci_access *);
  int (*detect)(struct pci_access *);
  void (*init)(struct pci_access *);
  void (*cleanup)(struct pci_access *);
  void (*scan)(struct pci_access *);
  int (*fill_info)(struct pci_dev *, int flags);
  int (*read)(struct pci_dev *, int pos, byte *buf, int len);
  int (*write)(struct pci_dev *, int pos, byte *buf, int len);
  void (*init_dev)(struct pci_dev *);
  void (*cleanup_dev)(struct pci_dev *);
};

void pci_generic_scan_bus(struct pci_access *, byte *busmap, int bus);
void pci_generic_scan(struct pci_access *);
int pci_generic_fill_info(struct pci_dev *, int flags);
int pci_generic_block_read(struct pci_dev *, int pos, byte *buf, int len);
int pci_generic_block_write(struct pci_dev *, int pos, byte *buf, int len);

void *pci_malloc(struct pci_access *, int);
void pci_mfree(void *);
char *pci_strdup(struct pci_access *a, char *s);

struct pci_dev *pci_alloc_dev(struct pci_access *);
int pci_link_dev(struct pci_access *, struct pci_dev *);

void pci_define_param(struct pci_access *acc, char *param, char *val, char *help);
int pci_set_param_internal(struct pci_access *acc, char *param, char *val, int copy);
void pci_free_params(struct pci_access *acc);

extern struct pci_methods pm_intel_conf1, pm_intel_conf2, pm_linux_proc,
	pm_fbsd_device, pm_aix_device, pm_nbsd_libpci, pm_obsd_device,
	pm_dump, pm_linux_sysfs;
