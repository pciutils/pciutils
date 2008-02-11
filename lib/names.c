/*
 *	The PCI Library -- ID to Name Translation
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include "internal.h"

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
  if (len >= 3 && memcmp(a->id_file_name + len - 3, ".gz", 3) != 0)
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
		int errnum;						\
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

struct id_entry {
  struct id_entry *next;
  u32 id12, id34;
  byte cat;
  char name[1];
};

enum id_entry_type {
  ID_UNKNOWN,
  ID_VENDOR,
  ID_DEVICE,
  ID_SUBSYSTEM,
  ID_GEN_SUBSYSTEM,
  ID_CLASS,
  ID_SUBCLASS,
  ID_PROGIF
};

struct id_bucket {
  struct id_bucket *next;
  unsigned int full;
};

#define MAX_LINE 1024
#define BUCKET_SIZE 8192
#define HASH_SIZE 4099

#ifdef __GNUC__
#define BUCKET_ALIGNMENT __alignof__(struct id_bucket)
#else
union id_align {
  struct id_bucket *next;
  unsigned int full;
};
#define BUCKET_ALIGNMENT sizeof(union id_align)
#endif
#define BUCKET_ALIGN(n) ((n)+BUCKET_ALIGNMENT-(n)%BUCKET_ALIGNMENT)

static void *id_alloc(struct pci_access *a, unsigned int size)
{
  struct id_bucket *buck = a->current_id_bucket;
  unsigned int pos;

  if (!a->id_hash)
    {
      a->id_hash = pci_malloc(a, sizeof(struct id_entry *) * HASH_SIZE);
      memset(a->id_hash, 0, sizeof(struct id_entry *) * HASH_SIZE);
    }

  if (!buck || buck->full + size > BUCKET_SIZE)
    {
      buck = pci_malloc(a, BUCKET_SIZE);
      buck->next = a->current_id_bucket;
      a->current_id_bucket = buck;
      buck->full = BUCKET_ALIGN(sizeof(struct id_bucket));
    }
  pos = buck->full;
  buck->full = BUCKET_ALIGN(buck->full + size);
  return (byte *)buck + pos;
}

static inline u32 id_pair(unsigned int x, unsigned int y)
{
  return ((x << 16) | y);
}

static inline unsigned int id_hash(int cat, u32 id12, u32 id34)
{
  unsigned int h;

  h = id12 ^ (id34 << 3) ^ (cat << 5);
  return h % HASH_SIZE;
}

static char *id_net_lookup(struct pci_access *a, int cat, int id1, int id2, int id3, int id4)
{
  char name[256], dnsname[256], txt[256];
  byte answer[4096];
  const byte *data;
  int res, i, j, dlen;
  ns_msg m;
  ns_rr rr;

  switch (cat)
    {
    case ID_VENDOR:
      sprintf(name, "%04x", id1);
      break;
    case ID_DEVICE:
      sprintf(name, "%04x.%04x", id2, id1);
      break;
    case ID_SUBSYSTEM:
      sprintf(name, "%04x.%04x.%04x.%04x", id4, id3, id2, id1);
      break;
    case ID_GEN_SUBSYSTEM:
      sprintf(name, "%04x.%04x.s", id2, id1);
      break;
    case ID_CLASS:
      sprintf(name, "%02x.c", id1);
      break;
    case ID_SUBCLASS:
      sprintf(name, "%02x.%02x.c", id2, id1);
      break;
    case ID_PROGIF:
      sprintf(name, "%02x.%02x.%02x.c", id3, id2, id1);
      break;
    default:
      return NULL;
    }
  sprintf(dnsname, "%s.%s", name, a->id_domain);

  a->debug("Resolving %s\n", dnsname);
  res_init();
  res = res_query(dnsname, ns_c_in, ns_t_txt, answer, sizeof(answer));
  if (res < 0)
    {
      a->debug("\tfailed, h_errno=%d\n", _res.res_h_errno);
      return NULL;
    }
  if (ns_initparse(answer, res, &m) < 0)
    {
      a->debug("\tinitparse failed\n");
      return NULL;
    }
  for (i=0; ns_parserr(&m, ns_s_an, i, &rr) >= 0; i++)
    {
      a->debug("\tanswer %d (class %d, type %d)\n", i, ns_rr_class(rr), ns_rr_type(rr));
      if (ns_rr_class(rr) != ns_c_in || ns_rr_type(rr) != ns_t_txt)
	continue;
      data = ns_rr_rdata(rr);
      dlen = ns_rr_rdlen(rr);
      j = 0;
      while (j < dlen && j+1+data[j] <= dlen)
	{
	  memcpy(txt, &data[j+1], data[j]);
	  txt[data[j]] = 0;
	  j += 1+data[j];
	  a->debug("\t\t%s\n", txt);
	  if (txt[0] == 'i' && txt[1] == '=')
	    return strdup(txt+2);	/* FIXME */
	}
    }

  return NULL;
}

