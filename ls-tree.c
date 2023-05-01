/*
 *	The PCI Utilities -- Show Bus Tree
 *
 *	Copyright (c) 1997--2021 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lspci.h"

struct bridge host_bridge = { NULL, NULL, NULL, NULL, NULL, NULL, ~0, ~0, ~0, ~0, NULL };

static struct bus *
find_bus(struct bridge *b, unsigned int domain, unsigned int n)
{
  struct bus *bus;

  for (bus=b->first_bus; bus; bus=bus->sibling)
    if (bus->domain == domain && bus->number == n)
      break;
  return bus;
}

static struct device *
find_device(struct pci_dev *dd)
{
  struct device *d;

  if (!dd)
    return NULL;
  for (d=first_dev; d; d=d->next)
    if (d->dev == dd)
      break;
  return d;
}

static struct bus *
new_bus(struct bridge *b, unsigned int domain, unsigned int n)
{
  struct bus *bus = xmalloc(sizeof(struct bus));
  bus->domain = domain;
  bus->number = n;
  bus->sibling = NULL;
  bus->first_dev = NULL;
  bus->last_dev = &bus->first_dev;
  bus->parent_bridge = b;
  if (b->last_bus)
    b->last_bus->sibling = bus;
  b->last_bus = bus;
  if (!b->first_bus)
    b->first_bus = bus;
  return bus;
}

static void
insert_dev(struct device *d, struct bridge *b)
{
  struct pci_dev *p = d->dev;
  struct device *parent = NULL;
  struct bus *bus = NULL;

  if (p->known_fields & PCI_FILL_PARENT)
    parent = find_device(p->parent);

  if (parent && parent->bridge)
    {
      bus = parent->bridge->first_bus;
      if (!bus)
        bus = new_bus(parent->bridge, p->domain, p->bus);
    }

  if (!bus && b == &host_bridge)
    {
      for (b=b->child; b; b=b->prev)
        if (b->domain == (unsigned)p->domain)
          break;
      if (!b)
        b = &host_bridge;
    }

  if (!bus && ! (bus = find_bus(b, p->domain, p->bus)))
    {
      struct bridge *c;
      for (c=b->child; c; c=c->prev)
	if (c->domain == (unsigned)p->domain && c->secondary <= p->bus && p->bus <= c->subordinate)
          {
            insert_dev(d, c);
            return;
          }
      bus = new_bus(b, p->domain, p->bus);
    }
  /* Simple insertion at the end _does_ guarantee the correct order as the
   * original device list was sorted by (domain, bus, devfn) lexicographically
   * and all devices on the new list have the same bus number.
   */
  *bus->last_dev = d;
  bus->last_dev = &d->bus_next;
  d->bus_next = NULL;
  d->parent_bus = bus;
}

