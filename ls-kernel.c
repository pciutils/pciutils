/*
 *	The PCI Utilities -- Show Kernel Drivers
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lspci.h"

#ifdef PCI_OS_LINUX

#include <sys/utsname.h>

struct pcimap_entry {
  struct pcimap_entry *next;
  unsigned int vendor, device;
  unsigned int subvendor, subdevice;
  unsigned int class, class_mask;
  char module[1];
};

static struct pcimap_entry *pcimap_head;

static void
load_pcimap(void)
{
  static int tried_pcimap;
  struct utsname uts;
  char *name, line[1024];
  FILE *f;

  if (tried_pcimap)
    return;
  tried_pcimap = 1;

  if (name = opt_pcimap)
    {
      f = fopen(name, "r");
      if (!f)
	die("Cannot open pcimap file %s: %m", name);
    }
  else
    {
      if (uname(&uts) < 0)
	die("uname() failed: %m");
      name = alloca(64 + strlen(uts.release));
      sprintf(name, "/lib/modules/%s/modules.pcimap", uts.release);
      f = fopen(name, "r");
      if (!f)
	return;
    }

  while (fgets(line, sizeof(line), f))
    {
      char *c = strchr(line, '\n');
      struct pcimap_entry *e;

      if (!c)
	die("Unterminated or too long line in %s", name);
      *c = 0;
      if (!line[0] || line[0] == '#')
	continue;

      c = line;
      while (*c && *c != ' ' && *c != '\t')
	c++;
      if (!*c)
	continue;	/* FIXME: Emit warnings! */
      *c++ = 0;

      e = xmalloc(sizeof(*e) + strlen(line));
      if (sscanf(c, "%i%i%i%i%i%i",
		 &e->vendor, &e->device,
		 &e->subvendor, &e->subdevice,
		 &e->class, &e->class_mask) != 6)
	continue;
      e->next = pcimap_head;
      pcimap_head = e;
      strcpy(e->module, line);
    }
  fclose(f);
}

static int
match_pcimap(struct device *d, struct pcimap_entry *e)
{
  struct pci_dev *dev = d->dev;
  unsigned int class = get_conf_long(d, PCI_REVISION_ID) >> 8;
  word subv, subd;

#define MATCH(x, y) ((y) > 0xffff || (x) == (y))
  get_subid(d, &subv, &subd);
  return
    MATCH(dev->vendor_id, e->vendor) &&
    MATCH(dev->device_id, e->device) &&
    MATCH(subv, e->subvendor) &&
    MATCH(subd, e->subdevice) &&
    (class & e->class_mask) == e->class;
#undef MATCH
}

#define DRIVER_BUF_SIZE 1024

static char *
find_driver(struct device *d, char *buf)
{
  struct pci_dev *dev = d->dev;
  char name[1024], *drv, *base;
  int n;

  if (dev->access->method != PCI_ACCESS_SYS_BUS_PCI)
    return NULL;

  base = pci_get_param(dev->access, "sysfs.path");
  if (!base || !base[0])
    return NULL;

  n = snprintf(name, sizeof(name), "%s/devices/%04x:%02x:%02x.%d/driver",
	       base, dev->domain, dev->bus, dev->dev, dev->func);
  if (n < 0 || n >= (int)sizeof(name))
    die("show_driver: sysfs device name too long, why?");

  n = readlink(name, buf, DRIVER_BUF_SIZE);
  if (n < 0)
    return NULL;
  if (n >= DRIVER_BUF_SIZE)
    return "<name-too-long>";
  buf[n] = 0;

  if (drv = strrchr(buf, '/'))
    return drv+1;
  else
    return buf;
}

void
show_kernel(struct device *d)
{
  char buf[DRIVER_BUF_SIZE];
  char *driver;
  struct pcimap_entry *e, *last = NULL;

  if (driver = find_driver(d, buf))
    printf("\tKernel driver in use: %s\n", driver);

  load_pcimap();
  for (e=pcimap_head; e; e=e->next)
    if (match_pcimap(d, e) && (!last || strcmp(last->module, e->module)))
      {
	printf("%s %s", (last ? "," : "\tKernel modules:"), e->module);
	last = e;
      }
  if (last)
    putchar('\n');
}

void
show_kernel_machine(struct device *d)
{
  char buf[DRIVER_BUF_SIZE];
  char *driver;
  struct pcimap_entry *e, *last = NULL;

  if (driver = find_driver(d, buf))
    printf("Driver:\t%s\n", driver);

  load_pcimap();
  for (e=pcimap_head; e; e=e->next)
    if (match_pcimap(d, e) && (!last || strcmp(last->module, e->module)))
      {
	printf("Module:\t%s\n", e->module);
	last = e;
      }
}

#else

void
show_kernel(struct device *d UNUSED)
{
}

void
show_kernel_machine(struct device *d UNUSED)
{
}

#endif

