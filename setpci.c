/*
 *	$Id: setpci.c,v 1.1 1998/03/31 21:02:20 mj Exp $
 *
 *	Linux PCI Utilities -- Manipulate PCI Configuration Registers
 *
 *	Copyright (c) 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "pciutils.h"

static int force;			/* Don't complain if no devices match */
static int verbose;			/* Verbosity level */

struct device {
  struct device *next;
  byte bus, devfn, mark;
  word vendid, devid;
  int fd;
};

static struct device *first_dev;

struct op {
  struct op *next;
  struct device **dev_vector;
  unsigned int addr;
  unsigned int width;			/* Byte width of the access */
  int num_values;			/* Number of values to write; <0=read */
  unsigned int values[0];
};

static struct op *first_op, **last_op = &first_op;

void *
xmalloc(unsigned int howmuch)
{
  void *p = malloc(howmuch);
  if (!p)
    {
      fprintf(stderr, "setpci: Unable to allocate %d bytes of memory\n", howmuch);
      exit(1);
    }
  return p;
}

static void
scan_devices(void)
{
  struct device **last = &first_dev;
  byte line[256];
  FILE *f;

  if (!(f = fopen(PROC_BUS_PCI "/devices", "r")))
    {
      perror(PROC_BUS_PCI "/devices");
      exit(1);
    }
  while (fgets(line, sizeof(line), f))
    {
      struct device *d = xmalloc(sizeof(struct device));
      unsigned int dfn, vend;

      sscanf(line, "%x %x", &dfn, &vend);
      d->bus = dfn >> 8U;
      d->devfn = dfn & 0xff;
      d->vendid = vend >> 16U;
      d->devid = vend & 0xffff;
      d->fd = -1;
      *last = d;
      last = &d->next;
    }
  fclose(f);
  *last = NULL;
}

static struct device **
select_devices(struct pci_filter *filt)
{
  struct device *z, **a, **b;
  int cnt = 1;

  for(z=first_dev; z; z=z->next)
    if (z->mark = filter_match(filt, z->bus, z->devfn, z->vendid, z->devid))
      cnt++;
  a = b = xmalloc(sizeof(struct device *) * cnt);
  for(z=first_dev; z; z=z->next)
    if (z->mark)
      *a++ = z;
  *a = NULL;
  return b;
}

static void
exec_op(struct op *op, struct device *dev)
{
  char *mm[] = { NULL, "%02x", "%04x", NULL, "%08x" };
  char *m = mm[op->width];

  if (dev->fd < 0)
    {
      char name[64];
      sprintf(name, PROC_BUS_PCI "/%02x/%02x.%x", dev->bus, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
      dev->fd = open(name, O_RDWR ????
    }

  if (verbose)
    printf("%02x.%02x:%x.%c ", dev->bus, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
	   "?BW?L"[op->width]);
  if (op->num_values > 0)
    {
    }
  else
    {
      if (verbose)
	printf("= ");
    }
}

static void
execute(struct op *op)
{
  struct device **vec = NULL;
  struct device **pdev, *dev;
  struct op *oops;

  while (op)
    {
      pdev = vec = op->dev_vector;
      while (dev = *pdev++)
	for(oops=op; oops && oops->dev_vector == vec; oops=oops->next)
	  exec_op(oops, dev);
      while (op && op->dev_vector == vec)
	op = op->next;
    }
}

static void usage(void) __attribute__((noreturn));

static void
usage(void)
{
  fprintf(stderr,
"Usage: setpci [-f] [-v] (<device>+ <reg>[=<values>]*)*\n\
<device>:  -s [[<bus>]:][<slot>][.[<func>]]\n\
\t|  -d [<vendor>]:[<device>]\n\
<reg>:     <number>[.(B|W|L)]\n\
<values>:  <value>[,<value>...]\n\
");
  exit(1);
}

int
main(int argc, char **argv)
{
  enum { STATE_INIT, STATE_GOT_FILTER, STATE_GOT_OP } state = STATE_INIT;
  struct pci_filter filter;
  struct device **selected_devices = NULL;

  argc--;
  argv++;
  while (argc && argv[0][0] == '-')
    {
      char *c = argv[0]+1;
      char *d = c;
      while (*c)
	switch (*c)
	  {
	  case 'v':
	    verbose++;
	    c++;
	    break;
	  case 'f':
	    force++;
	    c++;
	    break;
	  case 0:
	    break;
	  default:
	    if (c != d)
	      usage();
	    goto next;
	  }
      argc--;
      argv++;
    }
next:

  scan_devices();

  while (argc)
    {
      char *c = argv[0];
      char *d, *e, *f;
      int n, i;
      struct op *op;
      unsigned long ll, lim;

      if (*c == '-')
	{
	  if (!c[1] || !strchr("sd", c[1]))
	    usage();
	  if (c[2])
	    d = (c[2] == '=') ? c+3 : c+2;
	  else if (argc)
	    {
	      argc--;
	      argv++;
	      d = argv[0];
	    }
	  else
	    usage();
	  if (state != STATE_GOT_FILTER)
	    {
	      filter_init(&filter);
	      state = STATE_GOT_FILTER;
	    }
	  switch (c[1])
	    {
	    case 's':
	      if (d = filter_parse_slot(&filter, d))
		{
		  fprintf(stderr, "setpci: -s: %s\n", d);
		  return 1;
		}
	      break;
	    case 'd':
	      if (d = filter_parse_id(&filter, d))
		{
		  fprintf(stderr, "setpci: -d: %s\n", d);
		  return 1;
		}
	      break;
	    default:
	      usage();
	    }
	}
      else if (state == STATE_INIT)
	usage();
      else
	{
	  if (state == STATE_GOT_FILTER)
	    selected_devices = select_devices(&filter);
	  if (!selected_devices[0] && !force)
	    fprintf(stderr, "setpci: Warning: No devices selected for `%s'.\n", c);
	  state = STATE_GOT_OP;
	  d = strchr(c, '=');
	  if (d)
	    {
	      *d++ = 0;
	      for(e=d, n=1; *e; e++)
		if (*e == ',')
		  n++;
	      op = xmalloc(sizeof(struct op) + n*sizeof(unsigned int));
	    }
	  else
	    {
	      n = -1;
	      op = xmalloc(sizeof(struct op));
	    }
	  op->dev_vector = selected_devices;
	  op->num_values = n;
	  e = strchr(c, '.');
	  if (e)
	    {
	      *e++ = 0;
	      if (e[1])
		usage();
	      switch (*e & 0xdf)
		{
		case 'B':
		  op->width = 1; break;
		case 'W':
		  op->width = 2; break;
		case 'L':
		  op->width = 4; break;
		default:
		  usage();
		}
	    }
	  else
	    op->width = 1;
	  ll = strtol(c, &f, 16);
	  if (ll > 0x100 || ll + op->width*n > 0x100)
	    {
	      fprintf(stderr, "setpci: Register number out of range!\n");
	      return 1;
	    }
	  for(i=0; i<n; i++)
	    {
	      e = strchr(d, ',');
	      if (e)
		*e++ = 0;
	      ll = strtoul(d, &f, 16);
	      lim = (2 << ((op->width << 3) - 1)) - 1;
	      if (f && *f ||
		  (ll > lim && ll < ~0UL - lim))
		usage();
	      op->values[i] = ll;
	      d = e;
	    }
	  *last_op = op;
	  last_op = &op->next;
	  op->next = NULL;
	}
      argc--;
      argv++;
    }
  if (state == STATE_INIT)
    usage();

  execute(first_op);

  return 0;
}
