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
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

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
  byte src;
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

enum id_entry_src {
  SRC_UNKNOWN,
  SRC_CACHE,
  SRC_NET,
  SRC_LOCAL,
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

static inline unsigned int pair_first(unsigned int x)
{
  return (x >> 16) & 0xffff;
}

static inline unsigned int pair_second(unsigned int x)
{
  return x & 0xffff;
}

static inline unsigned int id_hash(int cat, u32 id12, u32 id34)
{
  unsigned int h;

  h = id12 ^ (id34 << 3) ^ (cat << 5);
  return h % HASH_SIZE;
}

static int id_insert(struct pci_access *a, int cat, int id1, int id2, int id3, int id4, char *text, enum id_entry_src src)
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
  n->src = src;
  memcpy(n->name, text, len+1);
  n->next = a->id_hash[h];
  a->id_hash[h] = n;
  return 0;
}

static char *id_lookup_raw(struct pci_access *a, int flags, int cat, int id1, int id2, int id3, int id4)
{
  struct id_entry *n, *best;
  u32 id12 = id_pair(id1, id2);
  u32 id34 = id_pair(id3, id4);

  if (a->id_hash)
    {
      n = a->id_hash[id_hash(cat, id12, id34)];
      best = NULL;
      for (; n; n=n->next)
        {
	  if (n->id12 != id12 || n->id34 != id34 || n->cat != cat)
	    continue;
	  if (n->src == SRC_LOCAL && (flags & PCI_LOOKUP_SKIP_LOCAL))
	    continue;
	  if (n->src == SRC_NET && !(flags & PCI_LOOKUP_NETWORK))
	    continue;
	  if (n->src == SRC_CACHE && !(flags & PCI_LOOKUP_CACHE))
	    continue;
	  if (!best || best->src < n->src)
	    best = n;
	}
      if (best)
	return best->name;
    }
  return NULL;
}

static const char cache_version[] = "#PCI-CACHE-1.0";

static int
pci_id_cache_load(struct pci_access *a, int flags)
{
  char *name;
  char line[MAX_LINE];
  const char default_name[] = "/.pciids-cache";
  FILE *f;
  int lino;

  a->id_cache_status = 1;
  if (!a->id_cache_file)
    {
      /* Construct the default ID cache name */
      uid_t uid = getuid();
      struct passwd *pw = getpwuid(uid);
      if (!pw)
        return 0;
      name = pci_malloc(a, strlen(pw->pw_dir) + sizeof(default_name));
      sprintf(name, "%s%s", pw->pw_dir, default_name);
      pci_set_id_cache(a, name, 1);
    }
  a->debug("Using cache %s\n", a->id_cache_file);
  if (flags & PCI_LOOKUP_REFRESH_CACHE)
    {
      a->debug("Not loading cache, will refresh everything\n");
      a->id_cache_status = 2;
      return 0;
    }

  f = fopen(a->id_cache_file, "rb");
  if (!f)
    {
      a->debug("Cache file does not exist\n");
      return 0;
    }
  /* FIXME: Compare timestamp with the pci.ids file? */

  lino = 0;
  while (fgets(line, sizeof(line), f))
    {
      char *p = strchr(line, '\n');
      lino++;
      if (p)
        {
	  *p = 0;
	  if (lino == 1)
	    {
	      if (strcmp(line, cache_version))
	        {
		  a->debug("Unrecognized cache version %s, ignoring\n", line);
		  break;
		}
	      continue;
	    }
	  else
	    {
	      int cat, id1, id2, id3, id4, cnt;
	      if (sscanf(line, "%d%x%x%x%x%n", &cat, &id1, &id2, &id3, &id4, &cnt) >= 5)
	        {
		  p = line + cnt;
		  while (*p && *p == ' ')
		    p++;
		  id_insert(a, cat, id1, id2, id3, id4, p, SRC_CACHE);
		  continue;
		}
	    }
	}
      a->warning("Malformed cache file %s (line %d), ignoring", a->id_cache_file, lino);
      break;
    }

  if (ferror(f))
    a->warning("Error while reading %s", a->id_cache_file);
  fclose(f);
  return 1;
}

static void
pci_id_cache_dirty(struct pci_access *a)
{
  if (a->id_cache_status >= 1)
    a->id_cache_status = 2;
}

