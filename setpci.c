/*
 *	$Id: setpci.c,v 1.11 2000/02/14 19:29:58 mj Exp $
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
#include <stdarg.h>
#include <unistd.h>

#include "pciutils.h"

static int force;			/* Don't complain if no devices match */
static int verbose;			/* Verbosity level */
static int demo_mode;			/* Only show */

static struct pci_access *pacc;

struct op {
  struct op *next;
  struct pci_dev **dev_vector;
  unsigned int addr;
  unsigned int width;			/* Byte width of the access */
  int num_values;			/* Number of values to write; <0=read */
  unsigned int values[0];
};

static struct op *first_op, **last_op = &first_op;

static struct pci_dev **
select_devices(struct pci_filter *filt)
{
  struct pci_dev *z, **a, **b;
  int cnt = 1;

  for(z=pacc->devices; z; z=z->next)
    if (pci_filter_match(filt, z))
      cnt++;
  a = b = xmalloc(sizeof(struct device *) * cnt);
  for(z=pacc->devices; z; z=z->next)
    if (pci_filter_match(filt, z))
      *a++ = z;
  *a = NULL;
  return b;
}

static void
exec_op(struct op *op, struct pci_dev *dev)
{
  char *mm[] = { NULL, "%02x", "%04x", NULL, "%08x" };
  char *m = mm[op->width];
  unsigned int x;
  int i, addr;

  if (verbose)
    printf("%02x:%02x.%x:%02x", dev->bus, dev->dev, dev->func, op->addr);
  addr = op->addr;
  if (op->num_values >= 0)
    for(i=0; i<op->num_values; i++)
      {
	if (verbose)
	  {
	    putchar(' ');
	    printf(m, op->values[i]);
	  }
	if (demo_mode)
	  continue;
	switch (op->width)
	  {
	  case 1:
	    pci_write_byte(dev, addr, op->values[i]);
	    break;
	  case 2:
	    pci_write_word(dev, addr, op->values[i]);
	    break;
	  default:
	    pci_write_long(dev, addr, op->values[i]);
	    break;
	  }
	addr += op->width;
      }
  else
    {
      if (verbose)
	printf(" = ");
      if (!demo_mode)
	{
	  switch (op->width)
	    {
	    case 1:
	      x = pci_read_byte(dev, addr);
	      break;
	    case 2:
	      x = pci_read_word(dev, addr);
	      break;
	    default:
	      x = pci_read_long(dev, addr);
	      break;
	    }
	  printf(m, x);
	}
      else
	putchar('?');
    }
  putchar('\n');
}

