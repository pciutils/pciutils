/*
 *	$Id: lspci.c,v 1.6 1998/02/08 10:58:54 mj Exp $
 *
 *	Linux PCI Utilities -- List All PCI Devices
 *
 *	Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/pci.h>

#include "pciutils.h"

/* Options */

static int verbose;			/* Show detailed information */
static int buscentric_view;		/* Show bus addresses/IRQ's instead of CPU-visible ones */
static int show_hex;			/* Show contents of config space as hexadecimal numbers */
static int bus_filter = -1;		/* Bus, slot, function, vendor and device ID filtering */
static int slot_filter = -1;
static int func_filter = -1;
static int vend_filter = -1;
static int dev_filter = -1;
static int show_tree;			/* Show bus tree */
static char *pci_dir = PROC_BUS_PCI;

static char options[] = "nvbxB:S:F:V:D:ti:p:";

static char help_msg[] = "\
Usage: lspci [<switches>]\n\
\n\
-v\tBe verbose\n\
-n\tShow numeric ID's\n\
-b\tBus-centric view (PCI addresses and IRQ's instead of those seen by the CPU)\n\
-x\tShow hex-dump of config space (-xx shows full 256 bytes)\n\
-B <bus>, -S <slot>, -F <func>, -V <vendor>, -D <device>  Show only selected devices\n\
-t\tShow bus tree\n\
-i <file>\tUse specified ID database instead of " ETC_PCI_IDS "\n\
-p <dir>\tUse specified bus directory instead of " PROC_BUS_PCI "\n\
";

/* Format strings used for IRQ numbers */

#ifdef ARCH_SPARC64
#define IRQ_FORMAT "%08x"
#else
#define IRQ_FORMAT "%d"
#endif

/* Our view of the PCI bus */

struct device {
  struct device *next;
  byte bus, devfn;
  word vendid, devid;
  unsigned int kernel_irq;
  unsigned long kernel_base_addr[6];
  byte config[256];
};

static struct device *first_dev, **last_dev = &first_dev;

/* Miscellaneous routines */

void *
xmalloc(unsigned int howmuch)
{
  void *p = malloc(howmuch);
  if (!p)
    {
      fprintf(stderr, "lspci: Unable to allocate %d bytes of memory\n", howmuch);
      exit(1);
    }
  return p;
}

/* Filtering */

static inline int
filter_out(struct device *d)
{
  return (bus_filter >= 0 && d->bus != bus_filter ||
	  slot_filter >= 0 && PCI_SLOT(d->devfn) != slot_filter ||
	  func_filter >= 0 && PCI_FUNC(d->devfn) != func_filter ||
	  vend_filter >= 0 && d->vendid != vend_filter ||
	  dev_filter >= 0 && d->devid != dev_filter);
}

/* Interface for /proc/bus/pci */

static void
scan_dev_list(void)
{
  FILE *f;
  byte line[256];
  byte name[256];

  sprintf(name, "%s/devices", pci_dir);
  if (! (f = fopen(name, "r")))
    {
      perror(name);
      exit(1);
    }
  while (fgets(line, sizeof(line), f))
    {
      struct device *d = xmalloc(sizeof(struct device));
      unsigned int dfn, vend;

      sscanf(line, "%x %x %x %lx %lx %lx %lx %lx %lx",
	     &dfn,
	     &vend,
	     &d->kernel_irq,
	     &d->kernel_base_addr[0],
	     &d->kernel_base_addr[1],
	     &d->kernel_base_addr[2],
	     &d->kernel_base_addr[3],
	     &d->kernel_base_addr[4],
	     &d->kernel_base_addr[5]);
      d->bus = dfn >> 8U;
      d->devfn = dfn & 0xff;
      d->vendid = vend >> 16U;
      d->devid = vend & 0xffff;
      if (!filter_out(d))
	{
	  *last_dev = d;
	  last_dev = &d->next;
	  d->next = NULL;
	}
    }
  fclose(f);
}

static inline void
make_proc_pci_name(struct device *d, char *p)
{
  sprintf(p, "%s/%02x/%02x.%x",
	  pci_dir, d->bus, PCI_SLOT(d->devfn), PCI_FUNC(d->devfn));
}

