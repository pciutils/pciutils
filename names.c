/*
 *	$Id: names.c,v 1.6 1998/07/17 08:57:16 mj Exp $
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

static struct nl_entry *nl_hash[HASH_SIZE];

static inline unsigned int nl_calc_hash(int cat, int id1, int id2)
{
  unsigned int h;

  h = id1 ^ id2 ^ (cat << 5);
  h += (h >> 6);
  return h & (HASH_SIZE-1);
}

static struct nl_entry *nl_lookup(int cat, int id1, int id2)
{
  unsigned int h = nl_calc_hash(cat, id1, id2);
  struct nl_entry *n = nl_hash[h];

  while (n && (n->id1 != id1 || n->id2 != id2 || n->cat != cat))
    n = n->next;
  return n;
}

static int nl_add(int cat, int id1, int id2, byte *text)
{
  unsigned int h = nl_calc_hash(cat, id1, id2);
  struct nl_entry *n = nl_hash[h];

  while (n && (n->id1 != id1 || n->id2 != id2 || n->cat != cat))
    n = n->next;
  if (n)
    return 1;
  n = xmalloc(sizeof(struct nl_entry));
  n->id1 = id1;
  n->id2 = id2;
  n->cat = cat;
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
      if (nl_add(cat, id1, id2, q))
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
do_lookup_vendor(int cat, word i)
{
  static char vendbuf[6];

  if (!show_numeric_ids && !name_list_loaded)
    load_name_list();
  if (!show_numeric_ids)
    {
      struct nl_entry *e;

      e = nl_lookup(cat, i, 0);
      if (e)
	return e->name;
    }
  sprintf(vendbuf, "%04x", i);
  return vendbuf;
}

char *
do_lookup_device(int cat, word v, word i)
{
  static char devbuf[6];

  if (!show_numeric_ids && !name_list_loaded)
    load_name_list();
  if (!show_numeric_ids)
    {
      struct nl_entry *e;

      e = nl_lookup(cat, v, i);
      if (e)
	return e->name;
    }
  sprintf(devbuf, "%04x", i);
  return devbuf;
}

char *
do_lookup_device_full(int cat, word v, word i)
{
  static char fullbuf[256];

  if (!show_numeric_ids && !name_list_loaded)
    load_name_list();
  if (!show_numeric_ids)
    {
      struct nl_entry *e, *e2;

      e = nl_lookup(cat, v, 0);
      e2 = nl_lookup(cat+1, v, i);
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
lookup_vendor(word i)
{
  return do_lookup_vendor(NL_VENDOR, i);
}

char *
lookup_subsys_vendor(word i)
{
  return do_lookup_vendor(NL_SUBSYSTEM_VENDOR, i);
}

char *
lookup_device(word i, word v)
{
  return do_lookup_device(NL_DEVICE, v, i);
}

char *
lookup_subsys_device(word v, word i)
{
  return do_lookup_device(NL_SUBSYSTEM_DEVICE, v, i);
}

char *
lookup_device_full(word v, word i)
{
  return do_lookup_device_full(NL_VENDOR, v, i);
}

char *
lookup_subsys_device_full(word v, word i)
{
  return do_lookup_device_full(NL_SUBSYSTEM_VENDOR, v, i);
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

      e = nl_lookup(NL_SUBCLASS, c >> 8, c & 0xff);
      if (e)
	return e->name;
      e = nl_lookup(NL_CLASS, c, 0);
      if (e)
	sprintf(classbuf, "%s [%04x]", e->name, c);
      else
	sprintf(classbuf, "Unknown class [%04x]", c);
    }
  else
    sprintf(classbuf, "Class %04x", c);
  return classbuf;
}
