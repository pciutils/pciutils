/*
 *	The PCI Library -- ID to Name Translation
 *
 *	Copyright (c) 1997--2014 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define MAX_LINE 1024

/* names-hash.c */

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
  SRC_HWDB,
  SRC_LOCAL,
};

#define BUCKET_SIZE 8192
#define HASH_SIZE 4099

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

int pci_id_insert(struct pci_access *a, int cat, int id1, int id2, int id3, int id4, char *text, enum id_entry_src src);
char *pci_id_lookup(struct pci_access *a, int flags, int cat, int id1, int id2, int id3, int id4);

/* names-cache.c */

int pci_id_cache_load(struct pci_access *a, int flags);
void pci_id_cache_dirty(struct pci_access *a);
void pci_id_cache_flush(struct pci_access *a);
void pci_id_hash_free(struct pci_access *a);

/* names-dns.c */

char *pci_id_net_lookup(struct pci_access *a, int cat, int id1, int id2, int id3, int id4);

/* names-hwdb.c */

char *pci_id_hwdb_lookup(struct pci_access *a, int cat, int id1, int id2, int id3, int id4);
void pci_id_hwdb_free(struct pci_access *a);