void
pci_id_cache_flush(struct pci_access *a)
{
  int orig_status = a->id_cache_status;
  FILE *f;
  unsigned int h;
  struct id_entry *e, *e2;

  a->id_cache_status = 0;
  if (orig_status < 2)
    return;
  if (!a->id_cache_file)
    return;
  f = fopen(a->id_cache_file, "wb");
  if (!f)
    {
      a->warning("Cannot write %s: %s", a->id_cache_file, strerror(errno));
      return;
    }
  a->debug("Writing cache to %s\n", a->id_cache_file);
  fprintf(f, "%s\n", cache_version);

  for (h=0; h<HASH_SIZE; h++)
    for (e=a->id_hash[h]; e; e=e->next)
      if (e->src == SRC_CACHE || e->src == SRC_NET)
	{
	  /* Verify that every entry is written at most once */
	  for (e2=a->id_hash[h]; e2 != e; e2=e2->next)
	    if ((e2->src == SRC_CACHE || e2->src == SRC_NET) &&
	        e2->cat == e->cat &&
		e2->id12 == e->id12 && e2->id34 == e->id34)
	    break;
	  if (e2 == e)
	    fprintf(f, "%d %x %x %x %x %s\n",
	            e->cat,
		    pair_first(e->id12), pair_second(e->id12),
		    pair_first(e->id34), pair_second(e->id34),
		    e->name);
	}

  fflush(f);
  if (ferror(f))
    a->warning("Error writing %s", a->id_cache_file);
  fclose(f);
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
	    return strdup(txt+2);
	}
    }

  return NULL;
}

static char *id_lookup(struct pci_access *a, int flags, int cat, int id1, int id2, int id3, int id4)
{
  char *name;

  while (!(name = id_lookup_raw(a, flags, cat, id1, id2, id3, id4)))
    {
      if ((flags & PCI_LOOKUP_CACHE) && !a->id_cache_status)
	{
	  if (pci_id_cache_load(a, flags))
	    continue;
	}
      if (flags & PCI_LOOKUP_NETWORK)
        {
	  if (name = id_net_lookup(a, cat, id1, id2, id3, id4))
	    {
	      id_insert(a, cat, id1, id2, id3, id4, name, SRC_NET);
	      free(name);
	      pci_id_cache_dirty(a);
	    }
	  else
	    id_insert(a, cat, id1, id2, id3, id4, "", SRC_NET);	/* FIXME: Check that negative caching works */
	  /* We want to iterate the lookup to get the allocated ID entry from the hash */
	  continue;
	}
      return NULL;
    }
  return (name[0] ? name : NULL);
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
	      if (!id_lookup(a, 0, ID_VENDOR, id1, 0, 0, 0))
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
      if (id_insert(a, cat, id1, id2, id3, id4, p, SRC_LOCAL))
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
  pci_mfree(a->id_hash);
  a->id_hash = NULL;
  a->id_cache_status = 0;
  while (a->current_id_bucket)
    {
      struct id_bucket *buck = a->current_id_bucket;
      a->current_id_bucket = buck->next;
      pci_mfree(buck);
    }
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
      return format_name(buf, size, flags, id_lookup(a, flags, ID_VENDOR, iv, 0, 0, 0), numbuf, "Unknown vendor");
    case PCI_LOOKUP_DEVICE:
      iv = va_arg(args, int);
      id = va_arg(args, int);
      sprintf(numbuf, "%04x", id);
      return format_name(buf, size, flags, id_lookup(a, flags, ID_DEVICE, iv, id, 0, 0), numbuf, "Unknown device");
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
      return format_name(buf, size, flags, id_lookup_subsys(a, flags, iv, id, isv, isd), numbuf, "Unknown device");
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
      return format_name(buf, size, flags, cls, numbuf, ((flags & PCI_LOOKUP_MIXED) ? "Unknown class" : "Class"));
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

void pci_set_name_list_path(struct pci_access *a, char *name, int to_be_freed)
{
  if (a->free_id_name)
    free(a->id_file_name);
  a->id_file_name = name;
  a->free_id_name = to_be_freed;
}

void pci_set_net_domain(struct pci_access *a, char *name, int to_be_freed)
{
  if (a->free_id_domain)
    free(a->id_domain);
  a->id_domain = name;
  a->free_id_domain = to_be_freed;
}

void pci_set_id_cache(struct pci_access *a, char *name, int to_be_freed)
{
  if (a->free_id_cache_file)
    free(a->id_cache_file);
  a->id_cache_file = name;
  a->free_id_cache_file = to_be_freed;
}