static void
scan_config(void)
{
  struct device *d;
  char name[64];
  int fd, res;
  int how_much = (show_hex > 1) ? 256 : 64;

  for(d=first_dev; d; d=d->next)
    {
      make_proc_pci_name(d, name);
      if ((fd = open(name, O_RDONLY)) < 0)
	{
	  fprintf(stderr, "lspci: Unable to open %s: %m\n", name);
	  exit(1);
	}
      res = read(fd, d->config, how_much);
      if (res < 0)
	{
	  fprintf(stderr, "lspci: Error reading %s: %m\n", name);
	  exit(1);
	}
      if (res != how_much)
	{
	  fprintf(stderr, "lspci: Only %d bytes of config space available to you\n", res);
	  exit(1);
	}
      close(fd);
    }
}

static void
scan_proc(void)
{
  scan_dev_list();
  scan_config();
}

/* Config space accesses */

static inline byte
get_conf_byte(struct device *d, unsigned int pos)
{
  return d->config[pos];
}

static word
get_conf_word(struct device *d, unsigned int pos)
{
  return d->config[pos] | (d->config[pos+1] << 8);
}

static u32
get_conf_long(struct device *d, unsigned int pos)
{
  return d->config[pos] |
    (d->config[pos+1] << 8) |
    (d->config[pos+2] << 16) |
    (d->config[pos+3] << 24);
}

/* Sorting */

static int
compare_them(const void *A, const void *B)
{
  const struct device *a = *(const struct device **)A;
  const struct device *b = *(const struct device **)B;

  if (a->bus < b->bus)
    return -1;
  if (a->bus > b->bus)
    return 1;
  if (a->devfn < b->devfn)
    return -1;
  if (a->devfn > b->devfn)
    return 1;
  return 0;
}

static void
sort_them(void)
{
  struct device **index, **h;
  int cnt;
  struct device *d;

  cnt = 0;
  for(d=first_dev; d; d=d->next)
    cnt++;
  h = index = alloca(sizeof(struct device *) * cnt);
  for(d=first_dev; d; d=d->next)
    *h++ = d;
  qsort(index, cnt, sizeof(struct device *), compare_them);
  last_dev = &first_dev;
  h = index;
  while (cnt--)
    {
      *last_dev = *h;
      last_dev = &(*h)->next;
      h++;
    }
  *last_dev = NULL;
}

/* Normal output */

static void
show_terse(struct device *d)
{
  int c;

  printf("%02x:%02x.%x %s: %s",
	 d->bus,
	 PCI_SLOT(d->devfn),
	 PCI_FUNC(d->devfn),
	 lookup_class(get_conf_word(d, PCI_CLASS_DEVICE)),
	 lookup_device_full(d->vendid, d->devid));
  if (c = get_conf_byte(d, PCI_REVISION_ID))
    printf(" (rev %02x)", c);
  if (verbose && (c = get_conf_byte(d, PCI_CLASS_PROG)))
    printf(" (prog-if %02x)", c);
  putchar('\n');
}

static void
show_bases(struct device *d, int cnt)
{
  word cmd = get_conf_word(d, PCI_COMMAND);
  int i;

  for(i=0; i<6; i++)
    {
      unsigned long pos;
      unsigned int flg = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
      if (buscentric_view)
	pos = flg;
      else
	pos = d->kernel_base_addr[i];
      if (!pos || pos == 0xffffffff)
	continue;
      if (flg & PCI_BASE_ADDRESS_SPACE_IO)
	{
	  if (cmd & PCI_COMMAND_IO)
	    {
	      if (verbose > 1)
		printf("\tRegion %d: ", i);
	      else
		putchar('\t');
	      printf("I/O ports at %04lx\n", pos & PCI_BASE_ADDRESS_IO_MASK);
	    }
	}
      else if (cmd & PCI_COMMAND_MEMORY)
	{
	  int t = flg & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
	  if (verbose > 1)
	    printf("\tRegion %d: ", i);
	  else
	    putchar('\t');
	  printf("Memory at ");
	  if (t == PCI_BASE_ADDRESS_MEM_TYPE_64)
	    {
	      if (i < cnt - 1)
		{
		  i++;
		  if (!buscentric_view)
		    printf("%08x", get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i));
		}
	      else
		printf("????????");
	    }
	  printf("%08lx (%s, %sprefetchable)\n",
		 pos & PCI_BASE_ADDRESS_MEM_MASK,
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_32) ? "32-bit" :
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_64) ? "64-bit" :
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_1M) ? "low-1M 32-bit" : "???",
		 (flg & PCI_BASE_ADDRESS_MEM_PREFETCH) ? "" : "non-");
	}
    }
}

