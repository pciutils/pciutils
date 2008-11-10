/*
 *	The PCI Utilities -- Manipulate PCI Configuration Registers
 *
 *	Copyright (c) 1998--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#define PCIUTILS_SETPCI
#include "pciutils.h"

static int force;			/* Don't complain if no devices match */
static int verbose;			/* Verbosity level */
static int demo_mode;			/* Only show */

const char program_name[] = "setpci";

static struct pci_access *pacc;

struct value {
  unsigned int value;
  unsigned int mask;
};

struct op {
  struct op *next;
  struct pci_dev **dev_vector;
  unsigned int addr;
  unsigned int width;			/* Byte width of the access */
  int num_values;			/* Number of values to write; <0=read */
  struct value values[0];
};

static struct op *first_op, **last_op = &first_op;
static unsigned int max_values[] = { 0, 0xff, 0xffff, 0, 0xffffffff };

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
  char *formats[] = { NULL, "%02x", "%04x", NULL, "%08x" };
  char *mask_formats[] = { NULL, "%02x->(%02x:%02x)->%02x", "%04x->(%04x:%04x)->%04x", NULL, "%08x->(%08x:%08x)->%08x" };
  unsigned int x, y;
  int i, addr;
  int width = op->width;

  if (verbose)
    printf("%02x:%02x.%x:%02x", dev->bus, dev->dev, dev->func, op->addr);
  addr = op->addr;
  if (op->num_values >= 0)
    {
      for(i=0; i<op->num_values; i++)
	{
	  if ((op->values[i].mask & max_values[width]) == max_values[width])
	    {
	      x = op->values[i].value;
	      if (verbose)
		{
		  putchar(' ');
		  printf(formats[width], op->values[i].value);
		}
	    }
	  else
	    {
	      switch (width)
		{
		case 1:
		  y = pci_read_byte(dev, addr);
		  break;
		case 2:
		  y = pci_read_word(dev, addr);
		  break;
		default:
		  y = pci_read_long(dev, addr);
		  break;
		}
	      x = (y & ~op->values[i].mask) | op->values[i].value;
	      if (verbose)
		{
		  putchar(' ');
		  printf(mask_formats[width], y, op->values[i].value, op->values[i].mask, x);
		}
	    }
	  if (!demo_mode)
	    {
	      switch (width)
		{
		case 1:
		  pci_write_byte(dev, addr, x);
		  break;
		case 2:
		  pci_write_word(dev, addr, x);
		  break;
		default:
		  pci_write_long(dev, addr, x);
		  break;
		}
	    }
	  addr += width;
	}
      if (verbose)
	putchar('\n');
    }
  else
    {
      if (verbose)
	printf(" = ");
      switch (width)
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
      printf(formats[width], x);
      putchar('\n');
    }
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
  unsigned int offset;
  unsigned int width;
  const char *name;
};

