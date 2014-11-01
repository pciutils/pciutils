/*
 *	The PCI Library -- Parameters
 *
 *	Copyright (c) 2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

char *
pci_get_param(struct pci_access *acc, char *param)
{
  struct pci_param *p;

  for (p=acc->params; p; p=p->next)
    if (!strcmp(p->param, param))
      return p->value;
  return NULL;
}

void
pci_define_param(struct pci_access *acc, char *param, char *value, char *help)
{
  struct pci_param *p = pci_malloc(acc, sizeof(*p));

  p->next = acc->params;
  acc->params = p;
  p->param = param;
  p->value = value;
  p->value_malloced = 0;
  p->help = help;
}

int
pci_set_param_internal(struct pci_access *acc, char *param, char *value, int copy)
{
  struct pci_param *p;

  for (p=acc->params; p; p=p->next)
    if (!strcmp(p->param, param))
      {
	if (p->value_malloced)
	  pci_mfree(p->value);
	p->value_malloced = copy;
	if (copy)
	  p->value = pci_strdup(acc, value);
	else
	  p->value = value;
	return 0;
      }
  return -1;
}

int
pci_set_param(struct pci_access *acc, char *param, char *value)
{
  return pci_set_param_internal(acc, param, value, 1);
}

void
pci_free_params(struct pci_access *acc)
{
  struct pci_param *p;

  while (p = acc->params)
    {
      acc->params = p->next;
      if (p->value_malloced)
	pci_mfree(p->value);
      pci_mfree(p);
    }
}

struct pci_param *
pci_walk_params(struct pci_access *acc, struct pci_param *prev)
{
  /* So far, the params form a simple linked list, but this can change in the future */
  if (!prev)
    return acc->params;
  else
    return prev->next;
}
