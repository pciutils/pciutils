/*
 *	The PCI Utilities -- List All PCI Devices
 *
 *	Copyright (c) 1997--2020 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "lspci.h"

/* Options */

int verbose;				/* Show detailed information */
static int opt_hex;			/* Show contents of config space as hexadecimal numbers */
struct pci_filter filter;		/* Device filter */
static int opt_filter;			/* Any filter was given */
static int opt_tree;			/* Show bus tree */
static int opt_path;			/* Show bridge path */
static int opt_machine;			/* Generate machine-readable output */
static int opt_map_mode;		/* Bus mapping mode enabled */
static int opt_domains;			/* Show domain numbers (0=disabled, 1=auto-detected, 2=requested) */
static int opt_kernel;			/* Show kernel drivers */
static int opt_query_dns;		/* Query the DNS (0=disabled, 1=enabled, 2=refresh cache) */
static int opt_query_all;		/* Query the DNS for all entries */
char *opt_pcimap;			/* Override path to Linux modules.pcimap */

const char program_name[] = "lspci";

static char options[] = "nvbxs:d:tPi:mgp:qkMDQ" GENERIC_OPTIONS ;

static char help_msg[] =
"Usage: lspci [<switches>]\n"
"\n"
"Basic display modes:\n"
"-mm\t\tProduce machine-readable output (single -m for an obsolete format)\n"
"-t\t\tShow bus tree\n"
"\n"
"Display options:\n"
"-v\t\tBe verbose (-vv or -vvv for higher verbosity)\n"
#ifdef PCI_OS_LINUX
"-k\t\tShow kernel drivers handling each device\n"
#endif
"-x\t\tShow hex-dump of the standard part of the config space\n"
"-xxx\t\tShow hex-dump of the whole config space (dangerous; root only)\n"
"-xxxx\t\tShow hex-dump of the 4096-byte extended config space (root only)\n"
"-b\t\tBus-centric view (addresses and IRQ's as seen by the bus)\n"
"-D\t\tAlways show domain numbers\n"
"-P\t\tDisplay bridge path in addition to bus and device number\n"
"-PP\t\tDisplay bus path in addition to bus and device number\n"
"\n"
"Resolving of device ID's to names:\n"
"-n\t\tShow numeric ID's\n"
"-nn\t\tShow both textual and numeric ID's (names & numbers)\n"
#ifdef PCI_USE_DNS
"-q\t\tQuery the PCI ID database for unknown ID's via DNS\n"
"-qq\t\tAs above, but re-query locally cached entries\n"
"-Q\t\tQuery the PCI ID database for all ID's via DNS\n"
#endif
"\n"
"Selection of devices:\n"
"-s [[[[<domain>]:]<bus>]:][<slot>][.[<func>]]\tShow only devices in selected slots\n"
"-d [<vendor>]:[<device>][:<class>]\t\tShow only devices with specified ID's\n"
"\n"
"Other options:\n"
"-i <file>\tUse specified ID database instead of %s\n"
#ifdef PCI_OS_LINUX
"-p <file>\tLook up kernel modules in a given file instead of default modules.pcimap\n"
#endif
"-M\t\tEnable `bus mapping' mode (dangerous; root only)\n"
"\n"
"PCI access options:\n"
GENERIC_HELP
;

/*** Our view of the PCI bus ***/

struct pci_access *pacc;
struct device *first_dev;
static int seen_errors;
static int need_topology;

int
config_fetch(struct device *d, unsigned int pos, unsigned int len)
{
  unsigned int end = pos+len;
  int result;

  while (pos < d->config_bufsize && len && d->present[pos])
    pos++, len--;
  while (pos+len <= d->config_bufsize && len && d->present[pos+len-1])
    len--;
  if (!len)
    return 1;

  if (end > d->config_bufsize)
    {
      int orig_size = d->config_bufsize;
      while (end > d->config_bufsize)
	d->config_bufsize *= 2;
      d->config = xrealloc(d->config, d->config_bufsize);
      d->present = xrealloc(d->present, d->config_bufsize);
      memset(d->present + orig_size, 0, d->config_bufsize - orig_size);
    }
  result = pci_read_block(d->dev, pos, d->config + pos, len);
  if (result)
    memset(d->present + pos, 1, len);
  return result;
}

struct device *
scan_device(struct pci_dev *p)
{
  struct device *d;

  if (p->domain && !opt_domains)
    opt_domains = 1;
  if (!pci_filter_match(&filter, p) && !need_topology)
    return NULL;
  d = xmalloc(sizeof(struct device));
  memset(d, 0, sizeof(*d));
  d->dev = p;
  d->no_config_access = p->no_config_access;
  d->config_cached = d->config_bufsize = 64;
  d->config = xmalloc(64);
  d->present = xmalloc(64);
  memset(d->present, 1, 64);
  if (!d->no_config_access && !pci_read_block(p, 0, d->config, 64))
    {
      d->no_config_access = 1;
      d->config_cached = d->config_bufsize = 0;
      memset(d->present, 0, 64);
    }
  if (!d->no_config_access && (d->config[PCI_HEADER_TYPE] & 0x7f) == PCI_HEADER_TYPE_CARDBUS)
    {
      /* For cardbus bridges, we need to fetch 64 bytes more to get the
       * full standard header... */
      if (config_fetch(d, 64, 64))
	d->config_cached += 64;
    }
  pci_setup_cache(p, d->config, d->config_cached);
  pci_fill_info(p, PCI_FILL_IDENT | PCI_FILL_CLASS | PCI_FILL_CLASS_EXT | PCI_FILL_SUBSYS | (need_topology ? PCI_FILL_PARENT : 0));
  return d;
}

