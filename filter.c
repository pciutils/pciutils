/*
 *	$Id: filter.c,v 1.2 1998/06/08 07:51:45 mj Exp $
 *
 *	Linux PCI Utilities -- Device Filtering
 *
 *	Copyright (c) 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pciutils.h"

void
filter_init(struct pci_filter *f)
{
  f->bus = f->slot = f->func = -1;
  f->vendor = f->device = -1;
}

/* Slot filter syntax: [[bus]:][slot][.[func]] */

char *
filter_parse_slot(struct pci_filter *f, char *str)
{
  char *colon = strchr(str, ':');
  char *dot = strchr((colon ? colon + 1 : str), '.');
  char *mid = str;
  char *e;

  if (colon)
    {
      *colon++ = 0;
      mid = colon;
      if (str[0] && strcmp(str, "*"))
	{
	  long int x = strtol(str, &e, 16);
	  if ((e && *e) || (x < 0 || x >= 0xff))
	    return "Invalid bus number";
	  f->bus = x;
	}
    }
  if (dot)
    *dot++ = 0;
  if (mid[0] && strcmp(mid, "*"))
    {
      long int x = strtol(mid, &e, 16);
      if ((e && *e) || (x < 0 || x >= 0x1f))
	return "Invalid slot number";
      f->slot = x;
    }
  if (dot && dot[0] && strcmp(dot, "*"))
    {
      long int x = strtol(dot, &e, 16);
      if ((e && *e) || (x < 0 || x >= 7))
	return "Invalid function number";
      f->func = x;
    }
  return NULL;
}

/* ID filter syntax: [vendor]:[device] */

char *
filter_parse_id(struct pci_filter *f, char *str)
{
  char *s, *e;

  if (!*str)
    return NULL;
  s = strchr(str, ':');
  if (!s)
    return "':' expected";
  *s++ = 0;
  if (str[0] && strcmp(str, "*"))
    {
      long int x = strtol(str, &e, 16);
      if ((e && *e) || (x < 0 || x >= 0xffff))
	return "Invalid vendor ID";
      f->vendor = x;
    }
  if (s[0] && strcmp(s, "*"))
    {
      long int x = strtol(s, &e, 16);
      if ((e && *e) || (x < 0 || x >= 0xffff))
	return "Invalid device ID";
      f->device = x;
    }
  return NULL;
}

int
filter_match(struct pci_filter *f, byte bus, byte devfn, word vendid, word devid)
{
  if ((f->bus >= 0 && f->bus != bus) ||
      (f->slot >= 0 && f->slot != PCI_SLOT(devfn)) ||
      (f->func >= 0 && f->func != PCI_FUNC(devfn)) ||
      (f->device >= 0 && f->device != devid) ||
      (f->vendor >= 0 && f->vendor != vendid))
    return 0;
  return 1;
}
