/*
 *	$Id: names.c,v 1.5 1998/06/09 19:16:45 mj Exp $
 *
 *	Linux PCI Utilities -- Device ID to Name Translation
 *
 *	Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "pciutils.h"

int show_numeric_ids;

char *pci_ids = ETC_PCI_IDS;

static byte *name_list;
static int name_list_loaded;

struct nl_entry {
  struct nl_entry *next;
  int id1, id2;
  byte *name;
};

#define ID1_VENDOR -1
#define ID1_CLASS -2
#define ID1_SUBCLASS -3
#define ID1_ERROR -4

#define HASH_SIZE 1024

static struct nl_entry *nl_hash[HASH_SIZE];

static inline unsigned int nl_calc_hash(int id1, int id2)
{
  unsigned int h;

  h = id1 ^ id2;
  h ^= (h >> 6);
  return h & (HASH_SIZE-1);
}

static struct nl_entry *nl_lookup(int id1, int id2)
{
  unsigned int h = nl_calc_hash(id1, id2);
  struct nl_entry *n = nl_hash[h];

  while (n && (n->id1 != id1 || n->id2 != id2))
    n = n->next;
  return n;
}

static int nl_add(int id1, int id2, byte *text)
{
  unsigned int h = nl_calc_hash(id1, id2);
  struct nl_entry *n = nl_hash[h];

  while (n && (n->id1 != id1 || n->id2 != id2))
    n = n->next;
  if (n)
    return 1;
  n = xmalloc(sizeof(struct nl_entry));
  n->id1 = id1;
  n->id2 = id2;
  n->name = text;
  n->next = nl_hash[h];
  nl_hash[h] = n;
  return 0;
}

static void
err_name_list(char *msg)
{
  fprintf(stderr, "%s: %s: %m\n", pci_ids, msg);
  exit(1);
}

static void
parse_name_list(void)
{
  byte *p = name_list;
  byte *q, *r;
  int lino = 0;
  int id1 = ID1_ERROR;
  int id2 = 0;
  int i, j;

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
      if (strlen(q) < 5 || q[4] != ' ')
	goto parserr;
      if (r == q)
	{
	  if (q[0] == 'C' && q[1] == ' ')
	    {
	      if (sscanf(q+2, "%x", &j) != 1)
		goto parserr;
	      i = ID1_CLASS;
	    }
	  else
	    {
	      if (sscanf(q, "%x", &j) != 1)
		goto parserr;
	      i = ID1_VENDOR;
	    }
	  id1 = i;
	  id2 = j;
	}
      else
	{
	  if (sscanf(q, "%x", &j) != 1)
	    goto parserr;
	  if (id1 == ID1_ERROR)
	    goto parserr;
	  if (id1 == ID1_CLASS)
	    {
	      i = ID1_SUBCLASS;
	      j |= (id2 << 8);
	    }
	  else
	    i = id2;
	}
      q += 4;
      while (*q == ' ')
	q++;
      if (!*q)
	goto parserr;
      if (nl_add(i, j, q))
	{
	  fprintf(stderr, "%s, line %d: duplicate entry\n", pci_ids, lino);
	  exit(1);
	}
    }
  return;

parserr:
  fprintf(stderr, "%s, line %d: parse error\n", pci_ids, lino);
  exit(1);
}

static void
load_name_list(void)
{
  int fd;
  struct stat st;

  fd = open(pci_ids, O_RDONLY);
  if (fd < 0)
    {
      show_numeric_ids = 1;
      return;
    }
  if (fstat(fd, &st) < 0)
    err_name_list("stat");
  name_list = xmalloc(st.st_size + 1);
  if (read(fd, name_list, st.st_size) != st.st_size)
    err_name_list("read");
  name_list[st.st_size] = 0;
  parse_name_list();
  close(fd);
  name_list_loaded = 1;
}

char *
lookup_vendor(word i)
{
  static char vendbuf[6];

  if (!show_numeric_ids && !name_list_loaded)
    load_name_list();
  if (!show_numeric_ids)
    {
      struct nl_entry *e;

      e = nl_lookup(ID1_VENDOR, i);
      if (e)
	return e->name;
    }
  sprintf(vendbuf, "%04x", i);
  return vendbuf;
}

char *
lookup_device(word v, word i)
{
  static char devbuf[6];

  if (!show_numeric_ids && !name_list_loaded)
    load_name_list();
  if (!show_numeric_ids)
    {
      struct nl_entry *e;

      e = nl_lookup(v, i);
      if (e)
	return e->name;
    }
  sprintf(devbuf, "%04x", i);
  return devbuf;
}

char *
lookup_device_full(word v, word i)
{
  static char fullbuf[256];

  if (!show_numeric_ids && !name_list_loaded)
    load_name_list();
  if (!show_numeric_ids)
    {
      struct nl_entry *e, *e2;

      e = nl_lookup(ID1_VENDOR, v);
      e2 = nl_lookup(v, i);
      if (!e)
	sprintf(fullbuf, "Unknown device %04x:%04x", v, i);
      else if (!e2)
	sprintf(fullbuf, "%s: Unknown device %04x", e->name, i);
      else
	sprintf(fullbuf, "%s %s", e->name, e2->name);
    }
  else
    sprintf(fullbuf, "%04x:%04x", v, i);
  return fullbuf;
}

char *
lookup_class(word c)
{
  static char classbuf[80];

  if (!show_numeric_ids && !name_list_loaded)
    load_name_list();
  if (!show_numeric_ids)
    {
      struct nl_entry *e;

      e = nl_lookup(ID1_SUBCLASS, c);
      if (e)
	return e->name;
      e = nl_lookup(ID1_CLASS, c);
      if (e)
	sprintf(classbuf, "%s [%04x]", e->name, c);
      else
	sprintf(classbuf, "Unknown class [%04x]", c);
    }
  else
    sprintf(classbuf, "Class %04x", c);
  return classbuf;
}