static void
show_htype0(struct device *d)
{
  u32 rom = get_conf_long(d, PCI_ROM_ADDRESS);

  show_bases(d, 6);

  if (rom & 1)
    {
      word cmd = get_conf_word(d, PCI_COMMAND);
      printf("\tExpansion ROM at %08x%s\n", rom & ~0xfff,
	     (cmd & PCI_COMMAND_MEMORY) ? "" : " [disabled]");
    }
}

static void
show_htype1(struct device *d)
{
  u32 io_base = get_conf_byte(d, PCI_IO_BASE);
  u32 io_limit = get_conf_byte(d, PCI_IO_LIMIT);
  u32 io_type = io_base & PCI_IO_RANGE_TYPE_MASK;
  u32 mem_base = get_conf_word(d, PCI_MEMORY_BASE);
  u32 mem_limit = get_conf_word(d, PCI_MEMORY_LIMIT);
  u32 mem_type = mem_base & PCI_MEMORY_RANGE_TYPE_MASK;
  u32 pref_base = get_conf_word(d, PCI_PREF_MEMORY_BASE);
  u32 pref_limit = get_conf_word(d, PCI_PREF_MEMORY_LIMIT);
  u32 pref_type = pref_base & PCI_PREF_RANGE_TYPE_MASK;
  u32 rom = get_conf_long(d, PCI_ROM_ADDRESS1);
  word brc = get_conf_word(d, PCI_BRIDGE_CONTROL);

  show_bases(d, 2);
  printf("\tBus: primary=%02x, secondary=%02x, subordinate=%02x, sec-latency=%d\n",
	 get_conf_byte(d, PCI_PRIMARY_BUS),
	 get_conf_byte(d, PCI_SECONDARY_BUS),
	 get_conf_byte(d, PCI_SUBORDINATE_BUS),
	 get_conf_byte(d, PCI_SEC_LATENCY_TIMER));

  if (io_type != (io_limit & PCI_IO_RANGE_TYPE_MASK) ||
      (io_type != PCI_IO_RANGE_TYPE_16 && io_type != PCI_IO_RANGE_TYPE_32))
    printf("\t!!! Unknown I/O range types %x/%x\n", io_base, io_limit);
  else
    {
      io_base = (io_base & PCI_IO_RANGE_MASK) << 8;
      io_limit = (io_limit & PCI_IO_RANGE_MASK) << 8;
      if (io_type == PCI_IO_RANGE_TYPE_32)
	{
	  io_base |= (get_conf_word(d, PCI_IO_BASE_UPPER16) << 16);
	  io_limit |= (get_conf_word(d, PCI_IO_LIMIT_UPPER16) << 16);
	}
      if (io_base)
	printf("\tI/O behind bridge: %08x-%08x\n", io_base, io_limit+0xfff);
    }

  if (mem_type != (mem_limit & PCI_MEMORY_RANGE_TYPE_MASK) ||
      mem_type)
    printf("\t!!! Unknown memory range types %x/%x\n", mem_base, mem_limit);
  else if (mem_base)
    {
      mem_base = (mem_base & PCI_MEMORY_RANGE_MASK) << 16;
      mem_limit = (mem_limit & PCI_MEMORY_RANGE_MASK) << 16;
      printf("\tMemory behind bridge: %08x-%08x\n", mem_base, mem_limit + 0xfffff);
    }

  if (pref_type != (pref_limit & PCI_PREF_RANGE_TYPE_MASK) ||
      (pref_type != PCI_PREF_RANGE_TYPE_32 && pref_type != PCI_PREF_RANGE_TYPE_64))
    printf("\t!!! Unknown prefetchable memory range types %x/%x\n", pref_base, pref_limit);
  else if (pref_base)
    {
      pref_base = (pref_base & PCI_PREF_RANGE_MASK) << 16;
      pref_limit = (pref_limit & PCI_PREF_RANGE_MASK) << 16;
      if (pref_type == PCI_PREF_RANGE_TYPE_32)
	printf("\tPrefetchable memory behind bridge: %08x-%08x\n", pref_base, pref_limit);
      else
	printf("\tPrefetchable memory behind bridge: %08x%08x-%08x%08x\n",
	       get_conf_long(d, PCI_PREF_BASE_UPPER32),
	       pref_base,
	       get_conf_long(d, PCI_PREF_LIMIT_UPPER32),
	       pref_limit);
    }

  if (get_conf_word(d, PCI_SEC_STATUS) & PCI_STATUS_SIG_SYSTEM_ERROR)
    printf("\tSecondary status: SERR\n");

  if (rom & 1)
    {
      word cmd = get_conf_word(d, PCI_COMMAND);
      printf("\tExpansion ROM at %08x%s\n", rom & ~0xfff,
	     (cmd & PCI_COMMAND_MEMORY) ? "" : " [disabled]");
    }

  if (verbose > 1)
    printf("\tBridgeCtl: Parity%c SERR%c NoISA%c VGA%c MAbort%c >Reset%c FastB2B%c\n",
	   (brc & PCI_BRIDGE_CTL_PARITY) ? '+' : '-',
	   (brc & PCI_BRIDGE_CTL_SERR) ? '+' : '-',
	   (brc & PCI_BRIDGE_CTL_NO_ISA) ? '+' : '-',
	   (brc & PCI_BRIDGE_CTL_VGA) ? '+' : '-',
	   (brc & PCI_BRIDGE_CTL_MASTER_ABORT) ? '+' : '-',
	   (brc & PCI_BRIDGE_CTL_BUS_RESET) ? '+' : '-',
	   (brc & PCI_BRIDGE_CTL_FAST_BACK) ? '+' : '-');
}

