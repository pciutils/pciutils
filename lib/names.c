/*
 *	$Id: names.c,v 1.1 1999/01/22 21:05:33 mj Exp $
 *
 *	The PCI Library -- ID to Name Translation
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "internal.h"

struct nl_entry {
  struct nl_entry *next;
  word id1, id2;
  int cat;
  byte *name;
};

#define NL_VENDOR 0
#define NL_DEVICE 1
#define NL_CLASS 2
#define NL_SUBCLASS 3
#define NL_SUBSYSTEM_VENDOR 4
#define NL_SUBSYSTEM_DEVICE 5

#define HASH_SIZE 1024

static inline unsigned int nl_calc_hash(int cat, int id1, int id2)
{
  unsigned int h;

  h = id1 ^ id2 ^ (cat << 5);
  h += (h >> 6);
  return h & (HASH_SIZE-1);
}

static struct nl_entry *nl_lookup(struct pci_access *a, int num, int cat, int id1, int id2)
{
  unsigned int h;
  struct nl_entry *n;

  if (num)
    return NULL;
  h = nl_calc_hash(cat, id1, id2);
  n = a->nl_hash[h];
  while (n && (n->id1 != id1 || n->id2 != id2 || n->cat != cat))
    n = n->next;
  return n;
}

static int nl_add(struct pci_access *a, int cat, int id1, int id2, byte *text)
{
  unsigned int h = nl_calc_hash(cat, id1, id2);
  struct nl_entry *n = a->nl_hash[h];

  while (n && (n->id1 != id1 || n->id2 != id2 || n->cat != cat))
    n = n->next;
  if (n)
    return 1;
  n = pci_malloc(a, sizeof(struct nl_entry));
  n->id1 = id1;
  n->id2 = id2;
  n->cat = cat;
  n->name = text;
  n->next = a->nl_hash[h];
  a->nl_hash[h] = n;
  return 0;
}

static void
err_name_list(struct pci_access *a, char *msg)
{
  a->error("%s: %s: %s\n", a->id_file_name, msg, strerror(errno));
}

static void
parse_name_list(struct pci_access *a)
{
  byte *p = a->nl_list;
  byte *q, *r;
  int lino = 0;
  unsigned int id1=0, id2=0;
  int cat, last_cat = -1;

  while (*p)
    {
      lino++;
      q = p;
      while (*p && *p != '\n')
	{
	  if (*p == '#')
	    {
	      *p++ = 0;
	      while (*p && *p != '\n')
		p++;
	      break;
	    }
	  if (*p == '\t')
	    *p = ' ';
	  p++;
	}
      if (*p == '\n')
	*p++ = 0;
      if (!*q)
	continue;
      r = p;
      while (r > q && r[-1] == ' ')
	*--r = 0;
      r = q;
      while (*q == ' ')
	q++;
      if (r == q)
	{
	  if (q[0] == 'C' && q[1] == ' ')
	    {
	      if (strlen(q+2) < 3 ||
		  q[4] != ' ' ||
		  sscanf(q+2, "%x", &id1) != 1)
		goto parserr;
	      cat = last_cat = NL_CLASS;
	    }
	  else if (q[0] == 'S' && q[1] == ' ')
	    {
	      if (strlen(q+2) < 5 ||
		  q[6] != ' ' ||
		  sscanf(q+2, "%x", &id1) != 1)
		goto parserr;
	      cat = last_cat = NL_SUBSYSTEM_VENDOR;
	      q += 2;
	    }
	  else
	    {
	      if (strlen(q) < 5 ||
		  q[4] != ' ' ||
		  sscanf(q, "%x", &id1) != 1)
		goto parserr;
	      cat = last_cat = NL_VENDOR;
	    }
	  id2 = 0;
	}
      else
	{
	  if (sscanf(q, "%x", &id2) != 1)
	    goto parserr;
	  if (last_cat < 0)
	    goto parserr;
	  if (last_cat == NL_CLASS)
	    cat = NL_SUBCLASS;
	  else
	    cat = last_cat+1;
	}
      q += 4;
      while (*q == ' ')
	q++;
      if (!*q)
	goto parserr;
      if (nl_add(a, cat, id1, id2, q))
	a->error("%s, line %d: duplicate entry", a->id_file_name, lino);
    }
  return;

parserr:
  a->error("%s, line %d: parse error", a->id_file_name, lino);
}

static void
load_name_list(struct pci_access *a)
{
  int fd;
  struct stat st;

  fd = open(a->id_file_name, O_RDONLY);
  if (fd < 0)
    {
      a->numeric_ids = 1;
      return;
    }
  if (fstat(fd, &st) < 0)
    err_name_list(a, "stat");
  a->nl_list = pci_malloc(a, st.st_size + 1);
  if (read(fd, a->nl_list, st.st_size) != st.st_size)
    err_name_list(a, "read");
  a->nl_list[st.st_size] = 0;
  a->nl_hash = pci_malloc(a, sizeof(struct nl_entry *) * HASH_SIZE);
  bzero(a->nl_hash, sizeof(struct nl_entry *) * HASH_SIZE);
  parse_name_list(a);
  close(fd);
}

void
pci_free_name_list(struct pci_access *a)
{
  pci_mfree(a->nl_list);
  a->nl_list = NULL;
  pci_mfree(a->nl_hash);
  a->nl_hash = NULL;
}

static int
compound_name(struct pci_access *a, int num, char *buf, int size, int cat, int v, int i)
{
  if (!num)
    {
      struct nl_entry *e, *e2;

      e = nl_lookup(a, 0, cat, v, 0);
      e2 = nl_lookup(a, 0, cat+1, v, i);
      if (!e)
	return snprintf(buf, size, "Unknown device %04x:%04x", v, i);
      else if (!e2)
	return snprintf(buf, size, "%s: Unknown device %04x", e->name, i);
      else
	return snprintf(buf, size, "%s %s", e->name, e2->name);
    }
  else
    return snprintf(buf, size, "%04x:%04x", v, i);
}

char *
pci_lookup_name(struct pci_access *a, char *buf, int size, int flags, u32 arg1, u32 arg2)
{
  int num = a->numeric_ids;
  int res;
  struct nl_entry *n;

  if (flags & PCI_LOOKUP_NUMERIC)
    {
      flags &= PCI_LOOKUP_NUMERIC;
      num = 1;
    }
  if (!a->nl_hash && !num)
    {
      load_name_list(a);
      num = a->numeric_ids;
    }
  switch (flags)
    {
    case PCI_LOOKUP_VENDOR:
      if (n = nl_lookup(a, num, NL_VENDOR, arg1, 0))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg1);
      break;
    case PCI_LOOKUP_DEVICE:
      if (n = nl_lookup(a, num, NL_DEVICE, arg1, arg2))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg1);
      break;
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE:
      res = compound_name(a, num, buf, size, NL_VENDOR, arg1, arg2);
      break;
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_SUBSYSTEM:
      if (n = nl_lookup(a, num, NL_SUBSYSTEM_VENDOR, arg1, 0))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg1);
      break;
    case PCI_LOOKUP_DEVICE | PCI_LOOKUP_SUBSYSTEM:
      if (n = nl_lookup(a, num, NL_SUBSYSTEM_DEVICE, arg1, arg2))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg1);
      break;
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE | PCI_LOOKUP_SUBSYSTEM:
      res = compound_name(a, num, buf, size, NL_SUBSYSTEM_VENDOR, arg1, arg2);
      break;
    case PCI_LOOKUP_CLASS:
      if (n = nl_lookup(a, num, NL_SUBCLASS, arg1 >> 8, arg1 & 0xff))
	return n->name;
      else if (n = nl_lookup(a, num, NL_CLASS, arg1, 0))
	res = snprintf(buf, size, "%s [%04x]", n->name, arg1);
      else
	res = snprintf(buf, size, "Class %04x", arg1);
      break;
    default:
      return "<pci_lookup_name: invalid request>";
    }
  return (res == size) ? "<too-large>" : buf;
}
