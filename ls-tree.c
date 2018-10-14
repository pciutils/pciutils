/*
 *	The PCI Utilities -- Show Bus Tree
 *
 *	Copyright (c) 1997--2018 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>

#include "lspci.h"

struct bridge host_bridge = { NULL, NULL, NULL, NULL, 0, ~0, 0, ~0, NULL };

static struct bus *
find_bus(struct bridge *b, unsigned int domain, unsigned int n)
{
  struct bus *bus;

  for (bus=b->first_bus; bus; bus=bus->sibling)
    if (bus->domain == domain && bus->number == n)
      break;
  return bus;
}

static struct bus *
new_bus(struct bridge *b, unsigned int domain, unsigned int n)
{
  struct bus *bus = xmalloc(sizeof(struct bus));
  bus->domain = domain;
  bus->number = n;
  bus->sibling = b->first_bus;
  bus->first_dev = NULL;
  bus->last_dev = &bus->first_dev;
  bus->parent_bridge = b;
  b->first_bus = bus;
  return bus;
}

static void
insert_dev(struct device *d, struct bridge *b)
{
  struct pci_dev *p = d->dev;
  struct bus *bus;

  if (! (bus = find_bus(b, p->domain, p->bus)))
    {
      struct bridge *c;
      for (c=b->child; c; c=c->next)
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

  /* Build list of bridges */

  last_br = &host_bridge.chain;
  for (d=first_dev; d; d=d->next)
    {
      struct pci_dev *dd = d->dev;
      word class = dd->device_class;
      byte ht = get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f;
      if ((class >> 8) == PCI_BASE_CLASS_BRIDGE &&
	  (ht == PCI_HEADER_TYPE_BRIDGE || ht == PCI_HEADER_TYPE_CARDBUS))
	{
	  b = xmalloc(sizeof(struct bridge));
	  b->domain = dd->domain;
	  if (ht == PCI_HEADER_TYPE_BRIDGE)
	    {
	      b->primary = get_conf_byte(d, PCI_PRIMARY_BUS);
	      b->secondary = get_conf_byte(d, PCI_SECONDARY_BUS);
	      b->subordinate = get_conf_byte(d, PCI_SUBORDINATE_BUS);
	    }
	  else
	    {
	      b->primary = get_conf_byte(d, PCI_CB_PRIMARY_BUS);
	      b->secondary = get_conf_byte(d, PCI_CB_CARD_BUS);
	      b->subordinate = get_conf_byte(d, PCI_CB_SUBORDINATE_BUS);
	    }
	  *last_br = b;
	  last_br = &b->chain;
	  b->next = b->child = NULL;
	  b->first_bus = NULL;
	  b->br_dev = d;
	  d->bridge = b;
	  pacc->debug("Tree: bridge %04x:%02x:%02x.%d: %02x -> %02x-%02x\n",
	    dd->domain, dd->bus, dd->dev, dd->func,
	    b->primary, b->secondary, b->subordinate);
	}
    }
  *last_br = NULL;

  /* Create a bridge tree */

  for (b=&host_bridge; b; b=b->chain)
    {
      struct bridge *c, *best;
      best = NULL;
      for (c=&host_bridge; c; c=c->chain)
	if (c != b && (c == &host_bridge || b->domain == c->domain) &&
	    b->primary >= c->secondary && b->primary <= c->subordinate &&
	    (!best || best->subordinate - best->primary > c->subordinate - c->primary))
	  best = c;
      if (best)
	{
	  b->next = best->child;
	  best->child = b;
	}
    }

  /* Insert secondary bus for each bridge */

  for (b=&host_bridge; b; b=b->chain)
    if (!find_bus(b, b->domain, b->secondary))
      new_bus(b, b->domain, b->secondary);

  /* Create bus structs and link devices */

  for (d=first_dev; d; d=d->next)
    insert_dev(d, &host_bridge);
}

static char*
show_size(u64 x, char* out_p)
{
  static const char suffix[][2] = { "", "K", "M", "G", "T" };
  unsigned i;
  if (!x)
    return out_p;
  for (i = 0; i < (sizeof(suffix) / sizeof(*suffix) - 1); i++) {
    if (x % 1024)
      break;
    x /= 1024;
  }
  return out_p + sprintf(out_p, " [size=%u%s]", (unsigned)x, suffix[i]);
}

static void
print_it(char *line, char *p)
{
  *p++ = '\n';
  *p = 0;
  fputs(line, stdout);
  for (p=line; *p; p++)
    if (*p == '+' || *p == '|')
      *p = '|';
    else
      *p = ' ';
}