static void
show_htype2(struct device *d)
{
}

static void
show_verbose(struct device *d)
{
  word status = get_conf_word(d, PCI_STATUS);
  word cmd = get_conf_word(d, PCI_COMMAND);
  word class = get_conf_word(d, PCI_CLASS_DEVICE);
  byte bist = get_conf_byte(d, PCI_BIST);
  byte htype = get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f;
  byte latency = get_conf_byte(d, PCI_LATENCY_TIMER);
  byte cache_line = get_conf_byte(d, PCI_CACHE_LINE_SIZE);
  byte max_lat, min_gnt;
  byte int_pin = get_conf_byte(d, PCI_INTERRUPT_PIN);
  byte int_line = get_conf_byte(d, PCI_INTERRUPT_LINE);
  unsigned int irq;
  word subsys_v, subsys_d;

  show_terse(d);

  switch (htype)
    {
    case PCI_HEADER_TYPE_NORMAL:
      if (class == PCI_CLASS_BRIDGE_PCI)
	{
	badhdr:
	  printf("\t!!! Header type %02x doesn't match class code %04x\n", htype, class);
	  return;
	}
      max_lat = get_conf_byte(d, PCI_MAX_LAT);
      min_gnt = get_conf_byte(d, PCI_MIN_GNT);
      subsys_v = get_conf_word(d, PCI_SUBSYSTEM_VENDOR_ID);
      subsys_d = get_conf_word(d, PCI_SUBSYSTEM_ID);
      break;
    case PCI_HEADER_TYPE_BRIDGE:
      if (class != PCI_CLASS_BRIDGE_PCI)
	goto badhdr;
      irq = int_line = int_pin = min_gnt = max_lat = 0;
      subsys_v = subsys_d = 0;
      break;
    case PCI_HEADER_TYPE_CARDBUS:
      if ((class >> 8) != PCI_BASE_CLASS_BRIDGE)
	goto badhdr;
      irq = int_line = int_pin = min_gnt = max_lat = 0;
      subsys_v = subsys_d = 0;
      break;
    default:
      printf("\t!!! Unknown header type %02x\n", htype);
      return;
    }

  if (buscentric_view)
    irq = int_line;
  else
    irq = d->kernel_irq;

  if (verbose > 1)
    {
      if (subsys_v)
	printf("\tSubsystem ID: %04x:%04x\n", subsys_v, subsys_d);
      printf("\tControl: I/O%c Mem%c BusMaster%c SpecCycle%c MemWINV%c VGASnoop%c ParErr%c Stepping%c SERR%c FastB2B%c\n",
	     (cmd & PCI_COMMAND_IO) ? '+' : '-',
	     (cmd & PCI_COMMAND_MEMORY) ? '+' : '-',
	     (cmd & PCI_COMMAND_MASTER) ? '+' : '-',
	     (cmd & PCI_COMMAND_SPECIAL) ? '+' : '-',
	     (cmd & PCI_COMMAND_INVALIDATE) ? '+' : '-',
	     (cmd & PCI_COMMAND_VGA_PALETTE) ? '+' : '-',
	     (cmd & PCI_COMMAND_PARITY) ? '+' : '-',
	     (cmd & PCI_COMMAND_WAIT) ? '+' : '-',
	     (cmd & PCI_COMMAND_SERR) ? '+' : '-',
	     (cmd & PCI_COMMAND_FAST_BACK) ? '+' : '-');
      printf("\tStatus: 66Mhz%c UDF%c FastB2B%c ParErr%c DEVSEL=%s >TAbort%c <TAbort%c <MAbort%c >SERR%c <PERR%c\n",
	     (status & PCI_STATUS_66MHZ) ? '+' : '-',
	     (status & PCI_STATUS_UDF) ? '+' : '-',
	     (status & PCI_STATUS_FAST_BACK) ? '+' : '-',
	     (status & PCI_STATUS_PARITY) ? '+' : '-',
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_SLOW) ? "slow" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_MEDIUM) ? "medium" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_FAST) ? "fast" : "??",
	     (status & PCI_STATUS_SIG_TARGET_ABORT) ? '+' : '-',
	     (status & PCI_STATUS_REC_TARGET_ABORT) ? '+' : '-',
	     (status & PCI_STATUS_REC_MASTER_ABORT) ? '+' : '-',
	     (status & PCI_STATUS_SIG_SYSTEM_ERROR) ? '+' : '-',
	     (status & PCI_STATUS_DETECTED_PARITY) ? '+' : '-');
      if (cmd & PCI_COMMAND_MASTER)
	{
	  printf("\tLatency: ");
	  if (min_gnt)
	    printf("%d min, ", min_gnt);
	  if (max_lat)
	    printf("%d max, ", max_lat);
	  printf("%d set", latency);
	  if (cache_line)
	    printf(", cache line size %02x", cache_line);
	  putchar('\n');
	}
      if (int_pin)
	printf("\tInterrupt: pin %c routed to IRQ " IRQ_FORMAT "\n", 'A' + int_pin - 1, irq);
    }
  else
    {
      printf("\tFlags: ");
      if (cmd & PCI_COMMAND_MASTER)
	printf("bus master, ");
      if (cmd & PCI_COMMAND_VGA_PALETTE)
	printf("VGA palette snoop, ");
      if (cmd & PCI_COMMAND_WAIT)
	printf("stepping, ");
      if (cmd & PCI_COMMAND_FAST_BACK)
	printf("fast Back2Back, ");
      if (status & PCI_STATUS_66MHZ)
	printf("66Mhz, ");
      if (status & PCI_STATUS_UDF)
	printf("user-definable features, ");
      printf("%s devsel",
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_SLOW) ? "slow" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_MEDIUM) ? "medium" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_FAST) ? "fast" : "??");
      if (cmd & PCI_COMMAND_MASTER)
	printf(", latency %d", latency);
      if (int_pin)
	if (d->kernel_irq)
	  printf(", IRQ " IRQ_FORMAT, irq);
	else
	  printf(", IRQ ?");
      putchar('\n');
    }

  if (bist & PCI_BIST_CAPABLE)
    {
      if (bist & PCI_BIST_START)
	printf("\tBIST is running\n");
      else
	printf("\tBIST result: %02x\n", bist & PCI_BIST_CODE_MASK);
    }

  switch (htype)
    {
    case PCI_HEADER_TYPE_NORMAL:
      show_htype0(d);
      break;
    case PCI_HEADER_TYPE_BRIDGE:
      show_htype1(d);
      break;
    case PCI_HEADER_TYPE_CARDBUS:
      show_htype2(d);
      break;
    }
}