static int id_insert(struct pci_access *a, int cat, int id1, int id2, int id3, int id4, char *text)
{
  u32 id12 = id_pair(id1, id2);
  u32 id34 = id_pair(id3, id4);
  unsigned int h = id_hash(cat, id12, id34);
  struct id_entry *n = a->id_hash ? a->id_hash[h] : NULL;
  int len = strlen(text);

  while (n && (n->id12 != id12 || n->id34 != id34 || n->cat != cat))
    n = n->next;
  if (n)
    return 1;
  n = id_alloc(a, sizeof(struct id_entry) + len);
  n->id12 = id12;
  n->id34 = id34;
  n->cat = cat;
  memcpy(n->name, text, len+1);
  n->next = a->id_hash[h];
  a->id_hash[h] = n;
  return 0;
}

static char *id_lookup(struct pci_access *a, int cat, int id1, int id2, int id3, int id4)
{
  struct id_entry *n;
  u32 id12 = id_pair(id1, id2);
  u32 id34 = id_pair(id3, id4);
  char *name;

  if (a->id_hash)
    {
      n = a->id_hash[id_hash(cat, id12, id34)];
      while (n && (n->id12 != id12 || n->id34 != id34 || n->cat != cat))
	n = n->next;
      if (n)
	return n->name;
    }
  if (name = id_net_lookup(a, cat, id1, id2, id3, id4))
    {
      id_insert(a, cat, id1, id2, id3, id4, name);
      return name;
    }
  return NULL;
}

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
	      if (!id_lookup(a, ID_VENDOR, id1, 0, 0, 0))
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
      if (id_insert(a, cat, id1, id2, id3, id4, p))
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
  a->hash_load_failed = 1;
  if (!(f = pci_open(a)))
    return 0;
  err = id_parse_list(a, f, &lino);
  PCI_ERROR(f, err);
  pci_close(f);
  if (err)
    a->error("%s at %s, line %d\n", err, a->id_file_name, lino);
  a->hash_load_failed = 0;
  return 1;
}

void
pci_free_name_list(struct pci_access *a)
{
  pci_mfree(a->id_hash);
  a->id_hash = NULL;
  while (a->current_id_bucket)
    {
      struct id_bucket *buck = a->current_id_bucket;
      a->current_id_bucket = buck->next;
      pci_mfree(buck);
    }
}

