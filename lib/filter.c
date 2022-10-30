/*
 *	The PCI Library -- Device Filtering
 *
 *	Copyright (c) 1998--2022 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

void pci_filter_init_v38(struct pci_access *a UNUSED, struct pci_filter *f) VERSIONED_ABI;
char *pci_filter_parse_slot_v38(struct pci_filter *f, char *str) VERSIONED_ABI;
char *pci_filter_parse_id_v38(struct pci_filter *f, char *str) VERSIONED_ABI;
int pci_filter_match_v38(struct pci_filter *f, struct pci_dev *d) VERSIONED_ABI;

void
pci_filter_init_v38(struct pci_access *a UNUSED, struct pci_filter *f)
{
  memset((byte *) f, 0, sizeof(*f));
  f->domain = f->bus = f->slot = f->func = -1;
  f->vendor = f->device = -1;
  f->device_class = -1;
  f->device_class_mask = ~0U;
  f->prog_if = -1;
}

#define BUF_SIZE 64

static char *
split_to_fields(char *str, char *buffer, int sep, char **fields, int num_fields)
{
  if (buffer)
    {
      if (strlen(str) >= BUF_SIZE)
	return "Expression too long";
      strcpy(buffer, str);
      str = buffer;
    }

  int i = 0;

  for (;;)
    {
      if (i >= num_fields)
	return "Too many fields";
      fields[i++] = str;
      while (*str && *str != sep)
	str++;
      if (!*str)
	break;
      *str++ = 0;
    }

  while (i < num_fields)
    fields[i++] = NULL;

  return NULL;
}

static int
field_defined(char *field)
{
  return field && field[0] && strcmp(field, "*");
}

static int
parse_hex_field(char *str, int *outp, unsigned int *maskp, unsigned int max)
{
  unsigned int out = 0;
  unsigned int mask = ~0U;
  unsigned int bound = 0;

  if (!field_defined(str))
    return 1;	// and keep the defaults

  while (*str)
    {
      int c = *str++;
      int d;

      if ((c == 'x' || c == 'X') && maskp)
	{
	  out = out << 4;
	  bound = (bound << 4) | 1;
	  mask = mask << 4;
	}
      else
	{
	  if (c >= '0' && c <= '9')
	    d = c - '0';
	  else if (c >= 'A' && c <= 'F')
	    d = c - 'A' + 10;
	  else if (c >= 'a' && c <= 'f')
	    d = c - 'a' + 10;
	  else
	    return 0;

	  out = (out << 4) | d;
	  bound = (bound << 4) | d;
	  mask = (mask << 4) | 0xf;
	}

      if (bound > max)
	return 0;
    }

  *outp = out;
  if (maskp)
    *maskp = mask;
  return 1;
}

/* Slot filter syntax: [[[domain]:][bus]:][slot][.[func]] */

char *
pci_filter_parse_slot_v38(struct pci_filter *f, char *str)
{
  char buf[BUF_SIZE];
  char *fields[3];
  char *err;

  if (err = split_to_fields(str, buf, ':', fields, 3))
    return err;

  int i = 0;
  if (fields[2])
    {
      if (!parse_hex_field(fields[0], &f->domain, NULL, 0x7fffffff))
	return "Invalid domain number";
      i++;
    }

  if (fields[i+1])
    {
      if (!parse_hex_field(fields[i], &f->bus, NULL, 0xff))
	return "Invalid bus number";
      i++;
    }

  char *fdev = fields[i];
  if (field_defined(fdev))
    {
      char *sfields[2];
      if (split_to_fields(fdev, NULL, '.', sfields, 2))
	return "Invalid slot/function number";

      if (!parse_hex_field(sfields[0], &f->slot, NULL, 0x1f))
	return "Invalid slot number";

      if (!parse_hex_field(sfields[1], &f->func, NULL, 7))
	return "Invalid function number";
    }

  return NULL;
}

/* ID filter syntax: [vendor]:[device][:class[:progif]] */

char *
pci_filter_parse_id_v38(struct pci_filter *f, char *str)
{
  char buf[BUF_SIZE];
  char *fields[4];
  char *err;

  if (err = split_to_fields(str, buf, ':', fields, 4))
    return err;

  if (!fields[1])
    return "At least two fields must be given";

  if (!parse_hex_field(fields[0], &f->vendor, NULL, 0xffff))
    return "Invalid vendor ID";

  if (!parse_hex_field(fields[1], &f->device, NULL, 0xffff))
    return "Invalid device ID";

  if (!parse_hex_field(fields[2], &f->device_class, &f->device_class_mask, 0xffff))
    return "Invalid class code";

  if (!parse_hex_field(fields[3], &f->prog_if, NULL, 0xff))
    return "Invalid programming interface code";

  return NULL;
}

int
pci_filter_match_v38(struct pci_filter *f, struct pci_dev *d)
{
  if ((f->domain >= 0 && f->domain != d->domain) ||
      (f->bus >= 0 && f->bus != d->bus) ||
      (f->slot >= 0 && f->slot != d->dev) ||
      (f->func >= 0 && f->func != d->func))
    return 0;
  if (f->device >= 0 || f->vendor >= 0)
    {
      pci_fill_info_v38(d, PCI_FILL_IDENT);
      if ((f->device >= 0 && f->device != d->device_id) ||
	  (f->vendor >= 0 && f->vendor != d->vendor_id))
	return 0;
    }
  if (f->device_class >= 0)
    {
      pci_fill_info_v38(d, PCI_FILL_CLASS);
      if ((f->device_class ^ d->device_class) & f->device_class_mask)
	return 0;
    }
  if (f->prog_if >= 0)
    {
      pci_fill_info_v38(d, PCI_FILL_CLASS_EXT);
      if (f->prog_if != d->prog_if)
	return 0;
    }
  return 1;
}