static void
show_hex_dump(struct device *d)
{
  int i;
  int limit = (show_hex > 1) ? 256 : 64;

  for(i=0; i<limit; i++)
    {
      if (! (i & 15))
	printf("%02x:", i);
      printf(" %02x", get_conf_byte(d, i));
      if ((i & 15) == 15)
	putchar('\n');
    }
}

static void
show(void)
{
  struct device *d;

  for(d=first_dev; d; d=d->next)
    {
      if (verbose)
	show_verbose(d);
      else
	show_terse(d);
      if (show_hex)
	show_hex_dump(d);
      if (verbose || show_hex)
	putchar('\n');
    }
}

/* Tree output */

struct bridge {
  struct bridge *chain;			/* Single-linked list of bridges */
  struct bridge *next, *child;		/* Tree of bridges */
  struct bus *first_bus;		/* List of busses connected to this bridge */
  unsigned int primary, secondary, subordinate;	/* Bus numbers */
  struct device *br_dev;
};

struct bus {
  unsigned int number;
  struct bus *sibling;
  struct device *first_dev, **last_dev;
};

static struct bridge host_bridge = { NULL, NULL, NULL, NULL, ~0, 0, ~0, NULL };

static struct bus *
find_bus(struct bridge *b, unsigned int n)
{
  struct bus *bus;

  for(bus=b->first_bus; bus; bus=bus->sibling)
    if (bus->number == n)
      break;
  return bus;
}

