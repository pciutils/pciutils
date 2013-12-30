/*
 *	The PCI Library -- ID to Name Hash
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>

#include "internal.h"
#include "names.h"

#ifdef PCI_HAVE_HWDB
#include <libudev.h>
#include <stdio.h>
#endif

struct id_bucket {
  struct id_bucket *next;
  unsigned int full;
};

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

static inline unsigned int id_hash(int cat, u32 id12, u32 id34)
{
  unsigned int h;

  h = id12 ^ (id34 << 3) ^ (cat << 5);
  return h % HASH_SIZE;
}

int
pci_id_insert(struct pci_access *a, int cat, int id1, int id2, int id3, int id4, char *text, enum id_entry_src src)
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

char
*pci_id_lookup(struct pci_access *a, int flags, int cat, int id1, int id2, int id3, int id4)
{
  struct id_entry *n, *best;
  u32 id12, id34;

#ifdef PCI_HAVE_HWDB
  if (!(flags & PCI_LOOKUP_SKIP_LOCAL))
    {
      char modalias[64];
      const char *key = NULL;
      struct udev *udev = udev_new();
      struct udev_hwdb *hwdb = udev_hwdb_new(udev);
      struct udev_list_entry *entry;

      switch(cat)
        {
        case ID_VENDOR:
          sprintf(modalias, "pci:v%08X*", id1);
          key = "ID_VENDOR_FROM_DATABASE";
          break;
        case ID_DEVICE:
          sprintf(modalias, "pci:v%08Xd%08X*", id1, id2);
          key = "ID_MODEL_FROM_DATABASE";
          break;
        case ID_SUBSYSTEM:
          sprintf(modalias, "pci:v%08Xd%08Xsv%08Xsd%08X*", id1, id2, id3, id4);
          key = "ID_MODEL_FROM_DATABASE";
          break;
        case ID_GEN_SUBSYSTEM:
          sprintf(modalias, "pci:v*d*sv%08Xsd%08X*", id1, id2);
          key = "ID_MODEL_FROM_DATABASE";
          break;
        case ID_CLASS:
          sprintf(modalias, "pci:v*d*sv*sd*bc%02X*", id1);
          key = "ID_PCI_CLASS_FROM_DATABASE";
          break;
        case ID_SUBCLASS:
          sprintf(modalias, "pci:v*d*sv*sd*bc%02Xsc%02X*", id1, id2);
          key = "ID_PCI_SUBCLASS_FROM_DATABASE";
          break;
        case ID_PROGIF:
          sprintf(modalias, "pci:v*d*sv*sd*bc%02Xsc%02Xi%02X*", id1, id2, id3);
          key = "ID_PCI_INTERFACE_FROM_DATABASE";
          break;
        }

      if (key)
          udev_list_entry_foreach(entry, udev_hwdb_get_properties_list_entry(hwdb, modalias, 0))
              if (strcmp(udev_list_entry_get_name(entry), key) == 0)
                return udev_list_entry_get_value(entry);
    }
#endif

  id12 = id_pair(id1, id2);
  id34 = id_pair(id3, id4);

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

void
pci_id_hash_free(struct pci_access *a)
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
