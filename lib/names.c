/*
 *	$Id: names.c,v 1.6 2000/04/29 12:56:23 mj Exp $
 *
 *	The PCI Library -- ID to Name Translation
 *
 *	Copyright (c) 1997--2000 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
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
  word id1, id2, id3, id4;
  int cat;
  byte *name;
};

#define NL_VENDOR 0
#define NL_DEVICE 1
#define NL_SUBSYSTEM 2
#define NL_CLASS 3
#define NL_SUBCLASS 4
#define NL_PROGIF 5

#define HASH_SIZE 1024

static inline unsigned int nl_calc_hash(int cat, int id1, int id2, int id3, int id4)
{
  unsigned int h;

  h = id1 ^ id2 ^ id3 ^ id4 ^ (cat << 5);
  h += (h >> 6);
  return h & (HASH_SIZE-1);
}

static struct nl_entry *nl_lookup(struct pci_access *a, int num, int cat, int id1, int id2, int id3, int id4)
{
  unsigned int h;
  struct nl_entry *n;

  if (num)
    return NULL;
  h = nl_calc_hash(cat, id1, id2, id3, id4);
  n = a->nl_hash[h];
  while (n && (n->id1 != id1 || n->id2 != id2 || n->id3 != id3 || n->id4 != id4 || n->cat != cat))
    n = n->next;
  return n;
}

static int nl_add(struct pci_access *a, int cat, int id1, int id2, int id3, int id4, byte *text)
{
  unsigned int h = nl_calc_hash(cat, id1, id2, id3, id4);
  struct nl_entry *n = a->nl_hash[h];

  while (n && (n->id1 != id1 || n->id2 != id2 || n->id3 != id3 || n->id4 != id4 || n->cat != cat))
    n = n->next;
  if (n)
    return 1;
  n = pci_malloc(a, sizeof(struct nl_entry));
  n->id1 = id1;
  n->id2 = id2;
  n->id3 = id3;
  n->id4 = id4;
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
  unsigned int id1=0, id2=0, id3=0, id4=0;
  int cat = -1;

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
      while (*q == '\t')
	q++;
      if (q == r)
	{
	  if (q[0] == 'C' && q[1] == ' ')
	    {
	      if (strlen(q+2) < 3 ||
		  q[4] != ' ' ||
		  sscanf(q+2, "%x", &id1) != 1)
		goto parserr;
	      cat = NL_CLASS;
	    }
	  else
	    {
	      if (strlen(q) < 5 ||
		  q[4] != ' ' ||
		  sscanf(q, "%x", &id1) != 1)
		goto parserr;
	      cat = NL_VENDOR;
	    }
	  id2 = id3 = id4 = 0;
	  q += 4;
	}
      else if (q == r+1) 
	switch (cat)
	  {
	  case NL_VENDOR:
	  case NL_DEVICE:
	  case NL_SUBSYSTEM:
	    if (sscanf(q, "%x", &id2) != 1 || q[4] != ' ')
	      goto parserr;
	    q += 5;
	    cat = NL_DEVICE;
	    id3 = id4 = 0;
	    break;
	  case NL_CLASS:
	  case NL_SUBCLASS:
	  case NL_PROGIF:
	    if (sscanf(q, "%x", &id2) != 1 || q[2] != ' ')
	      goto parserr;
	    q += 3;
	    cat = NL_SUBCLASS;
	    id3 = id4 = 0;
	    break;
	  default:
	    goto parserr;
	  }
      else if (q == r+2)
	switch (cat)
	  {
	  case NL_DEVICE:
	  case NL_SUBSYSTEM:
	    if (sscanf(q, "%x%x", &id3, &id4) != 2 || q[9] != ' ')
	      goto parserr;
	    q += 10;
	    cat = NL_SUBSYSTEM;
	    break;
	  case NL_CLASS:
	  case NL_SUBCLASS:
	  case NL_PROGIF:
	    if (sscanf(q, "%x", &id3) != 1 || q[2] != ' ')
	      goto parserr;
	    q += 3;
	    cat = NL_PROGIF;
	    id4 = 0;
	    break;
	  default:
	    goto parserr;
	  }
      else
	goto parserr;
      while (*q == ' ')
	q++;
      if (!*q)
	goto parserr;
      if (nl_add(a, cat, id1, id2, id3, id4, q))
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

char *
pci_lookup_name(struct pci_access *a, char *buf, int size, int flags, u32 arg1, u32 arg2, u32 arg3, u32 arg4)
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
      if (n = nl_lookup(a, num, NL_VENDOR, arg1, 0, 0, 0))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg1);
      break;
    case PCI_LOOKUP_DEVICE:
      if (n = nl_lookup(a, num, NL_DEVICE, arg1, arg2, 0, 0))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg2);
      break;
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE:
      if (!num)
	{
	  struct nl_entry *e, *e2;
	  e = nl_lookup(a, 0, NL_VENDOR, arg1, 0, 0, 0);
	  e2 = nl_lookup(a, 0, NL_DEVICE, arg1, arg2, 0, 0);
	  if (!e)
	    res = snprintf(buf, size, "Unknown device %04x:%04x", arg1, arg2);
	  else if (!e2)
	    res = snprintf(buf, size, "%s: Unknown device %04x", e->name, arg2);
	  else
	    res = snprintf(buf, size, "%s %s", e->name, e2->name);
	}
      else
	res = snprintf(buf, size, "%04x:%04x", arg1, arg2);
      break;
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_SUBSYSTEM:
      if (n = nl_lookup(a, num, NL_VENDOR, arg3, 0, 0, 0))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg1);
      break;
    case PCI_LOOKUP_DEVICE | PCI_LOOKUP_SUBSYSTEM:
      if (n = nl_lookup(a, num, NL_SUBSYSTEM, arg1, arg2, arg3, arg4))
	return n->name;
      else if (arg1 == arg3 && arg2 == arg4 && (n = nl_lookup(a, num, NL_DEVICE, arg1, arg2, 0, 0)))
	return n->name;
      else
	res = snprintf(buf, size, "%04x", arg2);
      break;
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE | PCI_LOOKUP_SUBSYSTEM:
      if (!num)
	{
	  struct nl_entry *e, *e2;
	  e = nl_lookup(a, 0, NL_VENDOR, arg3, 0, 0, 0);
	  e2 = nl_lookup(a, 0, NL_SUBSYSTEM, arg1, arg2, arg3, arg4);
	  if (!e2 && arg1 == arg3 && arg2 == arg4)
	    /* Cheat for vendors blindly setting subsystem ID same as device ID */
	    e2 = nl_lookup(a, 0, NL_DEVICE, arg1, arg2, 0, 0);
	  if (!e)
	    res = snprintf(buf, size, "Unknown device %04x:%04x", arg3, arg4);
	  else if (!e2)
	    res = snprintf(buf, size, "%s: Unknown device %04x", e->name, arg4);
	  else
	    res = snprintf(buf, size, "%s %s", e->name, e2->name);
	}
      else
	res = snprintf(buf, size, "%04x:%04x", arg3, arg4);
      break;
    case PCI_LOOKUP_CLASS:
      if (n = nl_lookup(a, num, NL_SUBCLASS, arg1 >> 8, arg1 & 0xff, 0, 0))
	return n->name;
      else if (n = nl_lookup(a, num, NL_CLASS, arg1, 0, 0, 0))
	res = snprintf(buf, size, "%s [%04x]", n->name, arg1);
      else
	res = snprintf(buf, size, "Class %04x", arg1);
      break;
    case PCI_LOOKUP_PROGIF:
      if (n = nl_lookup(a, num, NL_PROGIF, arg1 >> 8, arg1 & 0xff, arg2, 0))
	return n->name;
      if (arg1 == 0x0101)
	{
	  /* IDE controllers have complex prog-if semantics */
	  if (arg2 & 0x70)
	    return NULL;
	  res = snprintf(buf, size, "%s%s%s%s%s",
			 (arg2 & 0x80) ? "Master " : "",
			 (arg2 & 0x08) ? "SecP " : "",
			 (arg2 & 0x04) ? "SecO " : "",
			 (arg2 & 0x02) ? "PriP " : "",
			 (arg2 & 0x01) ? "PriO " : "");
	  if (res)
	    buf[--res] = 0;
	  break;
	}
      return NULL;
    default:
      return "<pci_lookup_name: invalid request>";
    }
  return (res == size) ? "<too-large>" : buf;
}
