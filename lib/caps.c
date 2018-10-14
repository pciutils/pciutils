/*
 *	The PCI Library -- Capabilities
 *
 *	Copyright (c) 2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>

#include "internal.h"

static void
pci_add_cap(struct pci_dev *d, unsigned int addr, unsigned int id, unsigned int type)
{
  struct pci_cap *cap = pci_malloc(d->access, sizeof(*cap));

  cap->next = d->first_cap;
  d->first_cap = cap;
  cap->addr = addr;
  cap->id = id;
  cap->type = type;
  d->access->debug("%04x:%02x:%02x.%d: Found capability %04x of type %d at %04x\n",
    d->domain, d->bus, d->dev, d->func, id, type, addr);
}

static void
pci_scan_trad_caps(struct pci_dev *d)
{
  word status = pci_read_word(d, PCI_STATUS);
  byte been_there[256];
  int where;

  if (!(status & PCI_STATUS_CAP_LIST))
    return;

  memset(been_there, 0, 256);
  where = pci_read_byte(d, PCI_CAPABILITY_LIST) & ~3;
  while (where)
    {
      byte id = pci_read_byte(d, where + PCI_CAP_LIST_ID);
      byte next = pci_read_byte(d, where + PCI_CAP_LIST_NEXT) & ~3;
      if (been_there[where]++)
	break;
      if (id == 0xff)
	break;
      pci_add_cap(d, where, id, PCI_CAP_NORMAL);
      where = next;
    }
}

static void
pci_scan_ext_caps(struct pci_dev *d)
{
  byte been_there[0x1000];
  int where = 0x100;

  if (!pci_find_cap(d, PCI_CAP_ID_EXP, PCI_CAP_NORMAL))
    return;

  memset(been_there, 0, 0x1000);
  do
    {
      u32 header;
      int id;

      header = pci_read_long(d, where);
      if (!header || header == 0xffffffff)
	break;
      id = header & 0xffff;
      if (been_there[where]++)
	break;
      pci_add_cap(d, where, id, PCI_CAP_EXTENDED);
      where = (header >> 20) & ~3;
    }
  while (where);
}

unsigned int
pci_scan_caps(struct pci_dev *d, unsigned int want_fields)
{
  if ((want_fields & PCI_FILL_EXT_CAPS) && !(d->known_fields & PCI_FILL_CAPS))
    want_fields |= PCI_FILL_CAPS;

  if (want_fields & PCI_FILL_CAPS)
    pci_scan_trad_caps(d);
  if (want_fields & PCI_FILL_EXT_CAPS)
    pci_scan_ext_caps(d);
  return want_fields;
}

void
pci_free_caps(struct pci_dev *d)
{
  struct pci_cap *cap;

  while (cap = d->first_cap)
    {
      d->first_cap = cap->next;
      pci_mfree(cap);
    }
}

struct pci_cap *
pci_find_cap(struct pci_dev *d, unsigned int id, unsigned int type)
{
  return pci_find_cap_nr(d, id, type, NULL);
}

/**
 * Finds a particular capability of a device
 *
 * To select one capability if there are more than one with the same id you
 * can provide a pointer to an unsigned int that contains the index which you
 * want as cap_number. If you don't care and are fine with the first one you
 * can supply NULL. To cap_number the acutal number of capablities with that id
 * will be written.
 *
 * @param d          Which device to target
 * @param id         Capability ID
 * @param type       PCI_FILL_CAPS or PCI_FILL_EXT_CAPS
 * @param cap_number Which instance of a capability to target
 * @returns          pointer to capability structure or NULL if not found
 */
struct pci_cap *
pci_find_cap_nr(struct pci_dev *d, unsigned int id, unsigned int type,
                unsigned int *cap_number)
{
  struct pci_cap *c;
  unsigned int target = 0;
  if (cap_number != NULL)
    {
      target = *cap_number;
      *cap_number = 0;
    }

  pci_fill_info_v35(d, ((type == PCI_CAP_NORMAL)
                        ? PCI_FILL_CAPS
                        : PCI_FILL_EXT_CAPS));
  for (c=d->first_cap; c; c=c->next)
    if (c->type == type && c->id == id)
      if (cap_number == NULL || target == *cap_number)
        return c;
      else
       (*cap_number)++;
  return NULL;
}