static void
scan_devices(void)
{
  struct device *d;
  struct pci_dev *p;

  pci_scan_bus(pacc);
  for (p=pacc->devices; p; p=p->next)
    if (d = scan_device(p))
      {
	d->next = first_dev;
	first_dev = d;
      }
}

/*** Config space accesses ***/

static void
check_conf_range(struct device *d, unsigned int pos, unsigned int len)
{
  while (len)
    if (!d->present[pos])
      die("Internal bug: Accessing non-read configuration byte at position %x", pos);
    else
      pos++, len--;
}

byte
get_conf_byte(struct device *d, unsigned int pos)
{
  check_conf_range(d, pos, 1);
  return d->config[pos];
}

word
get_conf_word(struct device *d, unsigned int pos)
{
  check_conf_range(d, pos, 2);
  return d->config[pos] | (d->config[pos+1] << 8);
}

u32
get_conf_long(struct device *d, unsigned int pos)
{
  check_conf_range(d, pos, 4);
  return d->config[pos] |
    (d->config[pos+1] << 8) |
    (d->config[pos+2] << 16) |
    (d->config[pos+3] << 24);
}

/*** Sorting ***/

static int
compare_them(const void *A, const void *B)
{
  const struct pci_dev *a = (*(const struct device **)A)->dev;
  const struct pci_dev *b = (*(const struct device **)B)->dev;

  if (a->domain < b->domain)
    return -1;
  if (a->domain > b->domain)
    return 1;
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
  for (d=first_dev; d; d=d->next)
    cnt++;
  h = index = alloca(sizeof(struct device *) * cnt);
  for (d=first_dev; d; d=d->next)
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

/*** Normal output ***/

static void
show_slot_path(struct device *d)
{
  struct pci_dev *p = d->dev;

  if (opt_path)
    {
      struct bus *bus = d->parent_bus;
      struct bridge *br = bus->parent_bridge;

      if (br && br->br_dev)
	{
	  show_slot_path(br->br_dev);
	  if (opt_path > 1)
	    printf("/%02x:%02x.%d", p->bus, p->dev, p->func);
	  else
	    printf("/%02x.%d", p->dev, p->func);
	  return;
	}
    }
  printf("%02x:%02x.%d", p->bus, p->dev, p->func);
}

static void
show_slot_name(struct device *d)
{
  struct pci_dev *p = d->dev;

  if (!opt_machine ? opt_domains : (p->domain || opt_domains >= 2))
    printf("%04x:", p->domain);
  show_slot_path(d);
}

static void
show_terse(struct device *d)
{
  int c;
  struct pci_dev *p = d->dev;
  char classbuf[128], devbuf[128];

  show_slot_name(d);
  printf(" %s: %s",
	 pci_lookup_name(pacc, classbuf, sizeof(classbuf),
			 PCI_LOOKUP_CLASS,
			 p->device_class),
	 pci_lookup_name(pacc, devbuf, sizeof(devbuf),
			 PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			 p->vendor_id, p->device_id));
  if ((p->known_fields & PCI_FILL_CLASS_EXT) && p->rev_id)
    printf(" (rev %02x)", p->rev_id);
  if (verbose)
    {
      char *x;
      c = (p->known_fields & PCI_FILL_CLASS_EXT) ? p->prog_if : 0;
      x = pci_lookup_name(pacc, devbuf, sizeof(devbuf),
			  PCI_LOOKUP_PROGIF | PCI_LOOKUP_NO_NUMBERS,
			  p->device_class, c);
      if (c || x)
	{
	  printf(" (prog-if %02x", c);
	  if (x)
	    printf(" [%s]", x);
	  putchar(')');
	}
    }
  putchar('\n');

  if (verbose || opt_kernel)
    {
      char ssnamebuf[256];

      pci_fill_info(p, PCI_FILL_LABEL);

      if (p->label)
        printf("\tDeviceName: %s", p->label);
      if ((p->known_fields & PCI_FILL_SUBSYS) &&
	  p->subsys_vendor_id && p->subsys_vendor_id != 0xffff)
	printf("\tSubsystem: %s\n",
		pci_lookup_name(pacc, ssnamebuf, sizeof(ssnamebuf),
			PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			p->vendor_id, p->device_id, p->subsys_vendor_id, p->subsys_id));
    }
}

/*** Verbose output ***/

static void
show_size(u64 x)
{
  static const char suffix[][2] = { "", "K", "M", "G", "T" };
  unsigned i;
  if (!x)
    return;
  for (i = 0; i < (sizeof(suffix) / sizeof(*suffix) - 1); i++) {
    if (x % 1024)
      break;
    x /= 1024;
  }
  printf(" [size=%u%s]", (unsigned)x, suffix[i]);
}

static void
show_range(const char *prefix, u64 base, u64 limit, int bits, int disabled)
{
  printf("%s:", prefix);
  if (base <= limit || verbose > 2)
    printf(" %0*" PCI_U64_FMT_X "-%0*" PCI_U64_FMT_X, (bits+3)/4, base, (bits+3)/4, limit);
  if (!disabled && base <= limit)
    show_size(limit - base + 1);
  else
    printf(" [disabled]");
  if (bits)
    printf(" [%d-bit]", bits);
  putchar('\n');
}

static u32
ioflg_to_pciflg(pciaddr_t ioflg)
{
  u32 flg;

  if (ioflg & PCI_IORESOURCE_IO)
    flg = PCI_BASE_ADDRESS_SPACE_IO;
  else if (!(ioflg & PCI_IORESOURCE_MEM))
    flg = 0;
  else
    {
      flg = PCI_BASE_ADDRESS_SPACE_MEMORY;
      if (ioflg & PCI_IORESOURCE_MEM_64)
        flg |= PCI_BASE_ADDRESS_MEM_TYPE_64;
      else
        flg |= PCI_BASE_ADDRESS_MEM_TYPE_32;
      if (ioflg & PCI_IORESOURCE_PREFETCH)
        flg |= PCI_BASE_ADDRESS_MEM_PREFETCH;
    }

  return flg;
}

static void
show_bases(struct device *d, int cnt, int without_config_data)
{
  struct pci_dev *p = d->dev;
  word cmd = without_config_data ? (PCI_COMMAND_IO | PCI_COMMAND_MEMORY) : get_conf_word(d, PCI_COMMAND);
  int i;

  for (i=0; i<cnt; i++)
    {
      pciaddr_t pos = p->base_addr[i];
      pciaddr_t len = (p->known_fields & PCI_FILL_SIZES) ? p->size[i] : 0;
      pciaddr_t ioflg = (p->known_fields & PCI_FILL_IO_FLAGS) ? p->flags[i] : 0;
      u32 flg = (p->known_fields & PCI_FILL_IO_FLAGS) ? ioflg_to_pciflg(ioflg) : without_config_data ? 0 : get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
      u32 hw_lower = 0;
      u32 hw_upper = 0;
      int broken = 0;
      int virtual = 0;

      if (flg == 0xffffffff)
	flg = 0;
      if (!pos && !flg && !len)
	continue;

      if (verbose > 1)
	printf("\tRegion %d: ", i);
      else
	putchar('\t');

      /* Detect virtual regions, which are reported by the OS, but unassigned in the device */
      if ((p->known_fields & PCI_FILL_IO_FLAGS) && !without_config_data)
	{
	  /* Read address as seen by the hardware */
	  hw_lower = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
	  if ((hw_lower & PCI_BASE_ADDRESS_SPACE) == (ioflg_to_pciflg(ioflg) & PCI_BASE_ADDRESS_SPACE))
	    {
	      if ((ioflg & PCI_IORESOURCE_TYPE_BITS) == PCI_IORESOURCE_MEM &&
		  (hw_lower & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64)
	        {
		  if (i >= cnt - 1)
		    broken = 1;
		  else
		    hw_upper = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i + 1);
		}
	      if (pos && !hw_lower && !hw_upper && !(ioflg & PCI_IORESOURCE_PCI_EA_BEI))
		virtual = 1;
	    }
	}

      /* Print base address */
      if (flg & PCI_BASE_ADDRESS_SPACE_IO)
	{
	  pciaddr_t a = pos & PCI_BASE_ADDRESS_IO_MASK;
	  printf("I/O ports at ");
	  if (a || (cmd & PCI_COMMAND_IO))
	    printf(PCIADDR_PORT_FMT, a);
	  else if (hw_lower)
	    printf("<ignored>");
	  else
	    printf("<unassigned>");
	  if (virtual)
	    printf(" [virtual]");
	  else if (!(cmd & PCI_COMMAND_IO))
	    printf(" [disabled]");
	}
      else
	{
	  int t = flg & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
	  pciaddr_t a = pos & PCI_ADDR_MEM_MASK;

	  printf("Memory at ");
	  if (broken)
	    printf("<broken-64-bit-slot>");
	  else if (a)
	    printf(PCIADDR_T_FMT, a);
	  else if (hw_lower || hw_upper)
	    printf("<ignored>");
	  else
	    printf("<unassigned>");
	  printf(" (%s, %sprefetchable)",
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_32) ? "32-bit" :
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_64) ? "64-bit" :
		 (t == PCI_BASE_ADDRESS_MEM_TYPE_1M) ? "low-1M" : "type 3",
		 (flg & PCI_BASE_ADDRESS_MEM_PREFETCH) ? "" : "non-");
	  if (virtual)
	    printf(" [virtual]");
	  else if (!(cmd & PCI_COMMAND_MEMORY))
	    printf(" [disabled]");
	}

      if (ioflg & PCI_IORESOURCE_PCI_EA_BEI)
	printf(" [enhanced]");

      show_size(len);
      putchar('\n');
    }
}