/*
 * Before pciutils v3.3, struct pci_filter had fewer fields,
 * so we have to provide compatibility wrappers.
 */

struct pci_filter_v30 {
  int domain, bus, slot, func;			/* -1 = ANY */
  int vendor, device;
};

void pci_filter_init_v30(struct pci_access *a, struct pci_filter_v30 *f) VERSIONED_ABI;
char *pci_filter_parse_slot_v30(struct pci_filter_v30 *f, char *str) VERSIONED_ABI;
char *pci_filter_parse_id_v30(struct pci_filter_v30 *f, char *str) VERSIONED_ABI;
int pci_filter_match_v30(struct pci_filter_v30 *f, struct pci_dev *d) VERSIONED_ABI;

static void
pci_filter_import_v30(struct pci_filter_v30 *old, struct pci_filter *new)
{
  new->domain = old->domain;
  new->bus = old->bus;
  new->slot = old->slot;
  new->func = old->func;
  new->vendor = old->vendor;
  new->device = old->device;
  new->device_class = -1;
  new->device_class_mask = ~0U;
  new->prog_if = -1;
}

static void
pci_filter_export_v30(struct pci_filter *new, struct pci_filter_v30 *old)
{
  old->domain = new->domain;
  old->bus = new->bus;
  old->slot = new->slot;
  old->func = new->func;
  old->vendor = new->vendor;
  old->device = new->device;
}

void
pci_filter_init_v30(struct pci_access *a, struct pci_filter_v30 *f)
{
  struct pci_filter new;
  pci_filter_init_v38(a, &new);
  pci_filter_export_v30(&new, f);
}

char *
pci_filter_parse_slot_v30(struct pci_filter_v30 *f, char *str)
{
  struct pci_filter new;
  char *err;
  pci_filter_import_v30(f, &new);
  if (err = pci_filter_parse_slot_v38(&new, str))
    return err;
  pci_filter_export_v30(&new, f);
  return NULL;
}

char *
pci_filter_parse_id_v30(struct pci_filter_v30 *f, char *str)
{
  struct pci_filter new;
  char *err;
  pci_filter_import_v30(f, &new);
  if (err = pci_filter_parse_id_v38(&new, str))
    return err;
  if (new.device_class >= 0 || new.prog_if >= 0)
    return "Filtering by class or programming interface not supported in this program";
  pci_filter_export_v30(&new, f);
  return NULL;
}

int
pci_filter_match_v30(struct pci_filter_v30 *f, struct pci_dev *d)
{
  struct pci_filter new;
  pci_filter_import_v30(f, &new);
  return pci_filter_match_v38(&new, d);
}

// Version 3.3 is the same as version 3.8, only device_class_mask and prog_if were not implemented
// (their positions in struct pci_filter were declared as RFU).

STATIC_ALIAS(void pci_filter_init(struct pci_access *a, struct pci_filter *f), pci_filter_init_v38(a, f));
DEFINE_ALIAS(void pci_filter_init_v33(struct pci_access *a, struct pci_filter *f), pci_filter_init_v38);
SYMBOL_VERSION(pci_filter_init_v30, pci_filter_init@LIBPCI_3.0);
SYMBOL_VERSION(pci_filter_init_v33, pci_filter_init@LIBPCI_3.3);
SYMBOL_VERSION(pci_filter_init_v38, pci_filter_init@@LIBPCI_3.8);

STATIC_ALIAS(char *pci_filter_parse_slot(struct pci_filter *f, char *str), pci_filter_parse_slot_v38(f, str));
DEFINE_ALIAS(char *pci_filter_parse_slot_v33(struct pci_filter *f, char *str), pci_filter_parse_slot_v38);
SYMBOL_VERSION(pci_filter_parse_slot_v30, pci_filter_parse_slot@LIBPCI_3.0);
SYMBOL_VERSION(pci_filter_parse_slot_v33, pci_filter_parse_slot@LIBPCI_3.3);
SYMBOL_VERSION(pci_filter_parse_slot_v38, pci_filter_parse_slot@@LIBPCI_3.8);

STATIC_ALIAS(char *pci_filter_parse_id(struct pci_filter *f, char *str), pci_filter_parse_id_v38(f, str));
DEFINE_ALIAS(char *pci_filter_parse_id_v33(struct pci_filter *f, char *str), pci_filter_parse_id_v38);
SYMBOL_VERSION(pci_filter_parse_id_v30, pci_filter_parse_id@LIBPCI_3.0);
SYMBOL_VERSION(pci_filter_parse_id_v33, pci_filter_parse_id@LIBPCI_3.3);
SYMBOL_VERSION(pci_filter_parse_id_v38, pci_filter_parse_id@@LIBPCI_3.8);

STATIC_ALIAS(int pci_filter_match(struct pci_filter *f, struct pci_dev *d), pci_filter_match_v38(f, d));
DEFINE_ALIAS(int pci_filter_match_v33(struct pci_filter *f, struct pci_dev *d), pci_filter_match_v38);
SYMBOL_VERSION(pci_filter_match_v30, pci_filter_match@LIBPCI_3.0);
SYMBOL_VERSION(pci_filter_match_v33, pci_filter_match@LIBPCI_3.3);
SYMBOL_VERSION(pci_filter_match_v38, pci_filter_match@@LIBPCI_3.8);
