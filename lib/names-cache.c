/*
 *	The PCI Library -- ID to Name Cache
 *
 *	Copyright (c) 2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#include "internal.h"
#include "names.h"

static const char cache_version[] = "#PCI-CACHE-1.0";

int
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
		  pci_id_insert(a, cat, id1, id2, id3, id4, p, SRC_CACHE);
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

void
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

void
pci_set_id_cache(struct pci_access *a, char *name, int to_be_freed)
{
  if (a->free_id_cache_file)
    free(a->id_cache_file);
  a->id_cache_file = name;
  a->free_id_cache_file = to_be_freed;
}