static void
show_rom(struct device *d, int reg)
{
  struct pci_dev *p = d->dev;
  pciaddr_t rom = p->rom_base_addr;
  pciaddr_t len = (p->known_fields & PCI_FILL_SIZES) ? p->rom_size : 0;
  pciaddr_t ioflg = (p->known_fields & PCI_FILL_IO_FLAGS) ? p->rom_flags : 0;
  u32 flg = reg >= 0 ? get_conf_long(d, reg) : ioflg_to_pciflg(ioflg);
  word cmd = reg >= 0 ? get_conf_word(d, PCI_COMMAND) : PCI_COMMAND_MEMORY;
  int virtual = 0;

  if (!rom && !flg && !len)
    return;

  if (reg >= 0 && (rom & PCI_ROM_ADDRESS_MASK) && !(flg & PCI_ROM_ADDRESS_MASK) && !(ioflg & PCI_IORESOURCE_PCI_EA_BEI))
    {
      flg = rom;
      virtual = 1;
    }

  printf("\tExpansion ROM at ");
  if (rom & PCI_ROM_ADDRESS_MASK)
    printf(PCIADDR_T_FMT, rom & PCI_ROM_ADDRESS_MASK);
  else if (flg & PCI_ROM_ADDRESS_MASK)
    printf("<ignored>");
  else
    printf("<unassigned>");

  if (virtual)
    printf(" [virtual]");

  if (!(flg & PCI_ROM_ADDRESS_ENABLE))
    printf(" [disabled]");
  else if (!virtual && !(cmd & PCI_COMMAND_MEMORY))
    printf(" [disabled by cmd]");

  if (ioflg & PCI_IORESOURCE_PCI_EA_BEI)
      printf(" [enhanced]");

  show_size(len);
  putchar('\n');
}

