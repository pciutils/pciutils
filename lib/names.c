/*
 *	The PCI Library -- ID to Name Translation
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "internal.h"
#include "names.h"

static char *id_lookup(struct pci_access *a, int flags, int cat, int id1, int id2, int id3, int id4)
{
  char *name;

  while (!(name = pci_id_lookup(a, flags, cat, id1, id2, id3, id4)))
    {
      if ((flags & PCI_LOOKUP_CACHE) && !a->id_cache_status)
	{
	  if (pci_id_cache_load(a, flags))
	    continue;
	}
      if (flags & PCI_LOOKUP_NETWORK)
        {
	  if (name = pci_id_net_lookup(a, cat, id1, id2, id3, id4))
	    {
	      pci_id_insert(a, cat, id1, id2, id3, id4, name, SRC_NET);
	      pci_mfree(name);
	      pci_id_cache_dirty(a);
	    }
	  else
	    pci_id_insert(a, cat, id1, id2, id3, id4, "", SRC_NET);
	  /* We want to iterate the lookup to get the allocated ID entry from the hash */
	  continue;
	}
      return NULL;
    }
  return (name[0] ? name : NULL);
}

static char *
id_lookup_subsys(struct pci_access *a, int flags, int iv, int id, int isv, int isd)
{
  char *d = NULL;
  if (iv > 0 && id > 0)						/* Per-device lookup */
    d = id_lookup(a, flags, ID_SUBSYSTEM, iv, id, isv, isd);
  if (!d)							/* Generic lookup */
    d = id_lookup(a, flags, ID_GEN_SUBSYSTEM, isv, isd, 0, 0);
  if (!d && iv == isv && id == isd)				/* Check for subsystem == device */
    d = id_lookup(a, flags, ID_DEVICE, iv, id, 0, 0);
  return d;
}

static char *
format_name(char *buf, int size, int flags, char *name, char *num, char *unknown)
{
  int res;
  if ((flags & PCI_LOOKUP_NO_NUMBERS) && !name)
    return NULL;
  else if (flags & PCI_LOOKUP_NUMERIC)
    res = snprintf(buf, size, "%s", num);
  else if (!name)
    res = snprintf(buf, size, ((flags & PCI_LOOKUP_MIXED) ? "%s [%s]" : "%s %s"), unknown, num);
  else if (!(flags & PCI_LOOKUP_MIXED))
    res = snprintf(buf, size, "%s", name);
  else
    res = snprintf(buf, size, "%s [%s]", name, num);
  if (res >= size && size >= 4)
    buf[size-2] = buf[size-3] = buf[size-4] = '.';
  else if (res < 0 || res >= size)
    return "<pci_lookup_name: buffer too small>";
  return buf;
}

static char *
format_name_pair(char *buf, int size, int flags, char *v, char *d, char *num)
{
  int res;
  if ((flags & PCI_LOOKUP_NO_NUMBERS) && (!v || !d))
    return NULL;
  if (flags & PCI_LOOKUP_NUMERIC)
    res = snprintf(buf, size, "%s", num);
  else if (flags & PCI_LOOKUP_MIXED)
    {
      if (v && d)
	res = snprintf(buf, size, "%s %s [%s]", v, d, num);
      else if (!v)
	res = snprintf(buf, size, "Device [%s]", num);
      else /* v && !d */
	res = snprintf(buf, size, "%s Device [%s]", v, num);
    }
  else
    {
      if (v && d)
	res = snprintf(buf, size, "%s %s", v, d);
      else if (!v)
	res = snprintf(buf, size, "Device %s", num);
      else /* v && !d */
	res = snprintf(buf, size, "%s Device %s", v, num+5);
    }
  if (res >= size && size >= 4)
    buf[size-2] = buf[size-3] = buf[size-4] = '.';
  else if (res < 0 || res >= size)
    return "<pci_lookup_name: buffer too small>";
  return buf;
}