static char *
id_lookup_subsys(struct pci_access *a, int iv, int id, int isv, int isd)
{
  char *d = NULL;
  if (iv > 0 && id > 0)						/* Per-device lookup */
    d = id_lookup(a, ID_SUBSYSTEM, iv, id, isv, isd);
  if (!d)							/* Generic lookup */
    d = id_lookup(a, ID_GEN_SUBSYSTEM, isv, isd, 0, 0);
  if (!d && iv == isv && id == isd)				/* Check for subsystem == device */
    d = id_lookup(a, ID_DEVICE, iv, id, 0, 0);
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
  if (res < 0 || res >= size)
    return "<pci_lookup_name: buffer too small>";
  else
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
	res = snprintf(buf, size, "Unknown device [%s]", num);
      else /* v && !d */
	res = snprintf(buf, size, "%s Unknown device [%s]", v, num);
    }
  else
    {
      if (v && d)
	res = snprintf(buf, size, "%s %s", v, d);
      else if (!v)
	res = snprintf(buf, size, "Unknown device %s", num);
      else /* v && !d */
	res = snprintf(buf, size, "%s Unknown device %s", v, num+5);
    }
  if (res < 0 || res >= size)
    return "<pci_lookup_name: buffer too small>";
  else
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

  if (!(flags & PCI_LOOKUP_NO_NUMBERS))
    {
      if (a->numeric_ids > 1)
	flags |= PCI_LOOKUP_MIXED;
      else if (a->numeric_ids)
	flags |= PCI_LOOKUP_NUMERIC;
    }
  if (flags & PCI_LOOKUP_MIXED)
    flags &= ~PCI_LOOKUP_NUMERIC;

  if (!a->id_hash && !(flags & PCI_LOOKUP_NUMERIC) && !a->hash_load_failed)
    pci_load_name_list(a);

  switch (flags & 0xffff)
    {
    case PCI_LOOKUP_VENDOR:
      iv = va_arg(args, int);
      sprintf(numbuf, "%04x", iv);
      return format_name(buf, size, flags, id_lookup(a, ID_VENDOR, iv, 0, 0, 0), numbuf, "Unknown vendor");
    case PCI_LOOKUP_DEVICE:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      sprintf(numbuf, "%04x", id);
      return format_name(buf, size, flags, id_lookup(a, ID_DEVICE, iv, id, 0, 0), numbuf, "Unknown device");
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      sprintf(numbuf, "%04x:%04x", iv, id);
      v = id_lookup(a, ID_VENDOR, iv, 0, 0, 0);
      d = id_lookup(a, ID_DEVICE, iv, id, 0, 0);
      return format_name_pair(buf, size, flags, v, d, numbuf);
    case PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR:
      isv = va_arg(args, int);
      sprintf(numbuf, "%04x", isv);
      v = id_lookup(a, ID_VENDOR, isv, 0, 0, 0);
      return format_name(buf, size, flags, v, numbuf, "Unknown vendor");
    case PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      isv = va_arg(args, int);
      isd = va_arg(args, int);
      sprintf(numbuf, "%04x", isd);
      return format_name(buf, size, flags, id_lookup_subsys(a, iv, id, isv, isd), numbuf, "Unknown device");
    case PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE | PCI_LOOKUP_SUBSYSTEM:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      isv = va_arg(args, int);
      isd = va_arg(args, int);
      v = id_lookup(a, ID_VENDOR, isv, 0, 0, 0);
      d = id_lookup_subsys(a, iv, id, isv, isd);
      sprintf(numbuf, "%04x:%04x", isv, isd);
      return format_name_pair(buf, size, flags, v, d, numbuf);
    case PCI_LOOKUP_CLASS:
      icls = va_arg(args, int);
      sprintf(numbuf, "%04x", icls);
      cls = id_lookup(a, ID_SUBCLASS, icls >> 8, icls & 0xff, 0, 0);
      if (!cls && (cls = id_lookup(a, ID_CLASS, icls >> 8, 0, 0, 0)))
	{
	  if (!(flags & PCI_LOOKUP_NUMERIC)) /* Include full class number */
	    flags |= PCI_LOOKUP_MIXED;
	}
      return format_name(buf, size, flags, cls, numbuf, ((flags & PCI_LOOKUP_MIXED) ? "Unknown class" : "Class"));
    case PCI_LOOKUP_PROGIF:
      icls = va_arg(args, int);
      ipif = va_arg(args, int);
      sprintf(numbuf, "%02x", ipif);
      pif = id_lookup(a, ID_PROGIF, icls >> 8, icls & 0xff, ipif, 0);
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

void pci_set_name_list_path(struct pci_access *a, char *name, int to_be_freed)
{
  if (a->free_id_name)
    free(a->id_file_name);
  a->id_file_name = name;
  a->free_id_name = to_be_freed;
}
