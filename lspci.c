/*
 *	$Id: lspci.c,v 1.19 1999/01/22 21:04:54 mj Exp $
 *
 *	Linux PCI Utilities -- List All PCI Devices
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#include "pciutils.h"

/* Options */

static int verbose;			/* Show detailed information */
static int buscentric_view;		/* Show bus addresses/IRQ's instead of CPU-visible ones */
static int show_hex;			/* Show contents of config space as hexadecimal numbers */
static struct pci_filter filter;	/* Device filter */
static int show_tree;			/* Show bus tree */
static int machine_readable;		/* Generate machine-readable output */

static char options[] = "nvbxs:d:ti:mg" GENERIC_OPTIONS ;

static char help_msg[] = "\
Usage: lspci [<switches>]\n\
\n\
-v\t\tBe verbose\n\
-n\t\tShow numeric ID's\n\
-b\t\tBus-centric view (PCI addresses and IRQ's instead of those seen by the CPU)\n\
-x\t\tShow hex-dump of config space\n\
-s [[<bus>]:][<slot>][.[<func>]]\tShow only devices in selected slots\n\
-d [<vendor>]:[<device>]\tShow only selected devices\n\
-t\t\tShow bus tree\n\
-m\t\tProduce machine-readable output\n\
-i <file>\tUse specified ID database instead of %s\n"
GENERIC_HELP
;

/* Communication with libpci */

static struct pci_access *pacc;

/* Format strings used for IRQ numbers and memory addresses */

#ifdef ARCH_SPARC64
#define IRQ_FORMAT "%08x"
#else
#define IRQ_FORMAT "%d"
#endif

#ifdef HAVE_64BIT_LONG_INT
#define LONG_FORMAT "%016lx"
#else
#define LONG_FORMAT "%08lx"
#endif

/* Our view of the PCI bus */

struct device {
  struct device *next;
  struct pci_dev *dev;
  int config_cnt;
  byte config[256];
};

static struct device *first_dev;

static void
scan_devices(void)
{
  struct device *d;
  int how_much = (show_hex > 2) ? 256 : 64;
  struct pci_dev *p;

  pci_scan_bus(pacc);
  for(p=pacc->devices; p; p=p->next)
    {
      if (!pci_filter_match(&filter, p))
	continue;
      d = xmalloc(sizeof(struct device));
      d->next = first_dev;
      first_dev = d;
      d->dev = p;
      if (!pci_read_block(p, 0, d->config, how_much))
	die("Unable to read %d bytes of configuration space.", how_much);
      if (how_much < 128 && (d->config[PCI_HEADER_TYPE] & 0x7f) == PCI_HEADER_TYPE_CARDBUS)
	{
	  /* For cardbus bridges, we need to fetch 64 bytes more to get the full standard header... */
	  if (!pci_read_block(p, 0, d->config+64, 64))
	    die("Unable to read cardbus bridge extension data.");
	  how_much = 128;
	}
      d->config_cnt = how_much;
      pci_setup_buffer(p, d->config);
      pci_fill_info(p, PCI_FILL_IDENT | PCI_FILL_IRQ | PCI_FILL_BASES | PCI_FILL_ROM_BASE);
    }
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
  const struct pci_dev *a = (*(const struct device **)A)->dev;
  const struct pci_dev *b = (*(const struct device **)B)->dev;

  if (a->bus < b->bus)
    return -1;
  if (a->bus > b->bus)
    return 1;
  if (a->dev < b->dev)
    return -1;
  if (a->dev > b->dev)
    return 1;
  if (a->func < b->func)
    return -1;
  if (a->func > b->func)
    return 1;
  return 0;
}

static void
sort_them(void)
{
  struct device **index, **h, **last_dev;
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
  struct pci_dev *p = d->dev;
  byte classbuf[128], devbuf[128];

  printf("%02x:%02x.%x %s: %s",
	 p->bus,
	 p->dev,
	 p->func,
	 pci_lookup_name(pacc, classbuf, sizeof(classbuf),
			 PCI_LOOKUP_CLASS,
			 get_conf_word(d, PCI_CLASS_DEVICE), 0),
	 pci_lookup_name(pacc, devbuf, sizeof(devbuf),
			 PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			 p->vendor_id, p->device_id));
  if (c = get_conf_byte(d, PCI_REVISION_ID))
    printf(" (rev %02x)", c);
  if (verbose && (c = get_conf_byte(d, PCI_CLASS_PROG)))
    printf(" (prog-if %02x)", c);
  putchar('\n');
}

