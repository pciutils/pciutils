/*
 *	The PCI Library -- AIX /dev/pci[0-n] access
 *
 *	Copyright (c) 1999 Jari Kirma <kirma@cs.hut.fi>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/*
 *      Read functionality of this driver is briefly tested, and seems
 *      to supply basic information correctly, but I promise no more.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/mdio.h>

#include "internal.h"

#define AIX_LSDEV_CMD "/usr/sbin/lsdev -C -c bus -t pci\\* -S available -F name"
#define AIX_ODMGET_CMD \
  "/usr/bin/odmget -q 'name=%s and attribute=bus_number' CuAt | \
   /usr/bin/awk '$1 == \"value\" { print $3 }'"


/* AIX PCI bus device information */

typedef struct aix_pci_bus {
    char *bus_name;
    int   bus_number;
    int   bus_fd;
} aix_pci_bus;

#define PCI_BUS_MAX 16		/* arbitrary choice */
static aix_pci_bus pci_buses[PCI_BUS_MAX];
static int pci_bus_count = 0;


/* Utility Routines */

static aix_pci_bus *
aix_find_bus(struct pci_access *a, int bus_number)
{
  int i;

  for (i = 0; i < pci_bus_count; i++)
    {
      if (pci_buses[i].bus_number == bus_number)
        {
          return &pci_buses[i];
        }
    }

  a->error("aix_find_bus: bus number %d not found", bus_number);
}

static int
aix_bus_open(struct pci_access *a, int bus_number)
{
  aix_pci_bus *bp = aix_find_bus(a, bus_number);

  if (bp->bus_fd < 0)
    {
      char devbuf[256];
      int mode = a->writeable ? O_RDWR : O_RDONLY;

      snprintf(devbuf, sizeof (devbuf), "/dev/%s", bp->bus_name);
      bp->bus_fd = open(devbuf, mode, 0);
      if (bp->bus_fd < 0)
        {
          a->error("aix_open_bus: %s open failed", devbuf);
        }
    }

  return bp->bus_fd;
}

static int
aix_bus_number(char *name)
{
  int bus_number;
  FILE *odmget_pipe;
  char command[256];
  char buf[256];
  char *bp;
  char *ep;

  snprintf(command, sizeof (command), AIX_ODMGET_CMD, name);
  odmget_pipe = popen(command, "r");
  if (odmget_pipe == NULL)
    {
      /* popen failed */
      return -1;
    }

  if (fgets(buf, sizeof (buf) - 1, odmget_pipe) != NULL)
    {
      bp = buf + 1;	/* skip leading double quote */
      bus_number = strtol(bp, &ep, 0);
      if (bp == ep)
        {
          /* strtol failed */
          bus_number = -1;
        }
    }
  else
    {
      /* first PCI bus_number is not recorded in ODM CuAt; default to 0 */
      bus_number = 0;
    }

  (void) pclose(odmget_pipe);

  return bus_number;
}


/* Method entries */

static void
aix_config(struct pci_access *a)
{
  a->method_params[PCI_ACCESS_AIX_DEVICE] = NULL;
}

static int
aix_detect(struct pci_access *a)
{
  int len;
  int mode = a->writeable ? W_OK : R_OK;
  char *command = AIX_LSDEV_CMD;
  FILE *lsdev_pipe;
  char buf[256];
  char *name;

  lsdev_pipe = popen(command, "r");
  if (lsdev_pipe == NULL)
    {
      a->error("aix_config: popen(\"%s\") failed", command);
    }

  while (fgets(buf, sizeof (buf) - 1, lsdev_pipe) != NULL)
    {
      len = strlen(buf);
      while (buf[len-1] == '\n' || buf[len-1] == '\r')
          len--;
      buf[len] = '\0';				/* clobber the newline */

      name = (char *) pci_malloc(a, len + 1);
      strcpy(name, buf);
      pci_buses[pci_bus_count].bus_name = name;
      pci_buses[pci_bus_count].bus_number = 0;
      pci_buses[pci_bus_count].bus_fd = -1;
      if (!pci_bus_count)
          a->debug("...using %s", name);
      else
          a->debug(", %s", name);
      pci_bus_count++;
      if (pci_bus_count >= PCI_BUS_MAX)
          break;
    }

  (void) pclose(lsdev_pipe);

  return pci_bus_count;
}

static void
aix_init(struct pci_access *a)
{
  char *name;
  int i;

  for (i = 0; i < pci_bus_count; i++)
    {
      name = pci_buses[i].bus_name;
      pci_buses[i].bus_number = aix_bus_number(name);
    }
}

static void
aix_cleanup(struct pci_access *a)
{
  aix_pci_bus *bp;

  while (pci_bus_count-- > 0)
    {
      bp = &pci_buses[pci_bus_count];
      (void) free(bp->bus_name);
      if (bp->bus_fd >= 0)
        {
          (void) close(bp->bus_fd);
          bp->bus_fd = -1;
        }
    }
}

void
aix_scan(struct pci_access *a)
{
  int i;
  int bus_number;
  byte busmap[256];

  bzero(busmap, sizeof(busmap));
  for (i = 0; i < pci_bus_count; i++)
    {
      bus_number = pci_buses[i].bus_number;
      if (!busmap[bus_number])
        {
          pci_generic_scan_bus(a, busmap, bus_number);
        }
    }
}

static int
aix_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct mdio mdio;
  int fd = aix_bus_open(d->access, d->bus);

  mdio.md_addr = (ulong) pos;
  mdio.md_size = len;
  mdio.md_incr = MV_BYTE;
  mdio.md_data = (char *) buf;
  mdio.md_sla = PCI_DEVFN(d->dev, d->func);

  if (ioctl(fd, MIOPCFGET, &mdio) < 0)
    d->access->error("aix_read: ioctl(MIOPCFGET) failed");
  
  return 1;
}

static int
aix_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct mdio mdio;
  int fd = aix_bus_open(d->access, d->bus);

  mdio.md_addr = (ulong) pos;
  mdio.md_size = len;
  mdio.md_incr = MV_BYTE;
  mdio.md_data = (char *) buf;
  mdio.md_sla = PCI_DEVFN(d->dev, d->func);

  if (ioctl(fd, MIOPCFPUT, &mdio) < 0)
    {
      d->access->error("aix_write: ioctl(MIOPCFPUT) failed");
    }

  return 1;
}

struct pci_methods pm_aix_device = {
  "AIX-device",
  aix_config,
  aix_detect,
  aix_init,
  aix_cleanup,
  aix_scan,
  pci_generic_fill_info,
  aix_read,
  aix_write,
  NULL,                                 /* dev_init */
  NULL                                  /* dev_cleanup */
};
