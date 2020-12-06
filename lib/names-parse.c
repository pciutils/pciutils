/*
 *	The PCI Library -- Parsing of the ID list
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "internal.h"
#include "names.h"

#ifdef PCI_COMPRESSED_IDS
#include <zlib.h>
typedef gzFile pci_file;
#define pci_gets(f, l, s)	gzgets(f, l, s)
#define pci_eof(f)		gzeof(f)

static pci_file pci_open(struct pci_access *a)
{
  pci_file result;
  size_t len;
  char *new_name;

  result = gzopen(a->id_file_name, "rb");
  if (result)
    return result;
  len = strlen(a->id_file_name);
  if (len < 3 || memcmp(a->id_file_name + len - 3, ".gz", 3) != 0)
    return result;
  new_name = malloc(len - 2);
  memcpy(new_name, a->id_file_name, len - 3);
  new_name[len - 3] = 0;
  pci_set_name_list_path(a, new_name, 1);
  return gzopen(a->id_file_name, "rb");
}

#define pci_close(f)		gzclose(f)
#define PCI_ERROR(f, err)						\
	if (!err) {							\
		int errnum = 0;						\
		gzerror(f, &errnum);					\
		if (errnum >= 0) err = NULL;				\
		else if (errnum == Z_ERRNO) err = "I/O error";		\
		else err = zError(errnum);				\
	}
#else
typedef FILE * pci_file;
#define pci_gets(f, l, s)	fgets(l, s, f)
#define pci_eof(f)		feof(f)
#define pci_open(a)		fopen(a->id_file_name, "r")
#define pci_close(f)		fclose(f)
#define PCI_ERROR(f, err)	if (!err && ferror(f))	err = "I/O error";
#endif

static int id_hex(char *p, int cnt)
{
  int x = 0;
  while (cnt--)
    {
      x <<= 4;
      if (*p >= '0' && *p <= '9')
	x += (*p - '0');
      else if (*p >= 'a' && *p <= 'f')
	x += (*p - 'a' + 10);
      else if (*p >= 'A' && *p <= 'F')
	x += (*p - 'A' + 10);
      else
	return -1;
      p++;
    }
  return x;
}

static inline int id_white_p(int c)
{
  return (c == ' ') || (c == '\t');
}


static const char *id_parse_list(struct pci_access *a, pci_file f, int *lino)
{
  char line[MAX_LINE];
  char *p;
  int id1=0, id2=0, id3=0, id4=0;
  int cat = -1;
  int nest;
  static const char parse_error[] = "Parse error";

  *lino = 0;
  while (pci_gets(f, line, sizeof(line)))
    {
      (*lino)++;
      p = line;
      while (*p && *p != '\n' && *p != '\r')
	p++;
      if (!*p && !pci_eof(f))
	return "Line too long";
      *p = 0;
      if (p > line && (p[-1] == ' ' || p[-1] == '\t'))
	*--p = 0;

      p = line;
      while (id_white_p(*p))
	p++;
      if (!*p || *p == '#')
	continue;

      p = line;
      while (*p == '\t')
	p++;
      nest = p - line;

      if (!nest)					/* Top-level entries */
	{
	  if (p[0] == 'C' && p[1] == ' ')		/* Class block */
	    {
	      if ((id1 = id_hex(p+2, 2)) < 0 || !id_white_p(p[4]))
		return parse_error;
	      cat = ID_CLASS;
	      p += 5;
	    }
	  else if (p[0] == 'S' && p[1] == ' ')
	    {						/* Generic subsystem block */
	      if ((id1 = id_hex(p+2, 4)) < 0 || p[6])
		return parse_error;
	      if (!pci_id_lookup(a, 0, ID_VENDOR, id1, 0, 0, 0))
		return "Vendor does not exist";
	      cat = ID_GEN_SUBSYSTEM;
	      continue;
	    }
	  else if (p[0] >= 'A' && p[0] <= 'Z' && p[1] == ' ')
	    {						/* Unrecognized block (RFU) */
	      cat = ID_UNKNOWN;
	      continue;
	    }
	  else						/* Vendor ID */
	    {
	      if ((id1 = id_hex(p, 4)) < 0 || !id_white_p(p[4]))
		return parse_error;
	      cat = ID_VENDOR;
	      p += 5;
	    }
	  id2 = id3 = id4 = 0;
	}
      else if (cat == ID_UNKNOWN)			/* Nested entries in RFU blocks are skipped */
	continue;
      else if (nest == 1)				/* Nesting level 1 */
	switch (cat)
	  {
	  case ID_VENDOR:
	  case ID_DEVICE:
	  case ID_SUBSYSTEM:
	    if ((id2 = id_hex(p, 4)) < 0 || !id_white_p(p[4]))
	      return parse_error;
	    p += 5;
	    cat = ID_DEVICE;
	    id3 = id4 = 0;
	    break;
	  case ID_GEN_SUBSYSTEM:
	    if ((id2 = id_hex(p, 4)) < 0 || !id_white_p(p[4]))
	      return parse_error;
	    p += 5;
	    id3 = id4 = 0;
	    break;
	  case ID_CLASS:
	  case ID_SUBCLASS:
	  case ID_PROGIF:
	    if ((id2 = id_hex(p, 2)) < 0 || !id_white_p(p[2]))
	      return parse_error;
	    p += 3;
	    cat = ID_SUBCLASS;
	    id3 = id4 = 0;
	    break;
	  default:
	    return parse_error;
	  }
      else if (nest == 2)				/* Nesting level 2 */
	switch (cat)
	  {
	  case ID_DEVICE:
	  case ID_SUBSYSTEM:
	    if ((id3 = id_hex(p, 4)) < 0 || !id_white_p(p[4]) || (id4 = id_hex(p+5, 4)) < 0 || !id_white_p(p[9]))
	      return parse_error;
	    p += 10;
	    cat = ID_SUBSYSTEM;
	    break;
	  case ID_CLASS:
	  case ID_SUBCLASS:
	  case ID_PROGIF:
	    if ((id3 = id_hex(p, 2)) < 0 || !id_white_p(p[2]))
	      return parse_error;
	    p += 3;
	    cat = ID_PROGIF;
	    id4 = 0;
	    break;
	  default:
	    return parse_error;
	  }
      else						/* Nesting level 3 or more */
	return parse_error;
      while (id_white_p(*p))
	p++;
      if (!*p)
	return parse_error;
      if (pci_id_insert(a, cat, id1, id2, id3, id4, p, SRC_LOCAL))
	return "Duplicate entry";
    }
  return NULL;
}

int
pci_load_name_list(struct pci_access *a)
{
  pci_file f;
  int lino;
  const char *err;

  pci_free_name_list(a);
  a->id_load_failed = 1;
  if (!(f = pci_open(a)))
    return 0;
  err = id_parse_list(a, f, &lino);
  PCI_ERROR(f, err);
  pci_close(f);
  if (err)
    a->error("%s at %s, line %d\n", err, a->id_file_name, lino);
  a->id_load_failed = 0;
  return 1;
}

void
pci_free_name_list(struct pci_access *a)
{
  pci_id_cache_flush(a);
  pci_id_hash_free(a);
  pci_id_hwdb_free(a);
  a->id_load_failed = 0;
}

void
pci_set_name_list_path(struct pci_access *a, char *name, int to_be_freed)
{
  if (a->free_id_name)
    free(a->id_file_name);
  a->id_file_name = name;
  a->free_id_name = to_be_freed;
}
