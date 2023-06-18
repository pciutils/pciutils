/*
 *	The PCI Library -- Looking up Names via UDEV and HWDB
 *
 *	Copyright (c) 2013--2014 Tom Gundersen <teg@jklm.no>
 *	Copyright (c) 2014 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>

#include "internal.h"
#include "names.h"

#ifdef PCI_HAVE_HWDB

#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>

char *
pci_id_hwdb_lookup(struct pci_access *a, int cat, int id1, int id2, int id3, int id4)
{
  char modalias[64];
  const char *key = NULL;

  const char *disabled = pci_get_param(a, "hwdb.disable");
  if (disabled && atoi(disabled))
    return NULL;

  switch (cat)
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
    {
      if (!a->id_udev_hwdb)
	{
	  a->debug("Initializing UDEV HWDB\n");
	  a->id_udev = udev_new();
	  a->id_udev_hwdb = udev_hwdb_new(a->id_udev);
	}

      struct udev_list_entry *entry;
      udev_list_entry_foreach(entry, udev_hwdb_get_properties_list_entry(a->id_udev_hwdb, modalias, 0))
	{
	  const char *entry_name = udev_list_entry_get_name(entry);
	  if (entry_name && !strcmp(entry_name, key))
	    {
	      const char *entry_value = udev_list_entry_get_value(entry);
	      if (entry_value)
		return pci_strdup(a, entry_value);
	    }
	}
    }

  return NULL;
}

void
pci_id_hwdb_free(struct pci_access *a)
{
  if (a->id_udev_hwdb)
    {
      udev_hwdb_unref(a->id_udev_hwdb);
      a->id_udev_hwdb = NULL;
    }
  if (a->id_udev)
    {
      udev_unref(a->id_udev);
      a->id_udev = NULL;
    }
}

#else

char *
pci_id_hwdb_lookup(struct pci_access *a UNUSED, int cat UNUSED, int id1 UNUSED, int id2 UNUSED, int id3 UNUSED, int id4 UNUSED)
{
  return NULL;
}

void
pci_id_hwdb_free(struct pci_access *a UNUSED)
{
}

#endif