static void
execute(struct op *op)
{
  struct pci_dev **vec = NULL;
  struct pci_dev **pdev, *dev;
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

static void
scan_ops(struct op *op)
{
  while (op)
    {
      if (op->num_values >= 0)
	pacc->writeable = 1;
      op = op->next;
    }
}

struct reg_name {
  int offset;
  int width;
  char *name;
};

static struct reg_name pci_reg_names[] = {
  { 0x00, 2, "VENDOR_ID", },
  { 0x02, 2, "DEVICE_ID", },
  { 0x04, 2, "COMMAND", },
  { 0x06, 2, "STATUS", },
  { 0x08, 1, "REVISION", },
  { 0x09, 1, "CLASS_PROG", },
  { 0x0a, 2, "CLASS_DEVICE", },
  { 0x0c, 1, "CACHE_LINE_SIZE", },
  { 0x0d, 1, "LATENCY_TIMER", },
  { 0x0e, 1, "HEADER_TYPE", },
  { 0x0f, 1, "BIST", },
  { 0x10, 4, "BASE_ADDRESS_0", },
  { 0x14, 4, "BASE_ADDRESS_1", },
  { 0x18, 4, "BASE_ADDRESS_2", },
  { 0x1c, 4, "BASE_ADDRESS_3", },
  { 0x20, 4, "BASE_ADDRESS_4", },
  { 0x24, 4, "BASE_ADDRESS_5", },
  { 0x28, 4, "CARDBUS_CIS", },
  { 0x2c, 4, "SUBSYSTEM_VENDOR_ID", },
  { 0x2e, 2, "SUBSYSTEM_ID", },
  { 0x30, 4, "ROM_ADDRESS", },
  { 0x3c, 1, "INTERRUPT_LINE", },
  { 0x3d, 1, "INTERRUPT_PIN", },
  { 0x3e, 1, "MIN_GNT", },
  { 0x3f, 1, "MAX_LAT", },
  { 0x18, 1, "PRIMARY_BUS", },
  { 0x19, 1, "SECONDARY_BUS", },
  { 0x1a, 1, "SUBORDINATE_BUS", },
  { 0x1b, 1, "SEC_LATENCY_TIMER", },
  { 0x1c, 1, "IO_BASE", },
  { 0x1d, 1, "IO_LIMIT", },
  { 0x1e, 2, "SEC_STATUS", },
  { 0x20, 2, "MEMORY_BASE", },
  { 0x22, 2, "MEMORY_LIMIT", },
  { 0x24, 2, "PREF_MEMORY_BASE", },
  { 0x26, 2, "PREF_MEMORY_LIMIT", },
  { 0x28, 4, "PREF_BASE_UPPER32", },
  { 0x2c, 4, "PREF_LIMIT_UPPER32", },
  { 0x30, 2, "IO_BASE_UPPER16", },
  { 0x32, 2, "IO_LIMIT_UPPER16", },
  { 0x38, 4, "BRIDGE_ROM_ADDRESS", },
  { 0x3e, 2, "BRIDGE_CONTROL", },
  { 0x10, 4, "CB_CARDBUS_BASE", },
  { 0x14, 2, "CB_CAPABILITIES", },
  { 0x16, 2, "CB_SEC_STATUS", },
  { 0x18, 1, "CB_BUS_NUMBER", },
  { 0x19, 1, "CB_CARDBUS_NUMBER", },
  { 0x1a, 1, "CB_SUBORDINATE_BUS", },
  { 0x1b, 1, "CB_CARDBUS_LATENCY", },
  { 0x1c, 4, "CB_MEMORY_BASE_0", },
  { 0x20, 4, "CB_MEMORY_LIMIT_0", },
  { 0x24, 4, "CB_MEMORY_BASE_1", },
  { 0x28, 4, "CB_MEMORY_LIMIT_1", },
  { 0x2c, 2, "CB_IO_BASE_0", },
  { 0x2e, 2, "CB_IO_BASE_0_HI", },
  { 0x30, 2, "CB_IO_LIMIT_0", },
  { 0x32, 2, "CB_IO_LIMIT_0_HI", },
  { 0x34, 2, "CB_IO_BASE_1", },
  { 0x36, 2, "CB_IO_BASE_1_HI", },
  { 0x38, 2, "CB_IO_LIMIT_1", },
  { 0x3a, 2, "CB_IO_LIMIT_1_HI", },
  { 0x40, 2, "CB_SUBSYSTEM_VENDOR_ID", },
  { 0x42, 2, "CB_SUBSYSTEM_ID", },
  { 0x44, 4, "CB_LEGACY_MODE_BASE", },
  { 0x00, 0, NULL }
};

static void usage(void) __attribute__((noreturn));

static void
usage(void)
{
  fprintf(stderr,
"Usage: setpci [<options>] (<device>+ <reg>[=<values>]*)*\n\
-f\t\tDon't complain if there's nothing to do\n\
-v\t\tBe verbose\n\
-D\t\tList changes, don't commit them\n"
GENERIC_HELP
"<device>:\t-s [[<bus>]:][<slot>][.[<func>]]\n\
\t|\t-d [<vendor>]:[<device>]\n\
<reg>:\t\t<number>[.(B|W|L)]\n\
     |\t\t<name>\n\
<values>:\t<value>[,<value>...]\n\
");
  exit(1);
}

int
main(int argc, char **argv)
{
  enum { STATE_INIT, STATE_GOT_FILTER, STATE_GOT_OP } state = STATE_INIT;
  struct pci_filter filter;
  struct pci_dev **selected_devices = NULL;
  char *opts = GENERIC_OPTIONS ;

  if (argc == 2 && !strcmp(argv[1], "--version"))
    {
      puts("setpci version " PCIUTILS_VERSION);
      return 0;
    }
  argc--;
  argv++;

  pacc = pci_alloc();
  pacc->error = die;

  while (argc && argv[0][0] == '-')
    {
      char *c = argv[0]+1;
      char *d = c;
      char *e;
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
	  case 'D':
	    demo_mode++;
	    c++;
	    break;
	  case 0:
	    break;
	  default:
	    if (e = strchr(opts, *c))
	      {
		char *arg;
		c++;
		if (e[1] == ':')
		  {
		    if (*c)
		      arg = c;
		    else if (argc > 1)
		      {
			arg = argv[1];
			argc--; argv++;
		      }
		    else
		      usage();
		    c = "";
		  }
		else
		  arg = NULL;
		if (!parse_generic_option(*e, pacc, arg))
		  usage();
	      }
	    else
	      {
		if (c != d)
		  usage();
		goto next;
	      }
	  }
      argc--;
      argv++;
    }
next:

  pci_init(pacc);
  pci_scan_bus(pacc);

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
	  else if (argc > 1)
	    {
	      argc--;
	      argv++;
	      d = argv[0];
	    }
	  else
	    usage();
	  if (state != STATE_GOT_FILTER)
	    {
	      pci_filter_init(pacc, &filter);
	      state = STATE_GOT_FILTER;
	    }
	  switch (c[1])
	    {
	    case 's':
	      if (d = pci_filter_parse_slot(&filter, d))
		die("-s: %s", d);
	      break;
	    case 'd':
	      if (d = pci_filter_parse_id(&filter, d))
		die("-d: %s", d);
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
	      if (!*d)
		usage();
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
	  if (f && *f)
	    {
	      struct reg_name *r;
	      for(r = pci_reg_names; r->name; r++)
		if (!strcasecmp(r->name, c))
		  break;
	      if (!r->name || e)
		usage();
	      ll = r->offset;
	      op->width = r->width;
	    }
	  if (ll > 0x100 || ll + op->width*((n < 0) ? 1 : n) > 0x100)
	    die("Register number out of range!");
	  if (ll & (op->width - 1))
	    die("Unaligned register address!");
	  op->addr = ll;
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

  scan_ops(first_op);
  execute(first_op);

  return 0;
}