static struct bus *
new_bus(struct bridge *b, unsigned int n)
{
  struct bus *bus = xmalloc(sizeof(struct bus));

  bus = xmalloc(sizeof(struct bus));
  bus->number = n;
  bus->sibling = b->first_bus;
  bus->first_dev = NULL;
  bus->last_dev = &bus->first_dev;
  b->first_bus = bus;
  return bus;
}

static void
insert_dev(struct device *d, struct bridge *b)
{
  struct bus *bus;

  if (! (bus = find_bus(b, d->bus)))
    {
      struct bridge *c;
      for(c=b->child; c; c=c->next)
	if (c->secondary <= d->bus && d->bus <= c->subordinate)
	  return insert_dev(d, c);
      bus = new_bus(b, d->bus);
    }
  /* Simple insertion at the end _does_ guarantee the correct order as the
   * original device list was sorted by (bus, devfn) lexicographically
   * and all devices on the new list have the same bus number.
   */
  *bus->last_dev = d;
  bus->last_dev = &d->next;
  d->next = NULL;
}

static void
grow_tree(void)
{
  struct device *d, *d2;
  struct bridge *first_br, *b;

  /* Build list of bridges */

  first_br = &host_bridge;
  for(d=first_dev; d; d=d->next)
    {
      word class = get_conf_word(d, PCI_CLASS_DEVICE);
      if (class == PCI_CLASS_BRIDGE_PCI && (get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f) == 1)
	{
	  b = xmalloc(sizeof(struct bridge));
	  b->primary = get_conf_byte(d, PCI_PRIMARY_BUS);
	  b->secondary = get_conf_byte(d, PCI_SECONDARY_BUS);
	  b->subordinate = get_conf_byte(d, PCI_SUBORDINATE_BUS);
	  b->chain = first_br;
	  first_br = b;
	  b->next = b->child = NULL;
	  b->first_bus = NULL;
	  b->br_dev = d;
	}
    }

  /* Create a bridge tree */

  for(b=first_br; b; b=b->chain)
    {
      struct bridge *c, *best;
      best = NULL;
      for(c=first_br; c; c=c->chain)
	if (c != b && b->primary >= c->secondary && b->primary <= c->subordinate &&
	    (!best || best->subordinate - best->primary > c->subordinate - c->primary))
	  best = c;
      if (best)
	{
	  b->next = best->child;
	  best->child = b;
	}
    }

  /* Insert secondary bus for each bridge */

  for(b=first_br; b; b=b->chain)
    if (!find_bus(b, b->secondary))
      new_bus(b, b->secondary);

  /* Create bus structs and link devices */

  for(d=first_dev; d;)
    {
      d2 = d->next;
      insert_dev(d, &host_bridge);
      d = d2;
    }
}