static void
show_bases(struct device *d, int cnt, char *line, char *out_pos)
{
  struct pci_dev *p = d->dev;
  word cmd = get_conf_word(d, PCI_COMMAND);
  int i;
  int virtual = 0;

  for (i=0; i<cnt; i++)
    {
      pciaddr_t pos = p->base_addr[i];
      pciaddr_t len = (p->known_fields & PCI_FILL_SIZES) ? p->size[i] : 0;
      pciaddr_t ioflg = (p->known_fields & PCI_FILL_IO_FLAGS) ? p->flags[i] : 0;
      u32 flg = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
      char* out_p = out_pos;
      if (flg == 0xffffffff)
        flg = 0;
      if (!pos && !flg && !len)
        continue;
      if (verbose > 2)
        out_p += sprintf(out_p, "  Region %d: ", i);
      else
        out_p += sprintf(out_p, "  ");
      if (ioflg & PCI_IORESOURCE_PCI_EA_BEI)
          out_p += sprintf(out_p, "[enhanced] ");
      else if (pos && !(flg & ((flg & PCI_BASE_ADDRESS_SPACE_IO) ? PCI_BASE_ADDRESS_IO_MASK : PCI_BASE_ADDRESS_MEM_MASK)))
        {
          /* Reported by the OS, but not by the device */
          out_p += sprintf(out_p, "[virtual] ");
          flg = pos;
          virtual = 1;
        }
      if (flg & PCI_BASE_ADDRESS_SPACE_IO)
        {
          pciaddr_t a = pos & PCI_BASE_ADDRESS_IO_MASK;
          out_p += sprintf(out_p, "I/O ports at ");
          if (a || (cmd & PCI_COMMAND_IO))
            out_p += sprintf(out_p, PCIADDR_PORT_FMT, a);
          else if (flg & PCI_BASE_ADDRESS_IO_MASK)
            out_p += sprintf(out_p, "<ignored>");
          else
            out_p += sprintf(out_p, "<unassigned>");
          if (!virtual && !(cmd & PCI_COMMAND_IO))
            out_p += sprintf(out_p, " [disabled]");
        }
      else
        {
          int t = flg & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
          pciaddr_t a = pos & PCI_ADDR_MEM_MASK;
          int done = 0;
          u32 z = 0;

          out_p += sprintf(out_p, "Memory at ");
          if (t == PCI_BASE_ADDRESS_MEM_TYPE_64)
            {
              if (i >= cnt - 1)
                {
                  out_p += sprintf(out_p, "<invalid-64bit-slot>");
                  done = 1;
                }
              else
                {
                  i++;
                  z = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
                }
            }
          if (!done)
            {
              if (a)
                out_p += sprintf(out_p, PCIADDR_T_FMT, a);
              else
                out_p += sprintf(out_p, ((flg & PCI_BASE_ADDRESS_MEM_MASK) || z) ? "<ignored>" : "<unassigned>");
            }
          out_p += sprintf(out_p, " (%s, %sprefetchable)",
                 (t == PCI_BASE_ADDRESS_MEM_TYPE_32) ? "32-bit" :
                 (t == PCI_BASE_ADDRESS_MEM_TYPE_64) ? "64-bit" :
                 (t == PCI_BASE_ADDRESS_MEM_TYPE_1M) ? "low-1M" : "type 3",
                 (flg & PCI_BASE_ADDRESS_MEM_PREFETCH) ? "" : "non-");
          if (!virtual && !(cmd & PCI_COMMAND_MEMORY))
            out_p += sprintf(out_p, " [disabled]");
        }
      out_p = show_size(len, out_p);
      print_it(line, out_p);
    }
}

static void show_tree_bridge(struct bridge *, char *, char *);

static void
show_tree_dev(struct device *d, char *line, char *p)
{
  struct pci_dev *q = d->dev;
  struct bridge *b;
  char namebuf[256];
  char* bases_p;

  p += sprintf(p, "%02x.%x", q->dev, q->func);
  for (b=&host_bridge; b; b=b->chain)
    if (b->br_dev == d)
      {
	if (b->secondary == b->subordinate)
	  p += sprintf(p, "-[%02x]-", b->secondary);
	else
	  p += sprintf(p, "-[%02x-%02x]-", b->secondary, b->subordinate);
        show_tree_bridge(b, line, p);
        return;
      }
  bases_p = p + 2;
  if (verbose)
    p += sprintf(p, "  %s",
		 pci_lookup_name(pacc, namebuf, sizeof(namebuf),
				 PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
				 q->vendor_id, q->device_id));
  print_it(line, p);
  if (verbose > 1)
    show_bases(d, 6, line, bases_p);
}

static void
show_tree_bus(struct bus *b, char *line, char *p)
{
  if (!b->first_dev)
    print_it(line, p);
  else if (!b->first_dev->bus_next)
    {
      *p++ = '-';
      *p++ = '-';
      show_tree_dev(b->first_dev, line, p);
    }
  else
    {
      struct device *d = b->first_dev;
      while (d->bus_next)
	{
	  p[0] = '+';
	  p[1] = '-';
	  show_tree_dev(d, line, p+2);
	  d = d->bus_next;
	}
      p[0] = '\\';
      p[1] = '-';
      show_tree_dev(d, line, p+2);
    }
}

static void
show_tree_bridge(struct bridge *b, char *line, char *p)
{
  *p++ = '-';
  if (!b->first_bus->sibling)
    {
      if (b == &host_bridge)
        p += sprintf(p, "[%04x:%02x]-", b->domain, b->first_bus->number);
      show_tree_bus(b->first_bus, line, p);
    }
  else
    {
      struct bus *u = b->first_bus;
      char *k;

      while (u->sibling)
        {
          k = p + sprintf(p, "+-[%04x:%02x]-", u->domain, u->number);
          show_tree_bus(u, line, k);
          u = u->sibling;
        }
      k = p + sprintf(p, "\\-[%04x:%02x]-", u->domain, u->number);
      show_tree_bus(u, line, k);
    }
}

void
show_forest(struct pci_filter *filter)
{
  char line[256];
  if (filter == NULL)
    show_tree_bridge(&host_bridge, line, line);
  else
    {
      struct bridge *b;
      for (b=&host_bridge; b; b=b->chain)
        {
          if (b->br_dev && pci_filter_match(filter, b->br_dev->dev))
            {
                struct pci_dev *d = b->br_dev->dev;
                char *p = line;
                p += sprintf(line, "%04x:%02x:", d->domain_16, d->bus);
                show_tree_dev(b->br_dev, line, p);
            }
        }
    }
}