static void
show_bases(struct device *d, int cnt)
{
  struct pci_dev *p = d->dev;
  word cmd = get_conf_word(d, PCI_COMMAND);
  int i;

  for(i=0; i<cnt; i++)
    {
      unsigned long pos;
      unsigned int flg = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
      pos = p->base_addr[i];
      if (flg == 0xffffffff)
	flg = 0;
      if (!pos && !flg)
	continue;
      if (verbose > 1)
	printf("\tRegion %d: ", i);
      else
	putchar('\t');
      if (pos && !flg)			/* Reported by the OS, but not by the device */
	{
	  printf("[virtual] ");
	  flg = pos;
	}
      if (flg & PCI_BASE_ADDRESS_SPACE_IO)
	{
	  unsigned long a = pos & PCI_BASE_ADDRESS_IO_MASK;
	  printf("I/O ports at ");
	  if (a)
	    printf("%04lx", a);
	  else if (flg & PCI_BASE_ADDRESS_IO_MASK)
	    printf("<ignored>");
	  else
	    printf("<unassigned>");
	  if (!(cmd & PCI_COMMAND_IO))
	    printf(" [disabled]");
	}
      else
	{
	  int t = flg & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
	  unsigned long a = pos & PCI_BASE_ADDRESS_MEM_MASK;
	  int done = 0;
	  u32 z = 0;

	  printf("Memory at ");
	  if (t == PCI_BASE_ADDRESS_MEM_TYPE_64)
	    {
	      if (i >= cnt - 1)
		{
		  printf("<invalid-64bit-slot>\n");
		  done = 1;
		}
	      else
		{
		  i++;
		  z = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
		  if (buscentric_view)
		    {
		      if (a || z)
			printf("%08x%08lx", z, a);
		      else
			printf("<unassigned>");
		      done = 1;
		    }
		}
	    }
	  if (!done)
	    {
	      if (a)
		printf(LONG_FORMAT, a);
	      else
		printf(((flg & PCI_BASE_ADDRESS_MEM_MASK) || z) ? "<ignored>" : "<unassigned>");
	    }
	  printf(" (%s, %sprefetchable)",
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_32) ? "32-bit" :
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_64) ? "64-bit" :
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_1M) ? "low-1M" : "type 3",
		 (flg & PCI_BASE_ADDRESS_MEM_PREFETCH) ? "" : "non-");
	  if (!(cmd & PCI_COMMAND_MEMORY))
	    printf(" [disabled]");
	}
      putchar('\n');
    }
}

static void
show_htype0(struct device *d)
{
  unsigned long rom = d->dev->rom_base_addr;

  show_bases(d, 6);
  if (rom & 1)
    printf("\tExpansion ROM at %08lx%s\n", rom & PCI_ROM_ADDRESS_MASK,
	   (rom & PCI_ROM_ADDRESS_ENABLE) ? "" : " [disabled]");
}