static void
print_it(byte *line, byte *p)
{
  *p++ = '\n';
  *p = 0;
  fputs(line, stdout);
  for(p=line; *p; p++)
    if (*p == '+')
      *p = '|';
    else
      *p = ' ';
}

static void show_tree_bridge(struct bridge *, byte *, byte *);

static void
show_tree_dev(struct device *d, byte *line, byte *p)
{
  struct bridge *b;

  p += sprintf(p, "%02x.%x", PCI_SLOT(d->devfn), PCI_FUNC(d->devfn));
  for(b=&host_bridge; b; b=b->chain)
    if (b->br_dev == d)
      {
	p += sprintf(p, "-[%02x-%02x]-", b->secondary, b->subordinate);
        show_tree_bridge(b, line, p);
        return;
      }
  if (verbose)
    p += sprintf(p, "  %s", lookup_device_full(d->vendid, d->devid));
  print_it(line, p);
}

static void
show_tree_bus(struct bus *b, byte *line, byte *p)
{
  if (!b->first_dev)
    print_it(line, p);
  else if (!b->first_dev->next)
    {
      *p++ = '-';
      *p++ = '-';
      show_tree_dev(b->first_dev, line, p);
    }
  else
    {
      struct device *d = b->first_dev;
      while (d->next)
	{
	  p[0] = '+';
	  p[1] = '-';
	  show_tree_dev(d, line, p+2);
	  d = d->next;
	}
      p[0] = '\\';
      p[1] = '-';
      show_tree_dev(d, line, p+2);
    }
}

static void
show_tree_bridge(struct bridge *b, byte *line, byte *p)
{
  *p++ = '-';
  if (!b->first_bus->sibling)
    {
      if (b == &host_bridge)
        p += sprintf(p, "[%02x]-", b->first_bus->number);
      show_tree_bus(b->first_bus, line, p);
    }
  else
    {
      struct bus *u = b->first_bus;
      byte *k;

      while (u->sibling)
        {
          k = p + sprintf(p, "+-[%02x]-", u->number);
          show_tree_bus(u, line, k);
          u = u->sibling;
        }
      k = p + sprintf(p, "\\-[%02x]-", u->number);
      show_tree_bus(u, line, k);
    }
}

static void
show_forest(void)
{
  char line[256];

  grow_tree();
  show_tree_bridge(&host_bridge, line, line);
}

/* Main */

int
main(int argc, char **argv)
{
  int i;

  while ((i = getopt(argc, argv, options)) != -1)
    switch (i)
      {
      case 'n':
	show_numeric_ids = 1;
	break;
      case 'v':
	verbose++;
	break;
      case 'b':
	buscentric_view = 1;
	break;
      case 'B':
	bus_filter = strtol(optarg, NULL, 16);
	break;
      case 'S':
	slot_filter = strtol(optarg, NULL, 16);
	break;
      case 'F':
	func_filter = strtol(optarg, NULL, 16);
	break;
      case 'V':
	vend_filter = strtol(optarg, NULL, 16);
	break;
      case 'D':
	dev_filter = strtol(optarg, NULL, 16);
	break;
      case 'x':
	show_hex++;
	break;
      case 't':
	show_tree++;
	break;
      case 'i':
	pci_ids = optarg;
	break;
      case 'p':
	pci_dir = optarg;
	break;
      default:
      bad:
	fprintf(stderr, help_msg);
	return 1;
      }
  if (optind < argc)
    goto bad;

  scan_proc();
  sort_them();
  if (show_tree)
    show_forest();
  else
    show();

  return 0;
}