char *
pci_lookup_name(struct pci_access *a, char *buf, int size, int flags, ...)
{
  va_list args;
  char *v, *d, *cls, *pif;
  int iv, id, isv, isd, icls, ipif;
  char numbuf[16], pifbuf[32];

  va_start(args, flags);

  flags |= a->id_lookup_mode;
  if (!(flags & PCI_LOOKUP_NO_NUMBERS))
    {
      if (a->numeric_ids > 1)
	flags |= PCI_LOOKUP_MIXED;
      else if (a->numeric_ids)
	flags |= PCI_LOOKUP_NUMERIC;
    }
  if (flags & PCI_LOOKUP_MIXED)
    flags &= ~PCI_LOOKUP_NUMERIC;

  if (!a->id_hash && !(flags & (PCI_LOOKUP_NUMERIC | PCI_LOOKUP_SKIP_LOCAL)) && !a->id_load_failed)
    pci_load_name_list(a);

  switch (flags & 0xffff)
    {
    case PCI_LOOKUP_VENDOR:
      iv = va_arg(args, int);
      sprintf(numbuf, "%04x", iv);
      return format_name(buf, size, flags, id_lookup(a, flags, ID_VENDOR, iv, 0, 0, 0), numbuf, "Vendor");
    case PCI_LOOKUP_DEVICE:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      sprintf(numbuf, "%04x", id);
      return format_name(buf, size, flags, id_lookup(a, flags, ID_DEVICE, iv, id, 0, 0), numbuf, "Device");
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      sprintf(numbuf, "%04x:%04x", iv, id);
      v = id_lookup(a, flags, ID_VENDOR, iv, 0, 0, 0);
      d = id_lookup(a, flags, ID_DEVICE, iv, id, 0, 0);
      return format_name_pair(buf, size, flags, v, d, numbuf);
    case PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR:
      isv = va_arg(args, int);
      sprintf(numbuf, "%04x", isv);
      v = id_lookup(a, flags, ID_VENDOR, isv, 0, 0, 0);
      return format_name(buf, size, flags, v, numbuf, "Unknown vendor");
    case PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      isv = va_arg(args, int);
      isd = va_arg(args, int);
      sprintf(numbuf, "%04x", isd);
      return format_name(buf, size, flags, id_lookup_subsys(a, flags, iv, id, isv, isd), numbuf, "Device");
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE | PCI_LOOKUP_SUBSYSTEM:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      isv = va_arg(args, int);
      isd = va_arg(args, int);
      v = id_lookup(a, flags, ID_VENDOR, isv, 0, 0, 0);
      d = id_lookup_subsys(a, flags, iv, id, isv, isd);
      sprintf(numbuf, "%04x:%04x", isv, isd);
      return format_name_pair(buf, size, flags, v, d, numbuf);
    case PCI_LOOKUP_CLASS:
      icls = va_arg(args, int);
      sprintf(numbuf, "%04x", icls);
      cls = id_lookup(a, flags, ID_SUBCLASS, icls >> 8, icls & 0xff, 0, 0);
      if (!cls && (cls = id_lookup(a, flags, ID_CLASS, icls >> 8, 0, 0, 0)))
	{
	  if (!(flags & PCI_LOOKUP_NUMERIC)) /* Include full class number */
	    flags |= PCI_LOOKUP_MIXED;
	}
      return format_name(buf, size, flags, cls, numbuf, "Class");
    case PCI_LOOKUP_PROGIF:
      icls = va_arg(args, int);
      ipif = va_arg(args, int);
      sprintf(numbuf, "%02x", ipif);
      pif = id_lookup(a, flags, ID_PROGIF, icls >> 8, icls & 0xff, ipif, 0);
      if (!pif && icls == 0x0101 && !(ipif & 0x70))
	{
	  /* IDE controllers have complex prog-if semantics */
	  sprintf(pifbuf, "%s%s%s%s%s",
		  (ipif & 0x80) ? " Master" : "",
		  (ipif & 0x08) ? " SecP" : "",
		  (ipif & 0x04) ? " SecO" : "",
		  (ipif & 0x02) ? " PriP" : "",
		  (ipif & 0x01) ? " PriO" : "");
	  pif = pifbuf;
	  if (*pif)
	    pif++;
	}
      return format_name(buf, size, flags, pif, numbuf, "ProgIf");
    default:
      return "<pci_lookup_name: invalid request>";
    }
}