static void
show_htype1(struct device *d)
{
  struct pci_dev *p = d->dev;
  u32 io_base = get_conf_byte(d, PCI_IO_BASE);
  u32 io_limit = get_conf_byte(d, PCI_IO_LIMIT);
  u32 io_type = io_base & PCI_IO_RANGE_TYPE_MASK;
  u32 mem_base = get_conf_word(d, PCI_MEMORY_BASE);
  u32 mem_limit = get_conf_word(d, PCI_MEMORY_LIMIT);
  u32 mem_type = mem_base & PCI_MEMORY_RANGE_TYPE_MASK;
  u32 pref_base = get_conf_word(d, PCI_PREF_MEMORY_BASE);
  u32 pref_limit = get_conf_word(d, PCI_PREF_MEMORY_LIMIT);
  u32 pref_type = pref_base & PCI_PREF_RANGE_TYPE_MASK;
  unsigned long rom = p->rom_base_addr;
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
	printf("\tPrefetchable memory behind bridge: %08x-%08x\n", pref_base, pref_limit + 0xfffff);
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
    printf("\tExpansion ROM at %08lx%s\n", rom & PCI_ROM_ADDRESS_MASK,
	   (rom & PCI_ROM_ADDRESS_ENABLE) ? "" : " [disabled]");

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
  int i;
  word cmd = get_conf_word(d, PCI_COMMAND);
  word brc = get_conf_word(d, PCI_CB_BRIDGE_CONTROL);
  word exca = get_conf_word(d, PCI_CB_LEGACY_MODE_BASE);

  show_bases(d, 1);
  printf("\tBus: primary=%02x, secondary=%02x, subordinate=%02x, sec-latency=%d\n",
	 get_conf_byte(d, PCI_CB_PRIMARY_BUS),
	 get_conf_byte(d, PCI_CB_CARD_BUS),
	 get_conf_byte(d, PCI_CB_SUBORDINATE_BUS),
	 get_conf_byte(d, PCI_CB_LATENCY_TIMER));
  for(i=0; i<2; i++)
    {
      int p = 8*i;
      u32 base = get_conf_long(d, PCI_CB_MEMORY_BASE_0 + p);
      u32 limit = get_conf_long(d, PCI_CB_MEMORY_LIMIT_0 + p);
      if (limit > base)
	printf("Memory window %d: %08x-%08x%s%s\n", i, base, limit,
	       (cmd & PCI_COMMAND_MEMORY) ? "" : " [disabled]",
	       (brc & (PCI_CB_BRIDGE_CTL_PREFETCH_MEM0 << i)) ? " (prefetchable)" : "");
    }
  for(i=0; i<2; i++)
    {
      int p = 8*i;
      u32 base = get_conf_long(d, PCI_CB_IO_BASE_0 + p);
      u32 limit = get_conf_long(d, PCI_CB_IO_LIMIT_0 + p);
      if (!(base & PCI_IO_RANGE_TYPE_32))
	{
	  base &= 0xffff;
	  limit &= 0xffff;
	}
      base &= PCI_CB_IO_RANGE_MASK;
      if (!base)
	continue;
      limit = (limit & PCI_CB_IO_RANGE_MASK) + 3;
      printf("I/O window %d: %08x-%08x%s\n", i, base, limit,
	     (cmd & PCI_COMMAND_IO) ? "" : " [disabled]");
    }

  if (get_conf_word(d, PCI_CB_SEC_STATUS) & PCI_STATUS_SIG_SYSTEM_ERROR)
    printf("\tSecondary status: SERR\n");
  if (verbose > 1)
    printf("\tBridgeCtl: Parity%c SERR%c ISA%c VGA%c MAbort%c >Reset%c 16bInt%c PostWrite%c\n",
	   (brc & PCI_CB_BRIDGE_CTL_PARITY) ? '+' : '-',
	   (brc & PCI_CB_BRIDGE_CTL_SERR) ? '+' : '-',
	   (brc & PCI_CB_BRIDGE_CTL_ISA) ? '+' : '-',
	   (brc & PCI_CB_BRIDGE_CTL_VGA) ? '+' : '-',
	   (brc & PCI_CB_BRIDGE_CTL_MASTER_ABORT) ? '+' : '-',
	   (brc & PCI_CB_BRIDGE_CTL_CB_RESET) ? '+' : '-',
	   (brc & PCI_CB_BRIDGE_CTL_16BIT_INT) ? '+' : '-',
	   (brc & PCI_CB_BRIDGE_CTL_POST_WRITES) ? '+' : '-');
  if (exca)
    printf("\t16-bit legacy interface ports at %04x\n", exca);
}

static void
show_verbose(struct device *d)
{
  struct pci_dev *p = d->dev;
  word status = get_conf_word(d, PCI_STATUS);
  word cmd = get_conf_word(d, PCI_COMMAND);
  word class = get_conf_word(d, PCI_CLASS_DEVICE);
  byte bist = get_conf_byte(d, PCI_BIST);
  byte htype = get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f;
  byte latency = get_conf_byte(d, PCI_LATENCY_TIMER);
  byte cache_line = get_conf_byte(d, PCI_CACHE_LINE_SIZE);
  byte max_lat, min_gnt;
  byte int_pin = get_conf_byte(d, PCI_INTERRUPT_PIN);
  unsigned int irq = p->irq;
  word subsys_v, subsys_d;
  char ssnamebuf[256];

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
      irq = int_pin = min_gnt = max_lat = 0;
      subsys_v = subsys_d = 0;
      break;
    case PCI_HEADER_TYPE_CARDBUS:
      if ((class >> 8) != PCI_BASE_CLASS_BRIDGE)
	goto badhdr;
      min_gnt = max_lat = 0;
      subsys_v = get_conf_word(d, PCI_CB_SUBSYSTEM_VENDOR_ID);
      subsys_d = get_conf_word(d, PCI_CB_SUBSYSTEM_ID);
      break;
    default:
      printf("\t!!! Unknown header type %02x\n", htype);
      return;
    }

  if (verbose && subsys_v && subsys_v != 0xffff)
    printf("\tSubsystem: %s\n",
	   pci_lookup_name(pacc, ssnamebuf, sizeof(ssnamebuf),
			   PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			   subsys_v, subsys_d));

  if (verbose > 1)
    {
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
      if (int_pin || irq)
	printf("\tInterrupt: pin %c routed to IRQ " IRQ_FORMAT "\n",
	       (int_pin ? 'A' + int_pin - 1 : '?'), irq);
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
      if (irq)
	printf(", IRQ " IRQ_FORMAT, irq);
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

  for(i=0; i<d->config_cnt; i++)
    {
      if (! (i & 15))
	printf("%02x:", i);
      printf(" %02x", get_conf_byte(d, i));
      if ((i & 15) == 15)
	putchar('\n');
    }
}

