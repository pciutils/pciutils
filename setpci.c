/*
 *	$Id: setpci.c,v 1.4 1998/06/09 19:22:05 mj Exp $
 *
 *	Linux PCI Utilities -- Manipulate PCI Configuration Registers
 *
 *	Copyright (c) 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <asm/byteorder.h>

#include <asm/unistd.h>
#ifdef __GLIBC__
#include <syscall-list.h>
#endif

#include "pciutils.h"

static int force;			/* Don't complain if no devices match */
static int verbose;			/* Verbosity level */
static int demo_mode;			/* Only show */

struct device {
  struct device *next;
  byte bus, devfn, mark;
  word vendid, devid;
  int fd, need_write;
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

/*
 * As libc doesn't support pread/pwrite yet, we have to call them directly
 * or use lseek/read/write instead.
 */
#ifdef __GLIBC__
#ifndef SYS_pread
#define SYS_pread __NR_pread
#endif
static int
pread(unsigned int fd, void *buf, size_t size, loff_t where)
{
  return syscall(SYS_pread, fd, buf, size, where);
}

#ifndef SYS_pwrite
#define SYS_pwrite __NR_pwrite
#endif
static int
pwrite(unsigned int fd, void *buf, size_t size, loff_t where)
{
  return syscall(SYS_pwrite, fd, buf, size, where);
}
#else
static _syscall4(int, pread, unsigned int, fd, void *, buf, size_t, size, loff_t, where);
static _syscall4(int, pwrite, unsigned int, fd, void *, buf, size_t, size, loff_t, where);
#endif

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
  unsigned int x;
  int i;
  __u32 x32;
  __u16 x16;
  __u8 x8;

  if (!demo_mode && dev->fd < 0)
    {
      char name[64];
      sprintf(name, PROC_BUS_PCI "/%02x/%02x.%x", dev->bus, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
      if ((dev->fd = open(name, dev->need_write ? O_RDWR : O_RDONLY)) < 0)
	{
	  perror(name);
	  exit(1);
	}
    }

  if (verbose)
    printf("%02x:%02x.%x:%02x", dev->bus, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), op->addr);
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
	    x8 = op->values[i];
	    i = pwrite(dev->fd, &x8, 1, op->addr);
	    break;
	  case 2:
	    x16 = __cpu_to_le16(op->values[i]);
	    i = pwrite(dev->fd, &x16, 2, op->addr);
	    break;
	  default:
	    x32 = __cpu_to_le32(op->values[i]);
	    i = pwrite(dev->fd, &x32, 4, op->addr);
	    break;
	  }
	if (i != (int) op->width)
	  {
	    fprintf(stderr, "Error writing to %02x:%02x.%d: %m\n", dev->bus, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	    exit(1);
	  }
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
	      i = pread(dev->fd, &x8, 1, op->addr);
	      x = x8;
	      break;
	    case 2:
	      i = pread(dev->fd, &x16, 2, op->addr);
	      x = __le16_to_cpu(x16);
	      break;
	    default:
	      i = pread(dev->fd, &x32, 4, op->addr);
	      x = __le32_to_cpu(x32);
	      break;
	    }
	  if (i != (int) op->width)
	    {
	      fprintf(stderr, "Error reading from %02x:%02x.%d: %m\n", dev->bus, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	      exit(1);
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

static void
scan_ops(struct op *op)
{
  struct device **pdev, *dev;

  while (op)
    {
      if (op->num_values >= 0)
	{
	  pdev = op->dev_vector;
	  while (dev = *pdev++)
	    dev->need_write = 1;
	}
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
"Usage: setpci [-fvD] (<device>+ <reg>[=<values>]*)*\n\
-f\t\tDon't complain if there's nothing to do\n\
-v\t\tBe verbose\n\
-D\t\tList changes, don't commit them\n\
<device>:\t-s [[<bus>]:][<slot>][.[<func>]]\n\
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
  struct device **selected_devices = NULL;

  if (argc == 2 && !strcmp(argv[1], "--version"))
    {
      puts("setpci version " PCIUTILS_VERSION);
      return 0;
    }
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
	  case 'D':
	    demo_mode++;
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
	    {
	      fprintf(stderr, "setpci: Register number out of range!\n");
	      return 1;
	    }
	  if (ll & (op->width - 1))
	    {
	      fprintf(stderr, "setpci: Unaligned register address!\n");
	      return 1;
	    }
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