static const struct reg_name pci_reg_names[] = {
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

static void NONRET PCI_PRINTF(1,2)
usage(char *msg, ...)
{
  va_list args;
  va_start(args, msg);
  if (msg)
    {
      fprintf(stderr, "setpci: ");
      vfprintf(stderr, msg, args);
      fprintf(stderr, "\n\n");
    }
  fprintf(stderr,
"Usage: setpci [<options>] (<device>+ <reg>[=<values>]*)*\n"
"\n"
"General options:\n"
"-f\t\tDon't complain if there's nothing to do\n"
"-v\t\tBe verbose\n"
"-D\t\tList changes, don't commit them\n"
"\n"
"PCI access options:\n"
GENERIC_HELP
"\n"
"Setting commands:\n"
"<device>:\t-s [[[<domain>]:][<bus>]:][<slot>][.[<func>]]\n"
"\t|\t-d [<vendor>]:[<device>]\n"
"<reg>:\t\t<number>[.(B|W|L)]\n"
"     |\t\t<name>\n"
"<values>:\t<value>[,<value>...]\n"
"<value>:\t<hex>\n"
"       |\t<hex>:<mask>\n");
  exit(1);
}

static int
parse_options(int argc, char **argv)
{
  char *opts = GENERIC_OPTIONS;
  int i=1;

  if (argc == 2 && !strcmp(argv[1], "--version"))
    {
      puts("setpci version " PCIUTILS_VERSION);
      return 0;
    }

  while (i < argc && argv[i][0] == '-')
    {
      char *c = argv[i]+1;
      char *d = c;
      char *e;
      while (*c)
	switch (*c)
	  {
	  case 0:
	    break;
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
	  default:
	    if (e = strchr(opts, *c))
	      {
		char *arg;
		c++;
		if (e[1] == ':')
		  {
		    if (*c)
		      arg = c;
		    else if (i+1 < argc)
		      {
			arg = argv[i+1];
			i++;
		      }
		    else
		      usage(NULL);
		    c = "";
		  }
		else
		  arg = NULL;
		if (!parse_generic_option(*e, pacc, arg))
		  usage(NULL);
	      }
	    else
	      {
		if (c != d)
		  usage(NULL);
		return i;
	      }
	  }
      i++;
    }

  return i;
}

static void parse_ops(int argc, char **argv, int i)
{
  enum { STATE_INIT, STATE_GOT_FILTER, STATE_GOT_OP } state = STATE_INIT;
  struct pci_filter filter;
  struct pci_dev **selected_devices = NULL;

  while (i < argc)
    {
      char *c = argv[i];
      char *d, *e, *f;
      int n, j;
      struct op *op;
      unsigned long ll;
      unsigned int lim;

      if (*c == '-')
	{
	  if (!c[1] || !strchr("sd", c[1]))
	    usage(NULL);
	  if (c[2])
	    d = (c[2] == '=') ? c+3 : c+2;
	  else if (argc > 1)
	    {
	      argc--;
	      argv++;
	      d = argv[0];
	    }
	  else
	    usage(NULL);
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
	      usage(NULL);
	    }
	}
      else if (state == STATE_INIT)
	usage(NULL);
      else
	{
	  if (state == STATE_GOT_FILTER)
	    selected_devices = select_devices(&filter);
	  if (!selected_devices[0] && !force)
	    fprintf(stderr, "setpci: Warning: No devices selected for `%s'.\n", c);
	  state = STATE_GOT_OP;
	  /* look for setting of values and count how many */
	  d = strchr(c, '=');
	  if (d)
	    {
	      *d++ = 0;
	      if (!*d)
		usage("Missing value");
	      for(e=d, n=1; *e; e++)
		if (*e == ',')
		  n++;
	      op = xmalloc(sizeof(struct op) + n*sizeof(struct value));
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
		usage("Missing width");
	      switch (*e & 0xdf)
		{
		case 'B':
		  op->width = 1; break;
		case 'W':
		  op->width = 2; break;
		case 'L':
		  op->width = 4; break;
		default:
		  usage("Invalid width \"%c\"", *e);
		}
	    }
	  else
	    op->width = 1;
	  ll = strtol(c, &f, 16);
	  if (f && *f)
	    {
	      const struct reg_name *r;
	      for(r = pci_reg_names; r->name; r++)
		if (!strcasecmp(r->name, c))
		  break;
	      if (!r->name)
		usage("Unknown register \"%s\"", c);
	      if (e && op->width != r->width)
		usage("Explicit width doesn't correspond with the named register \"%s\"", c);
	      ll = r->offset;
	      op->width = r->width;
	    }
	  if (ll > 0x1000 || ll + op->width*((n < 0) ? 1 : n) > 0x1000)
	    die("Register number out of range!");
	  if (ll & (op->width - 1))
	    die("Unaligned register address!");
	  op->addr = ll;
	  /* read in all the values to be set */
	  for(j=0; j<n; j++)
	    {
	      e = strchr(d, ',');
	      if (e)
		*e++ = 0;
	      ll = strtoul(d, &f, 16);
	      lim = max_values[op->width];
	      if (f && *f && *f != ':')
		usage("Invalid value \"%s\"", d);
	      if (ll > lim && ll < ~0UL - lim)
		usage("Value \"%s\" is out of range", d);
	      op->values[j].value = ll;
	      if (f && *f == ':')
		{
		  d = ++f;
		  ll = strtoul(d, &f, 16);
		  if (f && *f)
		    usage("Invalid mask \"%s\"", d);
		  if (ll > lim && ll < ~0UL - lim)
		    usage("Mask \"%s\" is out of range", d);
		  op->values[j].mask = ll;
		  op->values[j].value &= ll;
		}
	      else
		op->values[j].mask = ~0U;
	      d = e;
	    }
	  *last_op = op;
	  last_op = &op->next;
	  op->next = NULL;
	}
      i++;
    }
  if (state == STATE_INIT)
    usage("No operation specified");
}

int
main(int argc, char **argv)
{
  int i;

  pacc = pci_alloc();
  pacc->error = die;
  i = parse_options(argc, argv);

  pci_init(pacc);
  pci_scan_bus(pacc);

  parse_ops(argc, argv, i);
  scan_ops(first_op);
  execute(first_op);

  return 0;
}
