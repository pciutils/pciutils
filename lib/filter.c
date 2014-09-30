/*
 *	The PCI Library -- Device Filtering
 *
 *	Copyright (c) 1998--2003 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

void
pci_filter_init(struct pci_access *a UNUSED, struct pci_filter *f)
{
  f->domain = f->bus = f->slot = f->func = -1;
  f->vendor = f->device = f->class = -1;
}

/* Slot filter syntax: [[[domain]:][bus]:][slot][.[func]] */

char *
pci_filter_parse_slot(struct pci_filter *f, char *str)
{
  char *colon = strrchr(str, ':');
  char *dot = strchr((colon ? colon + 1 : str), '.');
  char *mid = str;
  char *e, *bus, *colon2;

  if (colon)
    {
      *colon++ = 0;
      mid = colon;
      colon2 = strchr(str, ':');
      if (colon2)
	{
	  *colon2++ = 0;
	  bus = colon2;
	  if (str[0] && strcmp(str, "*"))
	    {
	      long int x = strtol(str, &e, 16);
	      if ((e && *e) || (x < 0 || x > 0xffff))
		return "Invalid domain number";
	      f->domain = x;
	    }
	}
      else
	bus = str;
      if (bus[0] && strcmp(bus, "*"))
	{
	  long int x = strtol(bus, &e, 16);
	  if ((e && *e) || (x < 0 || x > 0xff))
	    return "Invalid bus number";
	  f->bus = x;
	}
    }
  if (dot)
    *dot++ = 0;
  if (mid[0] && strcmp(mid, "*"))
    {
      long int x = strtol(mid, &e, 16);
      if ((e && *e) || (x < 0 || x > 0x1f))
	return "Invalid slot number";
      f->slot = x;
    }
  if (dot && dot[0] && strcmp(dot, "*"))
    {
      long int x = strtol(dot, &e, 16);
      if ((e && *e) || (x < 0 || x > 7))
	return "Invalid function number";
      f->func = x;
    }
  return NULL;
}

/* ID filter syntax: [vendor]:[device][:class] */

char *
pci_filter_parse_id(struct pci_filter *f, char *str)
{
  char *s, *c, *e;

  if (!*str)
    return NULL;
  s = strchr(str, ':');
  if (!s)
    return "':' expected";
  *s++ = 0;
  if (str[0] && strcmp(str, "*"))
    {
      long int x = strtol(str, &e, 16);
      if ((e && *e) || (x < 0 || x > 0xffff))
	return "Invalid vendor ID";
      f->vendor = x;
    }
  c = strchr(s, ':');
  if (c)
    *c++ = 0;
  if (s[0] && strcmp(s, "*"))
    {
      long int x = strtol(s, &e, 16);
      if ((e && *e) || (x < 0 || x > 0xffff))
	return "Invalid device ID";
      f->device = x;
    }
  if (c && c[0] && strcmp(s, "*"))
    {
      long int x = strtol(c, &e, 16);
      if ((e && *e) || (x < 0 || x > 0xffff))
	return "Invalid class code";
      f->class = x;
    }
  return NULL;
}

int
pci_filter_match(struct pci_filter *f, struct pci_dev *d)
{
  if ((f->domain >= 0 && f->domain != d->domain) ||
      (f->bus >= 0 && f->bus != d->bus) ||
      (f->slot >= 0 && f->slot != d->dev) ||
      (f->func >= 0 && f->func != d->func))
    return 0;
  if (f->device >= 0 || f->vendor >= 0)
    {
      pci_fill_info_v32(d, PCI_FILL_IDENT);
      if ((f->device >= 0 && f->device != d->device_id) ||
	  (f->vendor >= 0 && f->vendor != d->vendor_id))
	return 0;
    }
  if (f->class >= 0)
    {
      pci_fill_info(d, PCI_FILL_CLASS);
      if (f->class != d->device_class)
	return 0;
    }
  return 1;
}