static void
show_machine(struct device *d)
{
  struct pci_dev *p = d->dev;
  int c;
  word sv_id=0, sd_id=0;
  char classbuf[128], vendbuf[128], devbuf[128], svbuf[128], sdbuf[128];

  switch (get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f)
    {
    case PCI_HEADER_TYPE_NORMAL:
      sv_id = get_conf_word(d, PCI_SUBSYSTEM_VENDOR_ID);
      sd_id = get_conf_word(d, PCI_SUBSYSTEM_ID);
      break;
    case PCI_HEADER_TYPE_CARDBUS:
      sv_id = get_conf_word(d, PCI_CB_SUBSYSTEM_VENDOR_ID);
      sd_id = get_conf_word(d, PCI_CB_SUBSYSTEM_ID);
      break;
    }

  if (verbose)
    {
      printf("Device:\t%02x:%02x.%x\n", p->bus, p->dev, p->func);
      printf("Class:\t%s\n",
	     pci_lookup_name(pacc, classbuf, sizeof(classbuf), PCI_LOOKUP_CLASS, get_conf_word(d, PCI_CLASS_DEVICE), 0));
      printf("Vendor:\t%s\n",
	     pci_lookup_name(pacc, vendbuf, sizeof(vendbuf), PCI_LOOKUP_VENDOR, p->vendor_id, p->device_id));
      printf("Device:\t%s\n",
	     pci_lookup_name(pacc, devbuf, sizeof(devbuf), PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id));
      if (sv_id && sv_id != 0xffff)
	{
	  printf("SVendor:\t%s\n",
		 pci_lookup_name(pacc, svbuf, sizeof(svbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR, sv_id, sd_id));
	  printf("SDevice:\t%s\n",
		 pci_lookup_name(pacc, sdbuf, sizeof(sdbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE, sv_id, sd_id));
	}
      if (c = get_conf_byte(d, PCI_REVISION_ID))
	printf("Rev:\t%02x\n", c);
      if (c = get_conf_byte(d, PCI_CLASS_PROG))
	printf("ProgIf:\t%02x\n", c);
    }
  else
    {
      printf("%02x:%02x.%x ", p->bus, p->dev, p->func);
      printf("\"%s\" \"%s\" \"%s\"",
	     pci_lookup_name(pacc, classbuf, sizeof(classbuf), PCI_LOOKUP_CLASS,
			     get_conf_word(d, PCI_CLASS_DEVICE), 0),
	     pci_lookup_name(pacc, vendbuf, sizeof(vendbuf), PCI_LOOKUP_VENDOR,
			     p->vendor_id, p->device_id),
	     pci_lookup_name(pacc, devbuf, sizeof(devbuf), PCI_LOOKUP_DEVICE,
			     p->vendor_id, p->device_id));
      if (c = get_conf_byte(d, PCI_REVISION_ID))
	printf(" -r%02x", c);
      if (c = get_conf_byte(d, PCI_CLASS_PROG))
	printf(" -p%02x", c);
      if (sv_id && sv_id != 0xffff)
	printf(" \"%s\" \"%s\"",
	       pci_lookup_name(pacc, svbuf, sizeof(svbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR, sv_id, sd_id),
	       pci_lookup_name(pacc, sdbuf, sizeof(sdbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE, sv_id, sd_id));
      else
	printf(" \"\" \"\"");
      putchar('\n');
    }
}

static void
show(void)
{
  struct device *d;

  for(d=first_dev; d; d=d->next)
    {
      if (machine_readable)
	show_machine(d);
      else if (verbose)
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
  struct pci_dev *p = d->dev;
  struct bus *bus;

  if (! (bus = find_bus(b, p->bus)))
    {
      struct bridge *c;
      for(c=b->child; c; c=c->next)
	if (c->secondary <= p->bus && p->bus <= c->subordinate)
	  return insert_dev(d, c);
      bus = new_bus(b, p->bus);
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
  struct bridge **last_br, *b;

  /* Build list of bridges */

  last_br = &host_bridge.chain;
  for(d=first_dev; d; d=d->next)
    {
      word class = get_conf_word(d, PCI_CLASS_DEVICE);
      byte ht = get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f;
      if (class == PCI_CLASS_BRIDGE_PCI &&
	  (ht == PCI_HEADER_TYPE_BRIDGE || ht == PCI_HEADER_TYPE_CARDBUS))
	{
	  b = xmalloc(sizeof(struct bridge));
	  if (ht == PCI_HEADER_TYPE_BRIDGE)
	    {
	      b->primary = get_conf_byte(d, PCI_CB_PRIMARY_BUS);
	      b->secondary = get_conf_byte(d, PCI_CB_CARD_BUS);
	      b->subordinate = get_conf_byte(d, PCI_CB_SUBORDINATE_BUS);
	    }
	  else
	    {
	      b->primary = get_conf_byte(d, PCI_PRIMARY_BUS);
	      b->secondary = get_conf_byte(d, PCI_SECONDARY_BUS);
	      b->subordinate = get_conf_byte(d, PCI_SUBORDINATE_BUS);
	    }
	  *last_br = b;
	  last_br = &b->chain;
	  b->next = b->child = NULL;
	  b->first_bus = NULL;
	  b->br_dev = d;
	}
    }
  *last_br = NULL;

  /* Create a bridge tree */

  for(b=&host_bridge; b; b=b->chain)
    {
      struct bridge *c, *best;
      best = NULL;
      for(c=&host_bridge; c; c=c->chain)
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

  for(b=&host_bridge; b; b=b->chain)
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
    if (*p == '+' || *p == '|')
      *p = '|';
    else
      *p = ' ';
}

static void show_tree_bridge(struct bridge *, byte *, byte *);

static void
show_tree_dev(struct device *d, byte *line, byte *p)
{
  struct pci_dev *q = d->dev;
  struct bridge *b;
  char namebuf[256];

  p += sprintf(p, "%02x.%x", q->dev, q->func);
  for(b=&host_bridge; b; b=b->chain)
    if (b->br_dev == d)
      {
	if (b->secondary == b->subordinate)
	  p += sprintf(p, "-[%02x]-", b->secondary);
	else
	  p += sprintf(p, "-[%02x-%02x]-", b->secondary, b->subordinate);
        show_tree_bridge(b, line, p);
        return;
      }
  if (verbose)
    p += sprintf(p, "  %s",
		 pci_lookup_name(pacc, namebuf, sizeof(namebuf),
				 PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
				 q->vendor_id, q->device_id));
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
  char *msg;

  if (argc == 2 && !strcmp(argv[1], "--version"))
    {
      puts("lspci version " PCIUTILS_VERSION);
      return 0;
    }

  pacc = pci_alloc();
  pacc->error = die;
  pci_filter_init(pacc, &filter);

  while ((i = getopt(argc, argv, options)) != -1)
    switch (i)
      {
      case 'n':
	pacc->numeric_ids = 1;
	break;
      case 'v':
	verbose++;
	break;
      case 'b':
	pacc->buscentric = 1;
	buscentric_view = 1;
	break;
      case 's':
	if (msg = pci_filter_parse_slot(&filter, optarg))
	  die("-f: %s", msg);
	break;
      case 'd':
	if (msg = pci_filter_parse_id(&filter, optarg))
	  die("-d: %s", msg);
	break;
      case 'x':
	show_hex++;
	break;
      case 't':
	show_tree++;
	break;
      case 'i':
	pacc->id_file_name = optarg;
	break;
      case 'm':
	machine_readable++;
	break;
      default:
	if (parse_generic_option(i, pacc, optarg))
	  break;
      bad:
	fprintf(stderr, help_msg, pacc->id_file_name);
	return 1;
      }
  if (optind < argc)
    goto bad;

  pci_init(pacc);
  scan_devices();
  sort_them();
  if (show_tree)
    show_forest();
  else
    show();
  pci_cleanup(pacc);

  return 0;
}