void
grow_tree(void)
{
  struct device *d;
  struct bridge **last_br, *b;

  last_br = &host_bridge.chain;

  /* Build list of top level domain bridges */

  for (d=first_dev; d; d=d->next)
    {
      for (b=host_bridge.chain; b; b=b->chain)
        if (b->domain == (unsigned)d->dev->domain)
          break;
      if (b)
        continue;
      b = xmalloc(sizeof(struct bridge));
      b->domain = d->dev->domain;
      b->primary = ~0;
      b->secondary = 0;
      b->subordinate = ~0;
      *last_br = b;
      last_br = &b->chain;
      b->prev = b->next = b->child = NULL;
      b->first_bus = NULL;
      b->last_bus = NULL;
      b->br_dev = NULL;
      b->chain = NULL;
      pacc->debug("Tree: domain %04x\n", b->domain);
    }

  /* Build list of bridges */

  for (d=first_dev; d; d=d->next)
    {
      struct pci_dev *dd = d->dev;
      word class = dd->device_class;
      byte ht = d->no_config_access ? -1 : (get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f);
      if ((class >> 8) == PCI_BASE_CLASS_BRIDGE &&
	  (ht == PCI_HEADER_TYPE_BRIDGE || ht == PCI_HEADER_TYPE_CARDBUS))
	{
	  b = xmalloc(sizeof(struct bridge));
	  b->domain = dd->domain;
	  b->primary = dd->bus;
	  if (ht == PCI_HEADER_TYPE_BRIDGE)
	    {
	      b->secondary = get_conf_byte(d, PCI_SECONDARY_BUS);
	      b->subordinate = get_conf_byte(d, PCI_SUBORDINATE_BUS);
	    }
	  else
	    {
	      b->secondary = get_conf_byte(d, PCI_CB_CARD_BUS);
	      b->subordinate = get_conf_byte(d, PCI_CB_SUBORDINATE_BUS);
	    }
	  *last_br = b;
	  last_br = &b->chain;
	  b->prev = b->next = b->child = NULL;
	  b->first_bus = NULL;
	  b->last_bus = NULL;
	  b->br_dev = d;
	  d->bridge = b;
	  pacc->debug("Tree: bridge %04x:%02x:%02x.%d: %02x -> %02x-%02x\n",
	    dd->domain, dd->bus, dd->dev, dd->func,
	    b->primary, b->secondary, b->subordinate);
	}
    }

  /* Append additional bridges reported by libpci via d->parent */

  for (d=first_dev; d; d=d->next)
    {
      struct device *parent = NULL;
      if (d->dev->known_fields & PCI_FILL_PARENT)
        parent = find_device(d->dev->parent);
      if (!parent || parent->bridge)
        continue;
      b = xmalloc(sizeof(struct bridge));
      b->domain = parent->dev->domain;
      b->primary = parent->dev->bus;
      b->secondary = d->dev->bus;
      /* At this stage subordinate number is unknown, so set it to secondary bus number. */
      b->subordinate = b->secondary;
      *last_br = b;
      last_br = &b->chain;
      b->prev = b->next = b->child = NULL;
      b->first_bus = NULL;
      b->last_bus = NULL;
      b->br_dev = parent;
      parent->bridge = b;
      pacc->debug("Tree: bridge %04x:%02x:%02x.%d\n", b->domain,
        parent->dev->bus, parent->dev->dev, parent->dev->func);
    }
  *last_br = NULL;

  /* Create a bridge tree */

  for (b=host_bridge.chain; b; b=b->chain)
    {
      struct device *br_dev = b->br_dev;
      struct bridge *c, *best = NULL;
      struct device *parent = NULL;

      if (br_dev && (br_dev->dev->known_fields & PCI_FILL_PARENT))
        parent = find_device(br_dev->dev->parent);
      if (parent)
        best = parent->bridge;
      if (!best)
      for (c=&host_bridge; c; c=c->chain)
	if (c != b && (c == &host_bridge || b->domain == c->domain) &&
	    b->primary >= c->secondary && b->primary <= c->subordinate &&
	    (!best || best == &host_bridge || best->subordinate - best->primary > c->subordinate - c->primary))
	  best = c;
      if (best)
	{
	  b->prev = best->child;
	  best->child = b;
	}
    }

  /* Insert secondary bus for each bridge */

  for (b=host_bridge.chain; b; b=b->chain)
    if (b->br_dev && !find_bus(b, b->domain, b->secondary))
      new_bus(b, b->domain, b->secondary);

  /* Create bus structs and link devices */

  for (d=first_dev; d; d=d->next)
    insert_dev(d, &host_bridge);
}

#define LINE_BUF_SIZE 1024

static void
print_it(char *line, char *p)
{
  *p = 0;
  fputs(line, stdout);
  if (p >= line + LINE_BUF_SIZE - 1)
    fputs("...", stdout);
  putchar('\n');
  for (p=line; *p; p++)
    if (*p == '+' || *p == '|')
      *p = '|';
    else
      *p = ' ';
}

static void show_tree_bridge(struct pci_filter *filter, struct bridge *, char *, char *);

static char * FORMAT_CHECK(printf, 3, 4)
tree_printf(char *line, char *p, char *fmt, ...)
{
  va_list args;
  int space = line + LINE_BUF_SIZE - 1 - p;

  if (space <= 0)
    return p;

  va_start(args, fmt);
  int res = vsnprintf(p, space, fmt, args);
  if (res < 0)
    {
      /* Ancient C libraries return -1 on overflow and they do not truncate the output properly. */
      *p = 0;
      p += space;
    }
  else if (res >= space)
    {
      /* Ancient C libraries do not truncate the output properly. */
      *(p+space-1) = 0;
      p += space;
    }
  else
    p += res;

  va_end(args);
  return p;
}