static void
show_htype0(struct device *d)
{
  show_bases(d, 6, 0);
  show_rom(d, PCI_ROM_ADDRESS);
  show_caps(d, PCI_CAPABILITY_LIST);
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
  word sec_stat = get_conf_word(d, PCI_SEC_STATUS);
  word brc = get_conf_word(d, PCI_BRIDGE_CONTROL);
  int io_disabled = (p->known_fields & PCI_FILL_BRIDGE_BASES) && !p->bridge_size[0];
  int mem_disabled = (p->known_fields & PCI_FILL_BRIDGE_BASES) && !p->bridge_size[1];
  int pref_disabled = (p->known_fields & PCI_FILL_BRIDGE_BASES) && !p->bridge_size[2];
  int io_bits, pref_bits;

  show_bases(d, 2, 0);
  printf("\tBus: primary=%02x, secondary=%02x, subordinate=%02x, sec-latency=%d\n",
	 get_conf_byte(d, PCI_PRIMARY_BUS),
	 get_conf_byte(d, PCI_SECONDARY_BUS),
	 get_conf_byte(d, PCI_SUBORDINATE_BUS),
	 get_conf_byte(d, PCI_SEC_LATENCY_TIMER));

  if ((p->known_fields & PCI_FILL_BRIDGE_BASES) && !io_disabled)
    {
      io_base = p->bridge_base_addr[0] & PCI_IO_RANGE_MASK;
      io_limit = io_base + p->bridge_size[0] - 1;
      io_type = p->bridge_base_addr[0] & PCI_IO_RANGE_TYPE_MASK;
      io_bits = (io_type == PCI_IO_RANGE_TYPE_32) ? 32 : 16;
      show_range("\tI/O behind bridge", io_base, io_limit, io_bits, io_disabled);
    }
  else if (io_type != (io_limit & PCI_IO_RANGE_TYPE_MASK) ||
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
      /* I/O is unsupported if both base and limit are zeros and resource is disabled */
      if (!(io_base == 0x0 && io_limit == 0x0 && io_disabled))
        {
          io_limit += 0xfff;
          io_bits = (io_type == PCI_IO_RANGE_TYPE_32) ? 32 : 16;
          show_range("\tI/O behind bridge", io_base, io_limit, io_bits, io_disabled);
        }
    }

  if ((p->known_fields & PCI_FILL_BRIDGE_BASES) && !mem_disabled)
    {
      mem_base = p->bridge_base_addr[1] & PCI_MEMORY_RANGE_MASK;
      mem_limit = mem_base + p->bridge_size[1] - 1;
      show_range("\tMemory behind bridge", mem_base, mem_limit, 32, mem_disabled);
    }
  else if (mem_type != (mem_limit & PCI_MEMORY_RANGE_TYPE_MASK) ||
      mem_type)
    printf("\t!!! Unknown memory range types %x/%x\n", mem_base, mem_limit);
  else
    {
      mem_base = (mem_base & PCI_MEMORY_RANGE_MASK) << 16;
      mem_limit = (mem_limit & PCI_MEMORY_RANGE_MASK) << 16;
      show_range("\tMemory behind bridge", mem_base, mem_limit + 0xfffff, 32, mem_disabled);
    }

  if ((p->known_fields & PCI_FILL_BRIDGE_BASES) && !pref_disabled)
    {
      u64 pref_base_64 = p->bridge_base_addr[2] & PCI_MEMORY_RANGE_MASK;
      u64 pref_limit_64 = pref_base_64 + p->bridge_size[2] - 1;
      pref_type = p->bridge_base_addr[2] & PCI_MEMORY_RANGE_TYPE_MASK;
      pref_bits = (pref_type == PCI_PREF_RANGE_TYPE_64) ? 64 : 32;
      show_range("\tPrefetchable memory behind bridge", pref_base_64, pref_limit_64, pref_bits, pref_disabled);
    }
  else if (pref_type != (pref_limit & PCI_PREF_RANGE_TYPE_MASK) ||
      (pref_type != PCI_PREF_RANGE_TYPE_32 && pref_type != PCI_PREF_RANGE_TYPE_64))
    printf("\t!!! Unknown prefetchable memory range types %x/%x\n", pref_base, pref_limit);
  else
    {
      u64 pref_base_64 = (pref_base & PCI_PREF_RANGE_MASK) << 16;
      u64 pref_limit_64 = (pref_limit & PCI_PREF_RANGE_MASK) << 16;
      if (pref_type == PCI_PREF_RANGE_TYPE_64)
	{
	  pref_base_64 |= (u64) get_conf_long(d, PCI_PREF_BASE_UPPER32) << 32;
	  pref_limit_64 |= (u64) get_conf_long(d, PCI_PREF_LIMIT_UPPER32) << 32;
	}
      /* Prefetchable memory is unsupported if both base and limit are zeros and resource is disabled */
      if (!(pref_base_64 == 0x0 && pref_limit_64 == 0x0 && pref_disabled))
        {
          pref_limit_64 += 0xfffff;
          pref_bits = (pref_type == PCI_PREF_RANGE_TYPE_64) ? 64 : 32;
          show_range("\tPrefetchable memory behind bridge", pref_base_64, pref_limit_64, pref_bits, pref_disabled);
        }
    }

  if (verbose > 1)
    printf("\tSecondary status: 66MHz%c FastB2B%c ParErr%c DEVSEL=%s >TAbort%c <TAbort%c <MAbort%c <SERR%c <PERR%c\n",
	     FLAG(sec_stat, PCI_STATUS_66MHZ),
	     FLAG(sec_stat, PCI_STATUS_FAST_BACK),
	     FLAG(sec_stat, PCI_STATUS_PARITY),
	     ((sec_stat & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_SLOW) ? "slow" :
	     ((sec_stat & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_MEDIUM) ? "medium" :
	     ((sec_stat & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_FAST) ? "fast" : "??",
	     FLAG(sec_stat, PCI_STATUS_SIG_TARGET_ABORT),
	     FLAG(sec_stat, PCI_STATUS_REC_TARGET_ABORT),
	     FLAG(sec_stat, PCI_STATUS_REC_MASTER_ABORT),
	     FLAG(sec_stat, PCI_STATUS_SIG_SYSTEM_ERROR),
	     FLAG(sec_stat, PCI_STATUS_DETECTED_PARITY));

  show_rom(d, PCI_ROM_ADDRESS1);

  if (verbose > 1)
    {
      printf("\tBridgeCtl: Parity%c SERR%c NoISA%c VGA%c VGA16%c MAbort%c >Reset%c FastB2B%c\n",
	FLAG(brc, PCI_BRIDGE_CTL_PARITY),
	FLAG(brc, PCI_BRIDGE_CTL_SERR),
	FLAG(brc, PCI_BRIDGE_CTL_NO_ISA),
	FLAG(brc, PCI_BRIDGE_CTL_VGA),
	FLAG(brc, PCI_BRIDGE_CTL_VGA_16BIT),
	FLAG(brc, PCI_BRIDGE_CTL_MASTER_ABORT),
	FLAG(brc, PCI_BRIDGE_CTL_BUS_RESET),
	FLAG(brc, PCI_BRIDGE_CTL_FAST_BACK));
      printf("\t\tPriDiscTmr%c SecDiscTmr%c DiscTmrStat%c DiscTmrSERREn%c\n",
	FLAG(brc, PCI_BRIDGE_CTL_PRI_DISCARD_TIMER),
	FLAG(brc, PCI_BRIDGE_CTL_SEC_DISCARD_TIMER),
	FLAG(brc, PCI_BRIDGE_CTL_DISCARD_TIMER_STATUS),
	FLAG(brc, PCI_BRIDGE_CTL_DISCARD_TIMER_SERR_EN));
    }

  show_caps(d, PCI_CAPABILITY_LIST);
}

static void
show_htype2(struct device *d)
{
  int i;
  word cmd = get_conf_word(d, PCI_COMMAND);
  word brc = get_conf_word(d, PCI_CB_BRIDGE_CONTROL);
  word exca;
  int verb = verbose > 2;

  show_bases(d, 1, 0);
  printf("\tBus: primary=%02x, secondary=%02x, subordinate=%02x, sec-latency=%d\n",
	 get_conf_byte(d, PCI_CB_PRIMARY_BUS),
	 get_conf_byte(d, PCI_CB_CARD_BUS),
	 get_conf_byte(d, PCI_CB_SUBORDINATE_BUS),
	 get_conf_byte(d, PCI_CB_LATENCY_TIMER));
  for (i=0; i<2; i++)
    {
      int p = 8*i;
      u32 base = get_conf_long(d, PCI_CB_MEMORY_BASE_0 + p);
      u32 limit = get_conf_long(d, PCI_CB_MEMORY_LIMIT_0 + p);
      limit = limit + 0xfff;
      if (base <= limit || verb)
	printf("\tMemory window %d: %08x-%08x%s%s\n", i, base, limit,
	       (cmd & PCI_COMMAND_MEMORY) ? "" : " [disabled]",
	       (brc & (PCI_CB_BRIDGE_CTL_PREFETCH_MEM0 << i)) ? " (prefetchable)" : "");
    }
  for (i=0; i<2; i++)
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
      limit = (limit & PCI_CB_IO_RANGE_MASK) + 3;
      if (base <= limit || verb)
	printf("\tI/O window %d: %08x-%08x%s\n", i, base, limit,
	       (cmd & PCI_COMMAND_IO) ? "" : " [disabled]");
    }

  if (get_conf_word(d, PCI_CB_SEC_STATUS) & PCI_STATUS_SIG_SYSTEM_ERROR)
    printf("\tSecondary status: SERR\n");
  if (verbose > 1)
    printf("\tBridgeCtl: Parity%c SERR%c ISA%c VGA%c MAbort%c >Reset%c 16bInt%c PostWrite%c\n",
	   FLAG(brc, PCI_CB_BRIDGE_CTL_PARITY),
	   FLAG(brc, PCI_CB_BRIDGE_CTL_SERR),
	   FLAG(brc, PCI_CB_BRIDGE_CTL_ISA),
	   FLAG(brc, PCI_CB_BRIDGE_CTL_VGA),
	   FLAG(brc, PCI_CB_BRIDGE_CTL_MASTER_ABORT),
	   FLAG(brc, PCI_CB_BRIDGE_CTL_CB_RESET),
	   FLAG(brc, PCI_CB_BRIDGE_CTL_16BIT_INT),
	   FLAG(brc, PCI_CB_BRIDGE_CTL_POST_WRITES));

  if (d->config_cached < 128)
    {
      printf("\t<access denied to the rest>\n");
      return;
    }

  exca = get_conf_word(d, PCI_CB_LEGACY_MODE_BASE);
  if (exca)
    printf("\t16-bit legacy interface ports at %04x\n", exca);
  show_caps(d, PCI_CB_CAPABILITY_LIST);
}

static void
show_htype_unknown(struct device *d)
{
  struct pci_dev *p = d->dev;
  u64 base, limit, flags;
  const char *str;
  int i, bits;

  if (pacc->buscentric)
    return;

  show_bases(d, 6, 1);
  for (i = 0; i < 4; i++)
    {
      if (!p->bridge_base_addr[i])
        continue;
      base = p->bridge_base_addr[i];
      limit = base + p->bridge_size[i] - 1;
      flags = p->bridge_flags[i];
      if (flags & PCI_IORESOURCE_IO)
        {
          bits = (flags & PCI_IORESOURCE_IO_16BIT_ADDR) ? 16 : 32;
          str = "\tI/O behind bridge";
        }
      else if (flags & PCI_IORESOURCE_MEM)
        {
          bits = (flags & PCI_IORESOURCE_MEM_64) ? 64 : 32;
          if (flags & PCI_IORESOURCE_PREFETCH)
            str = "\tPrefetchable memory behind bridge";
          else
            str = "\tMemory behind bridge";
        }
      else
        {
          bits = 0;
          str = "\tUnknown resource behind bridge";
        }
      show_range(str, base, limit, bits, 0);
    }
  show_rom(d, -1);
}

static void
show_verbose(struct device *d)
{
  struct pci_dev *p = d->dev;
  int unknown_config_data = 0;
  word class = p->device_class;
  byte htype = d->no_config_access ? -1 : (get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f);
  byte bist;
  byte max_lat, min_gnt;
  char *dt_node, *iommu_group;

  show_terse(d);

  pci_fill_info(p, PCI_FILL_IRQ | PCI_FILL_BASES | PCI_FILL_ROM_BASE | PCI_FILL_SIZES |
    PCI_FILL_PHYS_SLOT | PCI_FILL_NUMA_NODE | PCI_FILL_DT_NODE | PCI_FILL_IOMMU_GROUP |
    PCI_FILL_BRIDGE_BASES | PCI_FILL_CLASS_EXT | PCI_FILL_SUBSYS);

  switch (htype)
    {
    case PCI_HEADER_TYPE_NORMAL:
      if (class == PCI_CLASS_BRIDGE_PCI)
	printf("\t!!! Invalid class %04x for header type %02x\n", class, htype);
      bist = get_conf_byte(d, PCI_BIST);
      max_lat = get_conf_byte(d, PCI_MAX_LAT);
      min_gnt = get_conf_byte(d, PCI_MIN_GNT);
      break;
    case PCI_HEADER_TYPE_BRIDGE:
      if ((class >> 8) != PCI_BASE_CLASS_BRIDGE)
	printf("\t!!! Invalid class %04x for header type %02x\n", class, htype);
      bist = get_conf_byte(d, PCI_BIST);
      min_gnt = max_lat = 0;
      break;
    case PCI_HEADER_TYPE_CARDBUS:
      if ((class >> 8) != PCI_BASE_CLASS_BRIDGE)
	printf("\t!!! Invalid class %04x for header type %02x\n", class, htype);
      bist = get_conf_byte(d, PCI_BIST);
      min_gnt = max_lat = 0;
      break;
    default:
      if (!d->no_config_access)
      printf("\t!!! Unknown header type %02x\n", htype);
      bist = 0;
      min_gnt = max_lat = 0;
      unknown_config_data = 1;
    }

  if (p->phy_slot)
    printf("\tPhysical Slot: %s\n", p->phy_slot);

  if (dt_node = pci_get_string_property(p, PCI_FILL_DT_NODE))
    printf("\tDevice tree node: %s\n", dt_node);

  if (!unknown_config_data && verbose > 1)
    {
      word cmd = get_conf_word(d, PCI_COMMAND);
      word status = get_conf_word(d, PCI_STATUS);
      printf("\tControl: I/O%c Mem%c BusMaster%c SpecCycle%c MemWINV%c VGASnoop%c ParErr%c Stepping%c SERR%c FastB2B%c DisINTx%c\n",
	     FLAG(cmd, PCI_COMMAND_IO),
	     FLAG(cmd, PCI_COMMAND_MEMORY),
	     FLAG(cmd, PCI_COMMAND_MASTER),
	     FLAG(cmd, PCI_COMMAND_SPECIAL),
	     FLAG(cmd, PCI_COMMAND_INVALIDATE),
	     FLAG(cmd, PCI_COMMAND_VGA_PALETTE),
	     FLAG(cmd, PCI_COMMAND_PARITY),
	     FLAG(cmd, PCI_COMMAND_WAIT),
	     FLAG(cmd, PCI_COMMAND_SERR),
	     FLAG(cmd, PCI_COMMAND_FAST_BACK),
	     FLAG(cmd, PCI_COMMAND_DISABLE_INTx));
      printf("\tStatus: Cap%c 66MHz%c UDF%c FastB2B%c ParErr%c DEVSEL=%s >TAbort%c <TAbort%c <MAbort%c >SERR%c <PERR%c INTx%c\n",
	     FLAG(status, PCI_STATUS_CAP_LIST),
	     FLAG(status, PCI_STATUS_66MHZ),
	     FLAG(status, PCI_STATUS_UDF),
	     FLAG(status, PCI_STATUS_FAST_BACK),
	     FLAG(status, PCI_STATUS_PARITY),
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_SLOW) ? "slow" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_MEDIUM) ? "medium" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_FAST) ? "fast" : "??",
	     FLAG(status, PCI_STATUS_SIG_TARGET_ABORT),
	     FLAG(status, PCI_STATUS_REC_TARGET_ABORT),
	     FLAG(status, PCI_STATUS_REC_MASTER_ABORT),
	     FLAG(status, PCI_STATUS_SIG_SYSTEM_ERROR),
	     FLAG(status, PCI_STATUS_DETECTED_PARITY),
	     FLAG(status, PCI_STATUS_INTx));
      if (cmd & PCI_COMMAND_MASTER)
	{
	  byte latency = get_conf_byte(d, PCI_LATENCY_TIMER);
	  byte cache_line = get_conf_byte(d, PCI_CACHE_LINE_SIZE);
	  printf("\tLatency: %d", latency);
	  if (min_gnt || max_lat)
	    {
	      printf(" (");
	      if (min_gnt)
		printf("%dns min", min_gnt*250);
	      if (min_gnt && max_lat)
		printf(", ");
	      if (max_lat)
		printf("%dns max", max_lat*250);
	      putchar(')');
	    }
	  if (cache_line)
	    printf(", Cache Line Size: %d bytes", cache_line * 4);
	  putchar('\n');
	}
    }

  if (verbose > 1)
    {
      byte int_pin = unknown_config_data ? 0 : get_conf_byte(d, PCI_INTERRUPT_PIN);
      if (int_pin || p->irq)
	printf("\tInterrupt: pin %c routed to IRQ " PCIIRQ_FMT "\n",
	       (int_pin ? 'A' + int_pin - 1 : '?'), p->irq);
      if (p->numa_node != -1)
	printf("\tNUMA node: %d\n", p->numa_node);
      if (iommu_group = pci_get_string_property(p, PCI_FILL_IOMMU_GROUP))
	printf("\tIOMMU group: %s\n", iommu_group);
    }

  if (!unknown_config_data && verbose <= 1)
    {
      word cmd = get_conf_word(d, PCI_COMMAND);
      word status = get_conf_word(d, PCI_STATUS);
      byte latency = get_conf_byte(d, PCI_LATENCY_TIMER);
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
	printf("66MHz, ");
      if (status & PCI_STATUS_UDF)
	printf("user-definable features, ");
      printf("%s devsel",
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_SLOW) ? "slow" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_MEDIUM) ? "medium" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_FAST) ? "fast" : "??");
      if (cmd & PCI_COMMAND_MASTER)
	printf(", latency %d", latency);
      if (p->irq)
	printf(", IRQ " PCIIRQ_FMT, p->irq);
      if (p->numa_node != -1)
	printf(", NUMA node %d", p->numa_node);
      if (iommu_group = pci_get_string_property(p, PCI_FILL_IOMMU_GROUP))
	printf(", IOMMU group %s", iommu_group);
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
    default:
      show_htype_unknown(d);
    }
}