static void
show_tree_dev(struct pci_filter *filter, struct device *d, char *line, char *p)
{
  struct pci_dev *q = d->dev;
  struct bridge *b;
  char namebuf[256];

  p = tree_printf(line, p, "%02x.%x", q->dev, q->func);
  for (b=host_bridge.chain; b; b=b->chain)
    if (b->br_dev == d)
      {
	if (b->secondary == 0)
	  p = tree_printf(line, p, "-");
	else if (b->secondary == b->subordinate)
	  p = tree_printf(line, p, "-[%02x]-", b->secondary);
	else
	  p = tree_printf(line, p, "-[%02x-%02x]-", b->secondary, b->subordinate);
        show_tree_bridge(filter, b, line, p);
        return;
      }
  if (verbose)
    p = tree_printf(line, p, "  %s",
		    pci_lookup_name(pacc, namebuf, sizeof(namebuf),
				    PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
				    q->vendor_id, q->device_id));
  print_it(line, p);
}

static struct pci_filter *
get_filter_for_child(struct pci_filter *filter, struct device *d)
{
  if (!filter)
    return NULL;

  if (pci_filter_match(filter, d->dev))
    return NULL;

  return filter;
}

static int
check_bus_filter(struct pci_filter *filter, struct bus *b);

static int
check_dev_filter(struct pci_filter *filter, struct device *d)
{
  struct bridge *br;
  struct bus *b;

  if (!filter)
    return 1;

  if (pci_filter_match(filter, d->dev))
    return 1;

  for (br = host_bridge.chain; br; br = br->chain)
    if (br->br_dev == d)
      {
        for (b = br->first_bus; b; b = b->sibling)
          if (check_bus_filter(filter, b))
            return 1;
        break;
      }

  return 0;
}

static int
check_bus_filter(struct pci_filter *filter, struct bus *b)
{
  struct device *d;

  if (!filter)
    return 1;

  for (d = b->first_dev; d; d = d->bus_next)
    if (check_dev_filter(filter, d))
      return 1;

  return 0;
}

static void
show_tree_bus(struct pci_filter *filter, struct bus *b, char *line, char *p)
{
  if (!b->first_dev)
    print_it(line, p);
  else if (!b->first_dev->bus_next)
    {
      if (check_dev_filter(filter, b->first_dev))
        {
          p = tree_printf(line, p, "--");
          show_tree_dev(get_filter_for_child(filter, b->first_dev), b->first_dev, line, p);
        }
      else
        print_it(line, p);
    }
  else
    {
      int i, count = 0;
      struct device *d = b->first_dev;

      do
        {
          if (check_dev_filter(filter, d))
            count++;
          d = d->bus_next;
        }
      while (d);

      for (i = 0, d = b->first_dev; d; d = d->bus_next)
        {
          if (!check_dev_filter(filter, d))
            continue;
          char *p2 = tree_printf(line, p, count == 1 ? "--" : count == i+1 ? "\\-" : "+-");
          show_tree_dev(get_filter_for_child(filter, d), d, line, p2);
          i++;
        }

      if (count == 0)
        print_it(line, p);
    }
}

static void
show_tree_bridge(struct pci_filter *filter, struct bridge *b, char *line, char *p)
{
  *p++ = '-';
  if (!b->first_bus->sibling)
    {
      if (check_bus_filter(filter, b->first_bus))
        {
          if (!b->br_dev)
            p = tree_printf(line, p, "[%04x:%02x]-", b->first_bus->domain, b->first_bus->number);
          show_tree_bus(filter, b->first_bus, line, p);
        }
      else
        print_it(line, p);
    }
  else
    {
      int i, count = 0;
      struct bus *u = b->first_bus;
      char *k;

      do
        {
          if (check_bus_filter(filter, u))
            count++;
          u = u->sibling;
        }
      while (u);

      for (i = 0, u = b->first_bus; u; u = u->sibling)
        {
          if (!check_bus_filter(filter, u))
            continue;
          k = tree_printf(line, p, count == 1 ? "[%04x:%02x]-" : count == i+1 ? "\\-[%04x:%02x]-" : "+-[%04x:%02x]-", u->domain, u->number);
          show_tree_bus(filter, u, line, k);
          i++;
        }

      if (count == 0)
        print_it(line, p);
    }
}

void
show_forest(struct pci_filter *filter)
{
  char line[LINE_BUF_SIZE];
  struct bridge *b;
  if (host_bridge.child)
    {
      for (b=host_bridge.child; b->prev; b=b->prev)
        b->prev->next = b;
      for (; b; b=b->next)
        show_tree_bridge(filter, b, line, line);
    }
}