/*** Machine-readable dumps ***/

static void
show_hex_dump(struct device *d)
{
  unsigned int i, cnt;

  if (d->no_config_access)
    {
      printf("WARNING: Cannot show hex-dump of the config space\n");
      return;
    }

  cnt = d->config_cached;
  if (opt_hex >= 3 && config_fetch(d, cnt, 256-cnt))
    {
      cnt = 256;
      if (opt_hex >= 4 && config_fetch(d, 256, 4096-256))
	cnt = 4096;
    }

  for (i=0; i<cnt; i++)
    {
      if (! (i & 15))
	printf("%02x:", i);
      printf(" %02x", get_conf_byte(d, i));
      if ((i & 15) == 15)
	putchar('\n');
    }
}

static void
print_shell_escaped(char *c)
{
  printf(" \"");
  while (*c)
    {
      if (*c == '"' || *c == '\\')
	putchar('\\');
      putchar(*c++);
    }
  putchar('"');
}

static void
show_machine(struct device *d)
{
  struct pci_dev *p = d->dev;
  char classbuf[128], vendbuf[128], devbuf[128], svbuf[128], sdbuf[128];
  char *dt_node, *iommu_group;

  if (verbose)
    {
      pci_fill_info(p, PCI_FILL_PHYS_SLOT | PCI_FILL_NUMA_NODE | PCI_FILL_DT_NODE | PCI_FILL_IOMMU_GROUP);
      printf((opt_machine >= 2) ? "Slot:\t" : "Device:\t");
      show_slot_name(d);
      putchar('\n');
      printf("Class:\t%s\n",
	     pci_lookup_name(pacc, classbuf, sizeof(classbuf), PCI_LOOKUP_CLASS, p->device_class));
      printf("Vendor:\t%s\n",
	     pci_lookup_name(pacc, vendbuf, sizeof(vendbuf), PCI_LOOKUP_VENDOR, p->vendor_id, p->device_id));
      printf("Device:\t%s\n",
	     pci_lookup_name(pacc, devbuf, sizeof(devbuf), PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id));
      if ((p->known_fields & PCI_FILL_SUBSYS) &&
	  p->subsys_vendor_id && p->subsys_vendor_id != 0xffff)
	{
	  printf("SVendor:\t%s\n",
		 pci_lookup_name(pacc, svbuf, sizeof(svbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR, p->subsys_vendor_id));
	  printf("SDevice:\t%s\n",
		 pci_lookup_name(pacc, sdbuf, sizeof(sdbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id, p->subsys_vendor_id, p->subsys_id));
	}
      if (p->phy_slot)
	printf("PhySlot:\t%s\n", p->phy_slot);
      if ((p->known_fields & PCI_FILL_CLASS_EXT) && p->rev_id)
	printf("Rev:\t%02x\n", p->rev_id);
      if (p->known_fields & PCI_FILL_CLASS_EXT)
	printf("ProgIf:\t%02x\n", p->prog_if);
      if (opt_kernel)
	show_kernel_machine(d);
      if (p->numa_node != -1)
	printf("NUMANode:\t%d\n", p->numa_node);
      if (dt_node = pci_get_string_property(p, PCI_FILL_DT_NODE))
        printf("DTNode:\t%s\n", dt_node);
      if (iommu_group = pci_get_string_property(p, PCI_FILL_IOMMU_GROUP))
	printf("IOMMUGroup:\t%s\n", iommu_group);
    }
  else
    {
      show_slot_name(d);
      print_shell_escaped(pci_lookup_name(pacc, classbuf, sizeof(classbuf), PCI_LOOKUP_CLASS, p->device_class));
      print_shell_escaped(pci_lookup_name(pacc, vendbuf, sizeof(vendbuf), PCI_LOOKUP_VENDOR, p->vendor_id, p->device_id));
      print_shell_escaped(pci_lookup_name(pacc, devbuf, sizeof(devbuf), PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id));
      if ((p->known_fields & PCI_FILL_CLASS_EXT) && p->rev_id)
	printf(" -r%02x", p->rev_id);
      if (p->known_fields & PCI_FILL_CLASS_EXT)
	printf(" -p%02x", p->prog_if);
      if ((p->known_fields & PCI_FILL_SUBSYS) &&
	  p->subsys_vendor_id && p->subsys_vendor_id != 0xffff)
	{
	  print_shell_escaped(pci_lookup_name(pacc, svbuf, sizeof(svbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR, p->subsys_vendor_id));
	  print_shell_escaped(pci_lookup_name(pacc, sdbuf, sizeof(sdbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id, p->subsys_vendor_id, p->subsys_id));
	}
      else
	printf(" \"\" \"\"");
      putchar('\n');
    }
}

/*** Main show function ***/

void
show_device(struct device *d)
{
  if (opt_machine)
    show_machine(d);
  else
    {
      if (verbose)
	show_verbose(d);
      else
	show_terse(d);
      if (opt_kernel || verbose)
	show_kernel(d);
    }
  if (opt_hex)
    show_hex_dump(d);
  if (verbose || opt_hex)
    putchar('\n');
}

static void
show(void)
{
  struct device *d;

  for (d=first_dev; d; d=d->next)
    if (pci_filter_match(&filter, d->dev))
      show_device(d);
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
	pacc->numeric_ids++;
	break;
      case 'v':
	verbose++;
	break;
      case 'b':
	pacc->buscentric = 1;
	break;
      case 's':
	if (msg = pci_filter_parse_slot(&filter, optarg))
	  die("-s: %s", msg);
	opt_filter = 1;
	break;
      case 'd':
	if (msg = pci_filter_parse_id(&filter, optarg))
	  die("-d: %s", msg);
	opt_filter = 1;
	break;
      case 'x':
	opt_hex++;
	break;
      case 'P':
	opt_path++;
	need_topology = 1;
	break;
      case 't':
	opt_tree++;
	need_topology = 1;
	break;
      case 'i':
        pci_set_name_list_path(pacc, optarg, 0);
	break;
      case 'm':
	opt_machine++;
	break;
      case 'p':
	opt_pcimap = optarg;
	break;
#ifdef PCI_OS_LINUX
      case 'k':
	opt_kernel++;
	break;
#endif
      case 'M':
	opt_map_mode++;
	break;
      case 'D':
	opt_domains = 2;
	break;
#ifdef PCI_USE_DNS
      case 'q':
	opt_query_dns++;
	break;
      case 'Q':
	opt_query_all = 1;
	break;
#else
      case 'q':
      case 'Q':
	die("DNS queries are not available in this version");
#endif
      default:
	if (parse_generic_option(i, pacc, optarg))
	  break;
      bad:
	fprintf(stderr, help_msg, pacc->id_file_name);
	return 1;
      }
  if (optind < argc)
    goto bad;

  if (opt_query_dns)
    {
      pacc->id_lookup_mode |= PCI_LOOKUP_NETWORK;
      if (opt_query_dns > 1)
	pacc->id_lookup_mode |= PCI_LOOKUP_REFRESH_CACHE;
    }
  if (opt_query_all)
    pacc->id_lookup_mode |= PCI_LOOKUP_NETWORK | PCI_LOOKUP_SKIP_LOCAL;

  pci_init(pacc);
  if (opt_map_mode)
    {
      if (need_topology)
	die("Bus mapping mode does not recognize bus topology");
      map_the_bus();
    }
  else
    {
      scan_devices();
      sort_them();
      if (need_topology)
	grow_tree();
      if (opt_tree)
	show_forest(opt_filter ? &filter : NULL);
      else
	show();
    }
  show_kernel_cleanup();
  pci_cleanup(pacc);

  return (seen_errors ? 2 : 0);
}
