/*
 *	The PCI Utilities -- List All PCI Devices
 *
 *	Copyright (c) 1997--2007 Martin Mares <mj@ucw.cz>
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
static int opt_buscentric;		/* Show bus addresses/IRQ's instead of CPU-visible ones */
static int opt_hex;			/* Show contents of config space as hexadecimal numbers */
static struct pci_filter filter;	/* Device filter */
static int opt_tree;			/* Show bus tree */
static int opt_machine;			/* Generate machine-readable output */
static int opt_map_mode;		/* Bus mapping mode enabled */
static int opt_domains;			/* Show domain numbers (0=disabled, 1=auto-detected, 2=requested) */

const char program_name[] = "lspci";

static char options[] = "nvbxs:d:ti:mgMD" GENERIC_OPTIONS ;

static char help_msg[] = "\
Usage: lspci [<switches>]\n\
\n\
-v\t\tBe verbose\n\
-n\t\tShow numeric ID's\n\
-nn\t\tShow both textual and numeric ID's (names & numbers)\n\
-b\t\tBus-centric view (PCI addresses and IRQ's instead of those seen by the CPU)\n\
-x\t\tShow hex-dump of the standard portion of config space\n\
-xxx\t\tShow hex-dump of the whole config space (dangerous; root only)\n\
-xxxx\t\tShow hex-dump of the 4096-byte extended config space (root only)\n\
-s [[[[<domain>]:]<bus>]:][<slot>][.[<func>]]\tShow only devices in selected slots\n\
-d [<vendor>]:[<device>]\tShow only selected devices\n\
-t\t\tShow bus tree\n\
-m\t\tProduce machine-readable output\n\
-i <file>\tUse specified ID database instead of %s\n\
-D\t\tAlways show domain numbers\n\
-M\t\tEnable `bus mapping' mode (dangerous; root only)\n"
GENERIC_HELP
;

/*** Communication with libpci ***/

static struct pci_access *pacc;

/*
 *  If we aren't being compiled by GCC, use xmalloc() instead of alloca().
 *  This increases our memory footprint, but only slightly since we don't
 *  use alloca() much.
 */
#if defined (__FreeBSD__) || defined (__NetBSD__) || defined (__OpenBSD__) || defined (__DragonFly__)
/* alloca() is defined in stdlib.h */
#elif defined(__GNUC__) && !defined(PCI_OS_WINDOWS)
#include <alloca.h>
#else
#undef alloca
#define alloca xmalloc
#endif

/*** Our view of the PCI bus ***/

struct device {
  struct device *next;
  struct pci_dev *dev;
  unsigned int config_cached, config_bufsize;
  byte *config;				/* Cached configuration space data */
  byte *present;			/* Maps which configuration bytes are present */
};

static struct device *first_dev;
static int seen_errors;

static int
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

static struct device *
scan_device(struct pci_dev *p)
{
  struct device *d;

  if (p->domain && !opt_domains)
    opt_domains = 1;
  if (!pci_filter_match(&filter, p))
    return NULL;
  d = xmalloc(sizeof(struct device));
  memset(d, 0, sizeof(*d));
  d->dev = p;
  d->config_cached = d->config_bufsize = 64;
  d->config = xmalloc(64);
  d->present = xmalloc(64);
  memset(d->present, 1, 64);
  if (!pci_read_block(p, 0, d->config, 64))
    {
      fprintf(stderr, "lspci: Unable to read the standard configuration space header of device %04x:%02x:%02x.%d\n",
	      p->domain, p->bus, p->dev, p->func);
      seen_errors++;
      return NULL;
    }
  if ((d->config[PCI_HEADER_TYPE] & 0x7f) == PCI_HEADER_TYPE_CARDBUS)
    {
      /* For cardbus bridges, we need to fetch 64 bytes more to get the
       * full standard header... */
      if (config_fetch(d, 64, 64))
	d->config_cached += 64;
    }
  pci_setup_cache(p, d->config, d->config_cached);
  pci_fill_info(p, PCI_FILL_IDENT | PCI_FILL_CLASS | PCI_FILL_IRQ | PCI_FILL_BASES | PCI_FILL_ROM_BASE | PCI_FILL_SIZES);
  return d;
}

static void
scan_devices(void)
{
  struct device *d;
  struct pci_dev *p;

  pci_scan_bus(pacc);
  for(p=pacc->devices; p; p=p->next)
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

static inline byte
get_conf_byte(struct device *d, unsigned int pos)
{
  check_conf_range(d, pos, 1);
  return d->config[pos];
}

static word
get_conf_word(struct device *d, unsigned int pos)
{
  check_conf_range(d, pos, 2);
  return d->config[pos] | (d->config[pos+1] << 8);
}

static u32
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

/*** Normal output ***/

#define FLAG(x,y) ((x & y) ? '+' : '-')

static void
show_slot_name(struct device *d)
{
  struct pci_dev *p = d->dev;

  if (!opt_machine ? opt_domains : (p->domain || opt_domains >= 2))
    printf("%04x:", p->domain);
  printf("%02x:%02x.%d", p->bus, p->dev, p->func);
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
  if (c = get_conf_byte(d, PCI_REVISION_ID))
    printf(" (rev %02x)", c);
  if (verbose)
    {
      char *x;
      c = get_conf_byte(d, PCI_CLASS_PROG);
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
}

/*** Capabilities ***/

static void
cap_pm(struct device *d, int where, int cap)
{
  int t, b;
  static int pm_aux_current[8] = { 0, 55, 100, 160, 220, 270, 320, 375 };

  printf("Power Management version %d\n", cap & PCI_PM_CAP_VER_MASK);
  if (verbose < 2)
    return;
  printf("\t\tFlags: PMEClk%c DSI%c D1%c D2%c AuxCurrent=%dmA PME(D0%c,D1%c,D2%c,D3hot%c,D3cold%c)\n",
	 FLAG(cap, PCI_PM_CAP_PME_CLOCK),
	 FLAG(cap, PCI_PM_CAP_DSI),
	 FLAG(cap, PCI_PM_CAP_D1),
	 FLAG(cap, PCI_PM_CAP_D2),
	 pm_aux_current[(cap >> 6) & 7],
	 FLAG(cap, PCI_PM_CAP_PME_D0),
	 FLAG(cap, PCI_PM_CAP_PME_D1),
	 FLAG(cap, PCI_PM_CAP_PME_D2),
	 FLAG(cap, PCI_PM_CAP_PME_D3_HOT),
	 FLAG(cap, PCI_PM_CAP_PME_D3_COLD));
  if (!config_fetch(d, where + PCI_PM_CTRL, PCI_PM_SIZEOF - PCI_PM_CTRL))
    return;
  t = get_conf_word(d, where + PCI_PM_CTRL);
  printf("\t\tStatus: D%d PME-Enable%c DSel=%d DScale=%d PME%c\n",
	 t & PCI_PM_CTRL_STATE_MASK,
	 FLAG(t, PCI_PM_CTRL_PME_ENABLE),
	 (t & PCI_PM_CTRL_DATA_SEL_MASK) >> 9,
	 (t & PCI_PM_CTRL_DATA_SCALE_MASK) >> 13,
	 FLAG(t, PCI_PM_CTRL_PME_STATUS));
  b = get_conf_byte(d, where + PCI_PM_PPB_EXTENSIONS);
  if (b)
    printf("\t\tBridge: PM%c B3%c\n",
	   FLAG(t, PCI_PM_BPCC_ENABLE),
	   FLAG(~t, PCI_PM_PPB_B2_B3));
}

static void
format_agp_rate(int rate, char *buf, int agp3)
{
  char *c = buf;
  int i;

  for(i=0; i<=2; i++)
    if (rate & (1 << i))
      {
	if (c != buf)
	  *c++ = ',';
	c += sprintf(c, "x%d", 1 << (i + 2*agp3));
      }
  if (c != buf)
    *c = 0;
  else
    strcpy(buf, "<none>");
}

static void
cap_agp(struct device *d, int where, int cap)
{
  u32 t;
  char rate[16];
  int ver, rev;
  int agp3 = 0;

  ver = (cap >> 4) & 0x0f;
  rev = cap & 0x0f;
  printf("AGP version %x.%x\n", ver, rev);
  if (verbose < 2)
    return;
  if (!config_fetch(d, where + PCI_AGP_STATUS, PCI_AGP_SIZEOF - PCI_AGP_STATUS))
    return;
  t = get_conf_long(d, where + PCI_AGP_STATUS);
  if (ver >= 3 && (t & PCI_AGP_STATUS_AGP3))
    agp3 = 1;
  format_agp_rate(t & 7, rate, agp3);
  printf("\t\tStatus: RQ=%d Iso%c ArqSz=%d Cal=%d SBA%c ITACoh%c GART64%c HTrans%c 64bit%c FW%c AGP3%c Rate=%s\n",
	 ((t & PCI_AGP_STATUS_RQ_MASK) >> 24U) + 1,
	 FLAG(t, PCI_AGP_STATUS_ISOCH),
	 ((t & PCI_AGP_STATUS_ARQSZ_MASK) >> 13),
	 ((t & PCI_AGP_STATUS_CAL_MASK) >> 10),
	 FLAG(t, PCI_AGP_STATUS_SBA),
	 FLAG(t, PCI_AGP_STATUS_ITA_COH),
	 FLAG(t, PCI_AGP_STATUS_GART64),
	 FLAG(t, PCI_AGP_STATUS_HTRANS),
	 FLAG(t, PCI_AGP_STATUS_64BIT),
	 FLAG(t, PCI_AGP_STATUS_FW),
	 FLAG(t, PCI_AGP_STATUS_AGP3),
	 rate);
  t = get_conf_long(d, where + PCI_AGP_COMMAND);
  format_agp_rate(t & 7, rate, agp3);
  printf("\t\tCommand: RQ=%d ArqSz=%d Cal=%d SBA%c AGP%c GART64%c 64bit%c FW%c Rate=%s\n",
	 ((t & PCI_AGP_COMMAND_RQ_MASK) >> 24U) + 1,
	 ((t & PCI_AGP_COMMAND_ARQSZ_MASK) >> 13),
	 ((t & PCI_AGP_COMMAND_CAL_MASK) >> 10),
	 FLAG(t, PCI_AGP_COMMAND_SBA),
	 FLAG(t, PCI_AGP_COMMAND_AGP),
	 FLAG(t, PCI_AGP_COMMAND_GART64),
	 FLAG(t, PCI_AGP_COMMAND_64BIT),
	 FLAG(t, PCI_AGP_COMMAND_FW),
	 rate);
}

static void
cap_pcix_nobridge(struct device *d, int where)
{
  u16 command;
  u32 status;
  static const byte max_outstanding[8] = { 1, 2, 3, 4, 8, 12, 16, 32 };

  printf("PCI-X non-bridge device\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_PCIX_STATUS, 4))
    return;

  command = get_conf_word(d, where + PCI_PCIX_COMMAND);
  status = get_conf_long(d, where + PCI_PCIX_STATUS);
  printf("\t\tCommand: DPERE%c ERO%c RBC=%d OST=%d\n",
	 FLAG(command, PCI_PCIX_COMMAND_DPERE),
	 FLAG(command, PCI_PCIX_COMMAND_ERO),
	 1 << (9 + ((command & PCI_PCIX_COMMAND_MAX_MEM_READ_BYTE_COUNT) >> 2U)),
	 max_outstanding[(command & PCI_PCIX_COMMAND_MAX_OUTSTANDING_SPLIT_TRANS) >> 4U]);
  printf("\t\tStatus: Dev=%02x:%02x.%d 64bit%c 133MHz%c SCD%c USC%c DC=%s DMMRBC=%u DMOST=%u DMCRS=%u RSCEM%c 266MHz%c 533MHz%c\n",
	 ((status >> 8) & 0xff),
	 ((status >> 3) & 0x1f),
	 (status & PCI_PCIX_STATUS_FUNCTION),
	 FLAG(status, PCI_PCIX_STATUS_64BIT),
	 FLAG(status, PCI_PCIX_STATUS_133MHZ),
	 FLAG(status, PCI_PCIX_STATUS_SC_DISCARDED),
	 FLAG(status, PCI_PCIX_STATUS_UNEXPECTED_SC),
	 ((status & PCI_PCIX_STATUS_DEVICE_COMPLEXITY) ? "bridge" : "simple"),
	 1 << (9 + ((status >> 21) & 3U)),
	 max_outstanding[(status >> 23) & 7U],
	 1 << (3 + ((status >> 26) & 7U)),
	 FLAG(status, PCI_PCIX_STATUS_RCVD_SC_ERR_MESS),
	 FLAG(status, PCI_PCIX_STATUS_266MHZ),
	 FLAG(status, PCI_PCIX_STATUS_533MHZ));
}

static void
cap_pcix_bridge(struct device *d, int where)
{
  static const char * const sec_clock_freq[8] = { "conv", "66MHz", "100MHz", "133MHz", "?4", "?5", "?6", "?7" };
  u16 secstatus;
  u32 status, upstcr, downstcr;

  printf("PCI-X bridge device\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_PCIX_BRIDGE_STATUS, 12))
    return;

  secstatus = get_conf_word(d, where + PCI_PCIX_BRIDGE_SEC_STATUS);
  printf("\t\tSecondary Status: 64bit%c 133MHz%c SCD%c USC%c SCO%c SRD%c Freq=%s\n",
	 FLAG(secstatus, PCI_PCIX_BRIDGE_SEC_STATUS_64BIT),
	 FLAG(secstatus, PCI_PCIX_BRIDGE_SEC_STATUS_133MHZ),
	 FLAG(secstatus, PCI_PCIX_BRIDGE_SEC_STATUS_SC_DISCARDED),
	 FLAG(secstatus, PCI_PCIX_BRIDGE_SEC_STATUS_UNEXPECTED_SC),
	 FLAG(secstatus, PCI_PCIX_BRIDGE_SEC_STATUS_SC_OVERRUN),
	 FLAG(secstatus, PCI_PCIX_BRIDGE_SEC_STATUS_SPLIT_REQUEST_DELAYED),
	 sec_clock_freq[(secstatus >> 6) & 7]);
  status = get_conf_long(d, where + PCI_PCIX_BRIDGE_STATUS);
  printf("\t\tStatus: Dev=%02x:%02x.%d 64bit%c 133MHz%c SCD%c USC%c SCO%c SRD%c\n",
	 ((status >> 8) & 0xff),
	 ((status >> 3) & 0x1f),
	 (status & PCI_PCIX_BRIDGE_STATUS_FUNCTION),
	 FLAG(status, PCI_PCIX_BRIDGE_STATUS_64BIT),
	 FLAG(status, PCI_PCIX_BRIDGE_STATUS_133MHZ),
	 FLAG(status, PCI_PCIX_BRIDGE_STATUS_SC_DISCARDED),
	 FLAG(status, PCI_PCIX_BRIDGE_STATUS_UNEXPECTED_SC),
	 FLAG(status, PCI_PCIX_BRIDGE_STATUS_SC_OVERRUN),
	 FLAG(status, PCI_PCIX_BRIDGE_STATUS_SPLIT_REQUEST_DELAYED));
  upstcr = get_conf_long(d, where + PCI_PCIX_BRIDGE_UPSTREAM_SPLIT_TRANS_CTRL);
  printf("\t\tUpstream: Capacity=%u CommitmentLimit=%u\n",
	 (upstcr & PCI_PCIX_BRIDGE_STR_CAPACITY),
	 (upstcr >> 16) & 0xffff);
  downstcr = get_conf_long(d, where + PCI_PCIX_BRIDGE_DOWNSTREAM_SPLIT_TRANS_CTRL);
  printf("\t\tDownstream: Capacity=%u CommitmentLimit=%u\n",
	 (downstcr & PCI_PCIX_BRIDGE_STR_CAPACITY),
	 (downstcr >> 16) & 0xffff);
}

static void
cap_pcix(struct device *d, int where)
{
  switch (get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f)
    {
    case PCI_HEADER_TYPE_NORMAL:
      cap_pcix_nobridge(d, where);
      break;
    case PCI_HEADER_TYPE_BRIDGE:
      cap_pcix_bridge(d, where);
      break;
    }
}

static inline char *
ht_link_width(unsigned width)
{
  static char * const widths[8] = { "8bit", "16bit", "[2]", "32bit", "2bit", "4bit", "[6]", "N/C" };
  return widths[width];
}

static inline char *
ht_link_freq(unsigned freq)
{
  static char * const freqs[16] = { "200MHz", "300MHz", "400MHz", "500MHz", "600MHz", "800MHz", "1.0GHz", "1.2GHz",
				    "1.4GHz", "1.6GHz", "[a]", "[b]", "[c]", "[d]", "[e]", "Vend" };
  return freqs[freq];
}

static void
cap_ht_pri(struct device *d, int where, int cmd)
{
  u16 lctr0, lcnf0, lctr1, lcnf1, eh;
  u8 rid, lfrer0, lfcap0, ftr, lfrer1, lfcap1, mbu, mlu, bn;
  char *fmt;

  printf("HyperTransport: Slave or Primary Interface\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_HT_PRI_LCTR0, PCI_HT_PRI_SIZEOF - PCI_HT_PRI_LCTR0))
    return;
  rid = get_conf_byte(d, where + PCI_HT_PRI_RID);
  if (rid < 0x23 && rid > 0x11)
    printf("\t\t!!! Possibly incomplete decoding\n");

  if (rid >= 0x23)
    fmt = "\t\tCommand: BaseUnitID=%u UnitCnt=%u MastHost%c DefDir%c DUL%c\n";
  else
    fmt = "\t\tCommand: BaseUnitID=%u UnitCnt=%u MastHost%c DefDir%c\n";
  printf(fmt,
	 (cmd & PCI_HT_PRI_CMD_BUID),
	 (cmd & PCI_HT_PRI_CMD_UC) >> 5,
	 FLAG(cmd, PCI_HT_PRI_CMD_MH),
	 FLAG(cmd, PCI_HT_PRI_CMD_DD),
	 FLAG(cmd, PCI_HT_PRI_CMD_DUL));
  lctr0 = get_conf_word(d, where + PCI_HT_PRI_LCTR0);
  if (rid >= 0x23)
    fmt = "\t\tLink Control 0: CFlE%c CST%c CFE%c <LkFail%c Init%c EOC%c TXO%c <CRCErr=%x IsocEn%c LSEn%c ExtCTL%c 64b%c\n";
  else
    fmt = "\t\tLink Control 0: CFlE%c CST%c CFE%c <LkFail%c Init%c EOC%c TXO%c <CRCErr=%x\n";
  printf(fmt,
	 FLAG(lctr0, PCI_HT_LCTR_CFLE),
	 FLAG(lctr0, PCI_HT_LCTR_CST),
	 FLAG(lctr0, PCI_HT_LCTR_CFE),
	 FLAG(lctr0, PCI_HT_LCTR_LKFAIL),
	 FLAG(lctr0, PCI_HT_LCTR_INIT),
	 FLAG(lctr0, PCI_HT_LCTR_EOC),
	 FLAG(lctr0, PCI_HT_LCTR_TXO),
	 (lctr0 & PCI_HT_LCTR_CRCERR) >> 8,
	 FLAG(lctr0, PCI_HT_LCTR_ISOCEN),
	 FLAG(lctr0, PCI_HT_LCTR_LSEN),
	 FLAG(lctr0, PCI_HT_LCTR_EXTCTL),
	 FLAG(lctr0, PCI_HT_LCTR_64B));
  lcnf0 = get_conf_word(d, where + PCI_HT_PRI_LCNF0);
  if (rid >= 0x23)
    fmt = "\t\tLink Config 0: MLWI=%1$s DwFcIn%5$c MLWO=%2$s DwFcOut%6$c LWI=%3$s DwFcInEn%7$c LWO=%4$s DwFcOutEn%8$c\n";
  else
    fmt = "\t\tLink Config 0: MLWI=%s MLWO=%s LWI=%s LWO=%s\n";
  printf(fmt,
	 ht_link_width(lcnf0 & PCI_HT_LCNF_MLWI),
	 ht_link_width((lcnf0 & PCI_HT_LCNF_MLWO) >> 4),
	 ht_link_width((lcnf0 & PCI_HT_LCNF_LWI) >> 8),
	 ht_link_width((lcnf0 & PCI_HT_LCNF_LWO) >> 12),
	 FLAG(lcnf0, PCI_HT_LCNF_DFI),
	 FLAG(lcnf0, PCI_HT_LCNF_DFO),
	 FLAG(lcnf0, PCI_HT_LCNF_DFIE),
	 FLAG(lcnf0, PCI_HT_LCNF_DFOE));
  lctr1 = get_conf_word(d, where + PCI_HT_PRI_LCTR1);
  if (rid >= 0x23)
    fmt = "\t\tLink Control 1: CFlE%c CST%c CFE%c <LkFail%c Init%c EOC%c TXO%c <CRCErr=%x IsocEn%c LSEn%c ExtCTL%c 64b%c\n";
  else
    fmt = "\t\tLink Control 1: CFlE%c CST%c CFE%c <LkFail%c Init%c EOC%c TXO%c <CRCErr=%x\n";
  printf(fmt,
	 FLAG(lctr1, PCI_HT_LCTR_CFLE),
	 FLAG(lctr1, PCI_HT_LCTR_CST),
	 FLAG(lctr1, PCI_HT_LCTR_CFE),
	 FLAG(lctr1, PCI_HT_LCTR_LKFAIL),
	 FLAG(lctr1, PCI_HT_LCTR_INIT),
	 FLAG(lctr1, PCI_HT_LCTR_EOC),
	 FLAG(lctr1, PCI_HT_LCTR_TXO),
	 (lctr1 & PCI_HT_LCTR_CRCERR) >> 8,
	 FLAG(lctr1, PCI_HT_LCTR_ISOCEN),
	 FLAG(lctr1, PCI_HT_LCTR_LSEN),
	 FLAG(lctr1, PCI_HT_LCTR_EXTCTL),
	 FLAG(lctr1, PCI_HT_LCTR_64B));
  lcnf1 = get_conf_word(d, where + PCI_HT_PRI_LCNF1);
  if (rid >= 0x23)
    fmt = "\t\tLink Config 1: MLWI=%1$s DwFcIn%5$c MLWO=%2$s DwFcOut%6$c LWI=%3$s DwFcInEn%7$c LWO=%4$s DwFcOutEn%8$c\n";
  else
    fmt = "\t\tLink Config 1: MLWI=%s MLWO=%s LWI=%s LWO=%s\n";
  printf(fmt,
	 ht_link_width(lcnf1 & PCI_HT_LCNF_MLWI),
	 ht_link_width((lcnf1 & PCI_HT_LCNF_MLWO) >> 4),
	 ht_link_width((lcnf1 & PCI_HT_LCNF_LWI) >> 8),
	 ht_link_width((lcnf1 & PCI_HT_LCNF_LWO) >> 12),
	 FLAG(lcnf1, PCI_HT_LCNF_DFI),
	 FLAG(lcnf1, PCI_HT_LCNF_DFO),
	 FLAG(lcnf1, PCI_HT_LCNF_DFIE),
	 FLAG(lcnf1, PCI_HT_LCNF_DFOE));
  printf("\t\tRevision ID: %u.%02u\n",
	 (rid & PCI_HT_RID_MAJ) >> 5, (rid & PCI_HT_RID_MIN));
  if (rid < 0x23)
    return;
  lfrer0 = get_conf_byte(d, where + PCI_HT_PRI_LFRER0);
  printf("\t\tLink Frequency 0: %s\n", ht_link_freq(lfrer0 & PCI_HT_LFRER_FREQ));
  printf("\t\tLink Error 0: <Prot%c <Ovfl%c <EOC%c CTLTm%c\n",
	 FLAG(lfrer0, PCI_HT_LFRER_PROT),
	 FLAG(lfrer0, PCI_HT_LFRER_OV),
	 FLAG(lfrer0, PCI_HT_LFRER_EOC),
	 FLAG(lfrer0, PCI_HT_LFRER_CTLT));
  lfcap0 = get_conf_byte(d, where + PCI_HT_PRI_LFCAP0);
  printf("\t\tLink Frequency Capability 0: 200MHz%c 300MHz%c 400MHz%c 500MHz%c 600MHz%c 800MHz%c 1.0GHz%c 1.2GHz%c 1.4GHz%c 1.6GHz%c Vend%c\n",
	 FLAG(lfcap0, PCI_HT_LFCAP_200),
	 FLAG(lfcap0, PCI_HT_LFCAP_300),
	 FLAG(lfcap0, PCI_HT_LFCAP_400),
	 FLAG(lfcap0, PCI_HT_LFCAP_500),
	 FLAG(lfcap0, PCI_HT_LFCAP_600),
	 FLAG(lfcap0, PCI_HT_LFCAP_800),
	 FLAG(lfcap0, PCI_HT_LFCAP_1000),
	 FLAG(lfcap0, PCI_HT_LFCAP_1200),
	 FLAG(lfcap0, PCI_HT_LFCAP_1400),
	 FLAG(lfcap0, PCI_HT_LFCAP_1600),
	 FLAG(lfcap0, PCI_HT_LFCAP_VEND));
  ftr = get_conf_byte(d, where + PCI_HT_PRI_FTR);
  printf("\t\tFeature Capability: IsocFC%c LDTSTOP%c CRCTM%c ECTLT%c 64bA%c UIDRD%c\n",
	 FLAG(ftr, PCI_HT_FTR_ISOCFC),
	 FLAG(ftr, PCI_HT_FTR_LDTSTOP),
	 FLAG(ftr, PCI_HT_FTR_CRCTM),
	 FLAG(ftr, PCI_HT_FTR_ECTLT),
	 FLAG(ftr, PCI_HT_FTR_64BA),
	 FLAG(ftr, PCI_HT_FTR_UIDRD));
  lfrer1 = get_conf_byte(d, where + PCI_HT_PRI_LFRER1);
  printf("\t\tLink Frequency 1: %s\n", ht_link_freq(lfrer1 & PCI_HT_LFRER_FREQ));
  printf("\t\tLink Error 1: <Prot%c <Ovfl%c <EOC%c CTLTm%c\n",
	 FLAG(lfrer1, PCI_HT_LFRER_PROT),
	 FLAG(lfrer1, PCI_HT_LFRER_OV),
	 FLAG(lfrer1, PCI_HT_LFRER_EOC),
	 FLAG(lfrer1, PCI_HT_LFRER_CTLT));
  lfcap1 = get_conf_byte(d, where + PCI_HT_PRI_LFCAP1);
  printf("\t\tLink Frequency Capability 1: 200MHz%c 300MHz%c 400MHz%c 500MHz%c 600MHz%c 800MHz%c 1.0GHz%c 1.2GHz%c 1.4GHz%c 1.6GHz%c Vend%c\n",
	 FLAG(lfcap1, PCI_HT_LFCAP_200),
	 FLAG(lfcap1, PCI_HT_LFCAP_300),
	 FLAG(lfcap1, PCI_HT_LFCAP_400),
	 FLAG(lfcap1, PCI_HT_LFCAP_500),
	 FLAG(lfcap1, PCI_HT_LFCAP_600),
	 FLAG(lfcap1, PCI_HT_LFCAP_800),
	 FLAG(lfcap1, PCI_HT_LFCAP_1000),
	 FLAG(lfcap1, PCI_HT_LFCAP_1200),
	 FLAG(lfcap1, PCI_HT_LFCAP_1400),
	 FLAG(lfcap1, PCI_HT_LFCAP_1600),
	 FLAG(lfcap1, PCI_HT_LFCAP_VEND));
  eh = get_conf_word(d, where + PCI_HT_PRI_EH);
  printf("\t\tError Handling: PFlE%c OFlE%c PFE%c OFE%c EOCFE%c RFE%c CRCFE%c SERRFE%c CF%c RE%c PNFE%c ONFE%c EOCNFE%c RNFE%c CRCNFE%c SERRNFE%c\n",
	 FLAG(eh, PCI_HT_EH_PFLE),
	 FLAG(eh, PCI_HT_EH_OFLE),
	 FLAG(eh, PCI_HT_EH_PFE),
	 FLAG(eh, PCI_HT_EH_OFE),
	 FLAG(eh, PCI_HT_EH_EOCFE),
	 FLAG(eh, PCI_HT_EH_RFE),
	 FLAG(eh, PCI_HT_EH_CRCFE),
	 FLAG(eh, PCI_HT_EH_SERRFE),
	 FLAG(eh, PCI_HT_EH_CF),
	 FLAG(eh, PCI_HT_EH_RE),
	 FLAG(eh, PCI_HT_EH_PNFE),
	 FLAG(eh, PCI_HT_EH_ONFE),
	 FLAG(eh, PCI_HT_EH_EOCNFE),
	 FLAG(eh, PCI_HT_EH_RNFE),
	 FLAG(eh, PCI_HT_EH_CRCNFE),
	 FLAG(eh, PCI_HT_EH_SERRNFE));
  mbu = get_conf_byte(d, where + PCI_HT_PRI_MBU);
  mlu = get_conf_byte(d, where + PCI_HT_PRI_MLU);
  printf("\t\tPrefetchable memory behind bridge Upper: %02x-%02x\n", mbu, mlu);
  bn = get_conf_byte(d, where + PCI_HT_PRI_BN);
  printf("\t\tBus Number: %02x\n", bn);
}

static void
cap_ht_sec(struct device *d, int where, int cmd)
{
  u16 lctr, lcnf, ftr, eh;
  u8 rid, lfrer, lfcap, mbu, mlu;
  char *fmt;

  printf("HyperTransport: Host or Secondary Interface\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_HT_SEC_LCTR, PCI_HT_SEC_SIZEOF - PCI_HT_SEC_LCTR))
    return;
  rid = get_conf_byte(d, where + PCI_HT_SEC_RID);
  if (rid < 0x23 && rid > 0x11)
    printf("\t\t!!! Possibly incomplete decoding\n");

  if (rid >= 0x23)
    fmt = "\t\tCommand: WarmRst%c DblEnd%c DevNum=%u ChainSide%c HostHide%c Slave%c <EOCErr%c DUL%c\n";
  else
    fmt = "\t\tCommand: WarmRst%c DblEnd%c\n";
  printf(fmt,
	 FLAG(cmd, PCI_HT_SEC_CMD_WR),
	 FLAG(cmd, PCI_HT_SEC_CMD_DE),
	 (cmd & PCI_HT_SEC_CMD_DN) >> 2,
	 FLAG(cmd, PCI_HT_SEC_CMD_CS),
	 FLAG(cmd, PCI_HT_SEC_CMD_HH),
	 FLAG(cmd, PCI_HT_SEC_CMD_AS),
	 FLAG(cmd, PCI_HT_SEC_CMD_HIECE),
	 FLAG(cmd, PCI_HT_SEC_CMD_DUL));
  lctr = get_conf_word(d, where + PCI_HT_SEC_LCTR);
  if (rid >= 0x23)
    fmt = "\t\tLink Control: CFlE%c CST%c CFE%c <LkFail%c Init%c EOC%c TXO%c <CRCErr=%x IsocEn%c LSEn%c ExtCTL%c 64b%c\n";
  else
    fmt = "\t\tLink Control: CFlE%c CST%c CFE%c <LkFail%c Init%c EOC%c TXO%c <CRCErr=%x\n";
  printf(fmt,
	 FLAG(lctr, PCI_HT_LCTR_CFLE),
	 FLAG(lctr, PCI_HT_LCTR_CST),
	 FLAG(lctr, PCI_HT_LCTR_CFE),
	 FLAG(lctr, PCI_HT_LCTR_LKFAIL),
	 FLAG(lctr, PCI_HT_LCTR_INIT),
	 FLAG(lctr, PCI_HT_LCTR_EOC),
	 FLAG(lctr, PCI_HT_LCTR_TXO),
	 (lctr & PCI_HT_LCTR_CRCERR) >> 8,
	 FLAG(lctr, PCI_HT_LCTR_ISOCEN),
	 FLAG(lctr, PCI_HT_LCTR_LSEN),
	 FLAG(lctr, PCI_HT_LCTR_EXTCTL),
	 FLAG(lctr, PCI_HT_LCTR_64B));
  lcnf = get_conf_word(d, where + PCI_HT_SEC_LCNF);
  if (rid >= 0x23)
    fmt = "\t\tLink Config: MLWI=%1$s DwFcIn%5$c MLWO=%2$s DwFcOut%6$c LWI=%3$s DwFcInEn%7$c LWO=%4$s DwFcOutEn%8$c\n";
  else
    fmt = "\t\tLink Config: MLWI=%s MLWO=%s LWI=%s LWO=%s\n";
  printf(fmt,
	 ht_link_width(lcnf & PCI_HT_LCNF_MLWI),
	 ht_link_width((lcnf & PCI_HT_LCNF_MLWO) >> 4),
	 ht_link_width((lcnf & PCI_HT_LCNF_LWI) >> 8),
	 ht_link_width((lcnf & PCI_HT_LCNF_LWO) >> 12),
	 FLAG(lcnf, PCI_HT_LCNF_DFI),
	 FLAG(lcnf, PCI_HT_LCNF_DFO),
	 FLAG(lcnf, PCI_HT_LCNF_DFIE),
	 FLAG(lcnf, PCI_HT_LCNF_DFOE));
  printf("\t\tRevision ID: %u.%02u\n",
	 (rid & PCI_HT_RID_MAJ) >> 5, (rid & PCI_HT_RID_MIN));
  if (rid < 0x23)
    return;
  lfrer = get_conf_byte(d, where + PCI_HT_SEC_LFRER);
  printf("\t\tLink Frequency: %s\n", ht_link_freq(lfrer & PCI_HT_LFRER_FREQ));
  printf("\t\tLink Error: <Prot%c <Ovfl%c <EOC%c CTLTm%c\n",
	 FLAG(lfrer, PCI_HT_LFRER_PROT),
	 FLAG(lfrer, PCI_HT_LFRER_OV),
	 FLAG(lfrer, PCI_HT_LFRER_EOC),
	 FLAG(lfrer, PCI_HT_LFRER_CTLT));
  lfcap = get_conf_byte(d, where + PCI_HT_SEC_LFCAP);
  printf("\t\tLink Frequency Capability: 200MHz%c 300MHz%c 400MHz%c 500MHz%c 600MHz%c 800MHz%c 1.0GHz%c 1.2GHz%c 1.4GHz%c 1.6GHz%c Vend%c\n",
	 FLAG(lfcap, PCI_HT_LFCAP_200),
	 FLAG(lfcap, PCI_HT_LFCAP_300),
	 FLAG(lfcap, PCI_HT_LFCAP_400),
	 FLAG(lfcap, PCI_HT_LFCAP_500),
	 FLAG(lfcap, PCI_HT_LFCAP_600),
	 FLAG(lfcap, PCI_HT_LFCAP_800),
	 FLAG(lfcap, PCI_HT_LFCAP_1000),
	 FLAG(lfcap, PCI_HT_LFCAP_1200),
	 FLAG(lfcap, PCI_HT_LFCAP_1400),
	 FLAG(lfcap, PCI_HT_LFCAP_1600),
	 FLAG(lfcap, PCI_HT_LFCAP_VEND));
  ftr = get_conf_word(d, where + PCI_HT_SEC_FTR);
  printf("\t\tFeature Capability: IsocFC%c LDTSTOP%c CRCTM%c ECTLT%c 64bA%c UIDRD%c ExtRS%c UCnfE%c\n",
	 FLAG(ftr, PCI_HT_FTR_ISOCFC),
	 FLAG(ftr, PCI_HT_FTR_LDTSTOP),
	 FLAG(ftr, PCI_HT_FTR_CRCTM),
	 FLAG(ftr, PCI_HT_FTR_ECTLT),
	 FLAG(ftr, PCI_HT_FTR_64BA),
	 FLAG(ftr, PCI_HT_FTR_UIDRD),
	 FLAG(ftr, PCI_HT_SEC_FTR_EXTRS),
	 FLAG(ftr, PCI_HT_SEC_FTR_UCNFE));
  if (ftr & PCI_HT_SEC_FTR_EXTRS)
    {
      eh = get_conf_word(d, where + PCI_HT_SEC_EH);
      printf("\t\tError Handling: PFlE%c OFlE%c PFE%c OFE%c EOCFE%c RFE%c CRCFE%c SERRFE%c CF%c RE%c PNFE%c ONFE%c EOCNFE%c RNFE%c CRCNFE%c SERRNFE%c\n",
	     FLAG(eh, PCI_HT_EH_PFLE),
	     FLAG(eh, PCI_HT_EH_OFLE),
	     FLAG(eh, PCI_HT_EH_PFE),
	     FLAG(eh, PCI_HT_EH_OFE),
	     FLAG(eh, PCI_HT_EH_EOCFE),
	     FLAG(eh, PCI_HT_EH_RFE),
	     FLAG(eh, PCI_HT_EH_CRCFE),
	     FLAG(eh, PCI_HT_EH_SERRFE),
	     FLAG(eh, PCI_HT_EH_CF),
	     FLAG(eh, PCI_HT_EH_RE),
	     FLAG(eh, PCI_HT_EH_PNFE),
	     FLAG(eh, PCI_HT_EH_ONFE),
	     FLAG(eh, PCI_HT_EH_EOCNFE),
	     FLAG(eh, PCI_HT_EH_RNFE),
	     FLAG(eh, PCI_HT_EH_CRCNFE),
	     FLAG(eh, PCI_HT_EH_SERRNFE));
      mbu = get_conf_byte(d, where + PCI_HT_SEC_MBU);
      mlu = get_conf_byte(d, where + PCI_HT_SEC_MLU);
      printf("\t\tPrefetchable memory behind bridge Upper: %02x-%02x\n", mbu, mlu);
    }
}

static void
cap_ht(struct device *d, int where, int cmd)
{
  int type;

  switch (cmd & PCI_HT_CMD_TYP_HI)
    {
    case PCI_HT_CMD_TYP_HI_PRI:
      cap_ht_pri(d, where, cmd);
      return;
    case PCI_HT_CMD_TYP_HI_SEC:
      cap_ht_sec(d, where, cmd);
      return;
    }

  type = cmd & PCI_HT_CMD_TYP;
  switch (type)
    {
    case PCI_HT_CMD_TYP_SW:
      printf("HyperTransport: Switch\n");
      break;
    case PCI_HT_CMD_TYP_IDC:
      printf("HyperTransport: Interrupt Discovery and Configuration\n");
      break;
    case PCI_HT_CMD_TYP_RID:
      printf("HyperTransport: Revision ID: %u.%02u\n",
	     (cmd & PCI_HT_RID_MAJ) >> 5, (cmd & PCI_HT_RID_MIN));
      break;
    case PCI_HT_CMD_TYP_UIDC:
      printf("HyperTransport: UnitID Clumping\n");
      break;
    case PCI_HT_CMD_TYP_ECSA:
      printf("HyperTransport: Extended Configuration Space Access\n");
      break;
    case PCI_HT_CMD_TYP_AM:
      printf("HyperTransport: Address Mapping\n");
      break;
    case PCI_HT_CMD_TYP_MSIM:
      printf("HyperTransport: MSI Mapping Enable%c Fixed%c\n",
	     FLAG(cmd, PCI_HT_MSIM_CMD_EN),
	     FLAG(cmd, PCI_HT_MSIM_CMD_FIXD));
      if (verbose >= 2 && !(cmd & PCI_HT_MSIM_CMD_FIXD))
	{
	  u32 offl, offh;
	  if (!config_fetch(d, where + PCI_HT_MSIM_ADDR_LO, 8))
	    break;
	  offl = get_conf_long(d, where + PCI_HT_MSIM_ADDR_LO);
	  offh = get_conf_long(d, where + PCI_HT_MSIM_ADDR_HI);
	  printf("\t\tMapping Address Base: %016llx\n", ((unsigned long long)offh << 32) | (offl & ~0xfffff));
	}
      break;
    case PCI_HT_CMD_TYP_DR:
      printf("HyperTransport: DirectRoute\n");
      break;
    case PCI_HT_CMD_TYP_VCS:
      printf("HyperTransport: VCSet\n");
      break;
    case PCI_HT_CMD_TYP_RM:
      printf("HyperTransport: Retry Mode\n");
      break;
    case PCI_HT_CMD_TYP_X86:
      printf("HyperTransport: X86 (reserved)\n");
      break;
    default:
      printf("HyperTransport: #%02x\n", type >> 11);
    }
}

static void
cap_msi(struct device *d, int where, int cap)
{
  int is64;
  u32 t;
  u16 w;

  printf("Message Signalled Interrupts: Mask%c 64bit%c Queue=%d/%d Enable%c\n",
         FLAG(cap, PCI_MSI_FLAGS_MASK_BIT),
	 FLAG(cap, PCI_MSI_FLAGS_64BIT),
	 (cap & PCI_MSI_FLAGS_QSIZE) >> 4,
	 (cap & PCI_MSI_FLAGS_QMASK) >> 1,
	 FLAG(cap, PCI_MSI_FLAGS_ENABLE));
  if (verbose < 2)
    return;
  is64 = cap & PCI_MSI_FLAGS_64BIT;
  if (!config_fetch(d, where + PCI_MSI_ADDRESS_LO, (is64 ? PCI_MSI_DATA_64 : PCI_MSI_DATA_32) + 2 - PCI_MSI_ADDRESS_LO))
    return;
  printf("\t\tAddress: ");
  if (is64)
    {
      t = get_conf_long(d, where + PCI_MSI_ADDRESS_HI);
      w = get_conf_word(d, where + PCI_MSI_DATA_64);
      printf("%08x", t);
    }
  else
    w = get_conf_word(d, where + PCI_MSI_DATA_32);
  t = get_conf_long(d, where + PCI_MSI_ADDRESS_LO);
  printf("%08x  Data: %04x\n", t, w);
  if (cap & PCI_MSI_FLAGS_MASK_BIT)
    {
      u32 mask, pending;

      if (is64)
	{
	  if (!config_fetch(d, where + PCI_MSI_MASK_BIT_64, 8))
	    return;
	  mask = get_conf_long(d, where + PCI_MSI_MASK_BIT_64);
	  pending = get_conf_long(d, where + PCI_MSI_PENDING_64);
	}
      else
        {
	  if (!config_fetch(d, where + PCI_MSI_MASK_BIT_32, 8))
	    return;
	  mask = get_conf_long(d, where + PCI_MSI_MASK_BIT_32);
	  pending = get_conf_long(d, where + PCI_MSI_PENDING_32);
	}
      printf("\t\tMasking: %08x  Pending: %08x\n", mask, pending);
    }
}

static float power_limit(int value, int scale)
{
  static const float scales[4] = { 1.0, 0.1, 0.01, 0.001 };
  return value * scales[scale];
}

static const char *latency_l0s(int value)
{
  static const char *latencies[] = { "<64ns", "<128ns", "<256ns", "<512ns", "<1us", "<2us", "<4us", "unlimited" };
  return latencies[value];
}

static const char *latency_l1(int value)
{
  static const char *latencies[] = { "<1us", "<2us", "<4us", "<8us", "<16us", "<32us", "<64us", "unlimited" };
  return latencies[value];
}

static void cap_express_dev(struct device *d, int where, int type)
{
  u32 t;
  u16 w;

  t = get_conf_long(d, where + PCI_EXP_DEVCAP);
  printf("\t\tDevCap:\tMaxPayload %d bytes, PhantFunc %d, Latency L0s %s, L1 %s\n",
	128 << (t & PCI_EXP_DEVCAP_PAYLOAD),
	(1 << ((t & PCI_EXP_DEVCAP_PHANTOM) >> 3)) - 1,
	latency_l0s((t & PCI_EXP_DEVCAP_L0S) >> 6),
	latency_l1((t & PCI_EXP_DEVCAP_L1) >> 9));
  printf("\t\t\tExtTag%c", FLAG(t, PCI_EXP_DEVCAP_EXT_TAG));
  if ((type == PCI_EXP_TYPE_ENDPOINT) || (type == PCI_EXP_TYPE_LEG_END) ||
      (type == PCI_EXP_TYPE_UPSTREAM) || (type == PCI_EXP_TYPE_PCI_BRIDGE))
    printf(" AttnBtn%c AttnInd%c PwrInd%c",
	FLAG(t, PCI_EXP_DEVCAP_ATN_BUT),
	FLAG(t, PCI_EXP_DEVCAP_ATN_IND), FLAG(t, PCI_EXP_DEVCAP_PWR_IND));
  printf(" RBE%c FLReset%c",
	FLAG(t, PCI_EXP_DEVCAP_RBE),
	FLAG(t, PCI_EXP_DEVCAP_FLRESET));
  if (type == PCI_EXP_TYPE_UPSTREAM)
    printf("SlotPowerLimit %fW",
	power_limit((t & PCI_EXP_DEVCAP_PWR_VAL) >> 18,
		    (t & PCI_EXP_DEVCAP_PWR_SCL) >> 26));
  printf("\n");

  w = get_conf_word(d, where + PCI_EXP_DEVCTL);
  printf("\t\tDevCtl:\tReport errors: Correctable%c Non-Fatal%c Fatal%c Unsupported%c\n",
	FLAG(w, PCI_EXP_DEVCTL_CERE),
	FLAG(w, PCI_EXP_DEVCTL_NFERE),
	FLAG(w, PCI_EXP_DEVCTL_FERE),
	FLAG(w, PCI_EXP_DEVCTL_URRE));
  printf("\t\t\tRlxdOrd%c ExtTag%c PhantFunc%c AuxPwr%c NoSnoop%c",
	FLAG(w, PCI_EXP_DEVCTL_RELAXED),
	FLAG(w, PCI_EXP_DEVCTL_EXT_TAG),
	FLAG(w, PCI_EXP_DEVCTL_PHANTOM),
	FLAG(w, PCI_EXP_DEVCTL_AUX_PME),
	FLAG(w, PCI_EXP_DEVCTL_NOSNOOP));
  if (type == PCI_EXP_TYPE_PCI_BRIDGE || type == PCI_EXP_TYPE_PCIE_BRIDGE)
    printf(" BrConfRtry%c", FLAG(w, PCI_EXP_DEVCTL_BCRE));
  if (type == PCI_EXP_TYPE_ENDPOINT && (t & PCI_EXP_DEVCAP_FLRESET))
    printf(" FLReset%c", FLAG(w, PCI_EXP_DEVCTL_FLRESET));
  printf("\n\t\t\tMaxPayload %d bytes, MaxReadReq %d bytes\n",
	128 << ((w & PCI_EXP_DEVCTL_PAYLOAD) >> 5),
	128 << ((w & PCI_EXP_DEVCTL_READRQ) >> 12));

  w = get_conf_word(d, where + PCI_EXP_DEVSTA);
  printf("\t\tDevSta:\tCorrErr%c UncorrErr%c FatalErr%c UnsuppReq%c AuxPwr%c TransPend%c\n",
	FLAG(w, PCI_EXP_DEVSTA_CED),
	FLAG(w, PCI_EXP_DEVSTA_NFED),
	FLAG(w, PCI_EXP_DEVSTA_FED),
	FLAG(w, PCI_EXP_DEVSTA_URD),
	FLAG(w, PCI_EXP_DEVSTA_AUXPD),
	FLAG(w, PCI_EXP_DEVSTA_TRPND));

  /* FIXME: Second set of control/status registers is not supported yet. */
}

static char *link_speed(int speed)
{
  switch (speed)
    {
      case 1:
	return "2.5GT/s";
      case 2:
	return "5GT/s";
      default:
	return "unknown";
    }
}

static char *aspm_support(int code)
{
  switch (code)
    {
      case 1:
	return "L0s";
      case 3:
	return "L0s L1";
      default:
	return "unknown";
    }
}

static const char *aspm_enabled(int code)
{
  static const char *desc[] = { "Disabled", "L0s Enabled", "L1 Enabled", "L0s L1 Enabled" };
  return desc[code];
}

static void cap_express_link(struct device *d, int where, int type)
{
  u32 t;
  u16 w;

  t = get_conf_long(d, where + PCI_EXP_LNKCAP);
  printf("\t\tLnkCap:\tPort #%d, Speed %s, Width x%d, ASPM %s, Latency L0 %s, L1 %s\n",
	t >> 24,
	link_speed(t & PCI_EXP_LNKCAP_SPEED), (t & PCI_EXP_LNKCAP_WIDTH) >> 4,
	aspm_support((t & PCI_EXP_LNKCAP_ASPM) >> 10),
	latency_l0s((t & PCI_EXP_LNKCAP_L0S) >> 12),
	latency_l1((t & PCI_EXP_LNKCAP_L1) >> 15));
  printf("\t\t\tClockPM%c Suprise%c LLActRep%c BwNot%c\n",
	FLAG(t, PCI_EXP_LNKCAP_CLOCKPM),
	FLAG(t, PCI_EXP_LNKCAP_SURPRISE),
	FLAG(t, PCI_EXP_LNKCAP_DLLA),
	FLAG(t, PCI_EXP_LNKCAP_LBNC));

  w = get_conf_word(d, where + PCI_EXP_LNKCTL);
  printf("\t\tLnkCtl:\tASPM %s;", aspm_enabled(w & PCI_EXP_LNKCTL_ASPM));
  if ((type == PCI_EXP_TYPE_ROOT_PORT) || (type == PCI_EXP_TYPE_ENDPOINT) ||
      (type == PCI_EXP_TYPE_LEG_END))
    printf(" RCB %d bytes", w & PCI_EXP_LNKCTL_RCB ? 128 : 64);
  printf(" Disabled%c Retrain%c CommClk%c\n\t\t\tExtSynch%c ClockPM%c AutWidDis%c BWInt%c AutBWInt%c\n",
	FLAG(w, PCI_EXP_LNKCTL_DISABLE),
	FLAG(w, PCI_EXP_LNKCTL_RETRAIN),
	FLAG(w, PCI_EXP_LNKCTL_CLOCK),
	FLAG(w, PCI_EXP_LNKCTL_XSYNCH),
	FLAG(w, PCI_EXP_LNKCTL_CLOCKPM),
	FLAG(w, PCI_EXP_LNKCTL_HWAUTWD),
	FLAG(w, PCI_EXP_LNKCTL_BWMIE),
	FLAG(w, PCI_EXP_LNKCTL_AUTBWIE));

  w = get_conf_word(d, where + PCI_EXP_LNKSTA);
  printf("\t\tLnkSta:\tSpeed %s, Width x%d, TrErr%c Train%c SlotClk%c DLActive%c BWMgmt%c ABWMgmt%c\n",
	link_speed(w & PCI_EXP_LNKSTA_SPEED),
	(w & PCI_EXP_LNKSTA_WIDTH) >> 4,
	FLAG(w, PCI_EXP_LNKSTA_TR_ERR),
	FLAG(w, PCI_EXP_LNKSTA_TRAIN),
	FLAG(w, PCI_EXP_LNKSTA_SL_CLK),
	FLAG(w, PCI_EXP_LNKSTA_DL_ACT),
	FLAG(w, PCI_EXP_LNKSTA_BWMGMT),
	FLAG(w, PCI_EXP_LNKSTA_AUTBW));
}

static const char *indicator(int code)
{
  static const char *names[] = { "Unknown", "On", "Blink", "Off" };
  return names[code];
}

static void cap_express_slot(struct device *d, int where)
{
  u32 t;
  u16 w;

  t = get_conf_long(d, where + PCI_EXP_SLTCAP);
  printf("\t\tSltCap:\tAttnBtn%c PwrCtrl%c MRL%c AttnInd%c PwrInd%c HotPlug%c Surpise%c\n",
	FLAG(t, PCI_EXP_SLTCAP_ATNB),
	FLAG(t, PCI_EXP_SLTCAP_PWRC),
	FLAG(t, PCI_EXP_SLTCAP_MRL),
	FLAG(t, PCI_EXP_SLTCAP_ATNI),
	FLAG(t, PCI_EXP_SLTCAP_PWRI),
	FLAG(t, PCI_EXP_SLTCAP_HPC),
	FLAG(t, PCI_EXP_SLTCAP_HPS));
  printf("\t\t\tSlot #%3x, PowerLimit %f; Interlock%c NoCompl%c\n",
	t >> 19,
	power_limit((t & PCI_EXP_SLTCAP_PWR_VAL) >> 7, (t & PCI_EXP_SLTCAP_PWR_SCL) >> 15),
	FLAG(t, PCI_EXP_SLTCAP_INTERLOCK),
	FLAG(t, PCI_EXP_SLTCAP_NOCMDCOMP));

  w = get_conf_word(d, where + PCI_EXP_SLTCTL);
  printf("\t\tSltCtl:\tEnable: AttnBtn%c PwrFlt%c MRL%c PresDet%c CmdCplt%c HPIrq%c LinkChg%c\n",
	FLAG(w, PCI_EXP_SLTCTL_ATNB),
	FLAG(w, PCI_EXP_SLTCTL_PWRF),
	FLAG(w, PCI_EXP_SLTCTL_MRLS),
	FLAG(w, PCI_EXP_SLTCTL_PRSD),
	FLAG(w, PCI_EXP_SLTCTL_CMDC),
	FLAG(w, PCI_EXP_SLTCTL_HPIE),
	FLAG(w, PCI_EXP_SLTCTL_LLCHG));
  printf("\t\t\tControl: AttnInd %s, PwrInd %s, Power%c Interlock%c\n",
	indicator((w & PCI_EXP_SLTCTL_ATNI) >> 6),
	indicator((w & PCI_EXP_SLTCTL_PWRI) >> 8),
	FLAG(w, PCI_EXP_SLTCTL_PWRC),
	FLAG(w, PCI_EXP_SLTCTL_INTERLOCK));

  w = get_conf_word(d, where + PCI_EXP_SLTSTA);
  printf("\t\tSltSta:\tStatus: AttnBtn%c PowerFlt%c MRL%c CmdCplt%c PresDet%c Interlock%c\n",
	FLAG(w, PCI_EXP_SLTSTA_ATNB),
	FLAG(w, PCI_EXP_SLTSTA_PWRF),
	FLAG(w, PCI_EXP_SLTSTA_MRL_ST),
	FLAG(w, PCI_EXP_SLTSTA_CMDC),
	FLAG(w, PCI_EXP_SLTSTA_PRES),
	FLAG(w, PCI_EXP_SLTSTA_INTERLOCK));
  printf("\t\t\tChanged: MRL%c PresDet%c LinkState%c\n",
	FLAG(w, PCI_EXP_SLTSTA_MRLS),
	FLAG(w, PCI_EXP_SLTSTA_PRSD),
	FLAG(w, PCI_EXP_SLTSTA_LLCHG));
}

static void cap_express_root(struct device *d, int where)
{
  u32 w = get_conf_word(d, where + PCI_EXP_RTCTL);
  printf("\t\tRootCtl: ErrCorrectable%c ErrNon-Fatal%c ErrFatal%c PMEIntEna%c CRSVisible%c\n",
	FLAG(w, PCI_EXP_RTCTL_SECEE),
	FLAG(w, PCI_EXP_RTCTL_SENFEE),
	FLAG(w, PCI_EXP_RTCTL_SEFEE),
	FLAG(w, PCI_EXP_RTCTL_PMEIE),
	FLAG(w, PCI_EXP_RTCTL_CRSVIS));

  w = get_conf_word(d, where + PCI_EXP_RTCAP);
  printf("\t\tRootCap: CRSVisible%c\n",
	FLAG(w, PCI_EXP_RTCAP_CRSVIS));

  w = get_conf_word(d, where + PCI_EXP_RTSTA);
  printf("\t\tRootSta: PME ReqID %04x, PMEStatus%c PMEPending%c\n",
	w & PCI_EXP_RTSTA_PME_REQID,
	FLAG(w, PCI_EXP_RTSTA_PME_STATUS),
	FLAG(w, PCI_EXP_RTSTA_PME_PENDING));
}

static void
cap_express(struct device *d, int where, int cap)
{
  int type = (cap & PCI_EXP_FLAGS_TYPE) >> 4;
  int size;
  int slot = 0;

  printf("Express ");
  if (verbose >= 2)
    printf("(v%d) ", cap & PCI_EXP_FLAGS_VERS);
  switch (type)
    {
    case PCI_EXP_TYPE_ENDPOINT:
      printf("Endpoint");
      break;
    case PCI_EXP_TYPE_LEG_END:
      printf("Legacy Endpoint");
      break;
    case PCI_EXP_TYPE_ROOT_PORT:
      slot = cap & PCI_EXP_FLAGS_SLOT;
      printf("Root Port (Slot%c)", FLAG(cap, PCI_EXP_FLAGS_SLOT));
      break;
    case PCI_EXP_TYPE_UPSTREAM:
      printf("Upstream Port");
      break;
    case PCI_EXP_TYPE_DOWNSTREAM:
      slot = cap & PCI_EXP_FLAGS_SLOT;
      printf("Downstream Port (Slot%c)", FLAG(cap, PCI_EXP_FLAGS_SLOT));
      break;
    case PCI_EXP_TYPE_PCI_BRIDGE:
      printf("PCI/PCI-X Bridge");
      break;
    case PCI_EXP_TYPE_PCIE_BRIDGE:
      printf("PCI/PCI-X to PCI-Express Bridge");
      break;
    case PCI_EXP_TYPE_ROOT_INT_EP:
      printf("Root Complex Integrated Endpoint");
      break;
    case PCI_EXP_TYPE_ROOT_EC:
      printf("Root Complex Event Collector");
      break;
    default:
      printf("Unknown type %d", type);
  }
  printf(", MSI %02x\n", (cap & PCI_EXP_FLAGS_IRQ) >> 9);
  if (verbose < 2)
    return;

  size = 16;
  if (slot)
    size = 24;
  if (type == PCI_EXP_TYPE_ROOT_PORT)
    size = 32;
  if (!config_fetch(d, where + PCI_EXP_DEVCAP, size))
    return;

  cap_express_dev(d, where, type);
  cap_express_link(d, where, type);
  if (slot)
    cap_express_slot(d, where);
  if (type == PCI_EXP_TYPE_ROOT_PORT)
    cap_express_root(d, where);
}

static void
cap_msix(struct device *d, int where, int cap)
{
  u32 off;

  printf("MSI-X: Enable%c Mask%c TabSize=%d\n",
	 FLAG(cap, PCI_MSIX_ENABLE),
	 FLAG(cap, PCI_MSIX_MASK),
	 (cap & PCI_MSIX_TABSIZE) + 1);
  if (verbose < 2 || !config_fetch(d, where + PCI_MSIX_TABLE, 8))
    return;

  off = get_conf_long(d, where + PCI_MSIX_TABLE);
  printf("\t\tVector table: BAR=%d offset=%08x\n",
	 off & PCI_MSIX_BIR, off & ~PCI_MSIX_BIR);
  off = get_conf_long(d, where + PCI_MSIX_PBA);
  printf("\t\tPBA: BAR=%d offset=%08x\n",
	 off & PCI_MSIX_BIR, off & ~PCI_MSIX_BIR);
}

static void
cap_slotid(int cap)
{
  int esr = cap & 0xff;
  int chs = cap >> 8;

  printf("Slot ID: %d slots, First%c, chassis %02x\n",
	 esr & PCI_SID_ESR_NSLOTS,
	 FLAG(esr, PCI_SID_ESR_FIC),
	 chs);
}

static void
cap_ssvid(struct device *d, int where)
{
  u16 subsys_v, subsys_d;
  char ssnamebuf[256];

  if (!config_fetch(d, where, 8))
    return;
  subsys_v = get_conf_word(d, where + PCI_SSVID_VENDOR);
  subsys_d = get_conf_word(d, where + PCI_SSVID_DEVICE);
  printf("Subsystem: %s\n",
	   pci_lookup_name(pacc, ssnamebuf, sizeof(ssnamebuf),
			   PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			   d->dev->vendor_id, d->dev->device_id, subsys_v, subsys_d));
}

static void
cap_dsn(struct device *d, int where)
{
  u32 t1, t2;
  if (!config_fetch(d, where + 4, 8))
    return;
  t1 = get_conf_long(d, where + 4);
  t2 = get_conf_long(d, where + 8);
  printf("Device Serial Number %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n",
	t1 & 0xff, (t1 >> 8) & 0xff, (t1 >> 16) & 0xff, t1 >> 24,
	t2 & 0xff, (t2 >> 8) & 0xff, (t2 >> 16) & 0xff, t2 >> 24);
}

static void
cap_debug_port(int cap)
{
  int bar = cap >> 13;
  int pos = cap & 0x1fff;
  printf("Debug port: BAR=%d offset=%04x\n", bar, pos);
}

static void
show_ext_caps(struct device *d)
{
  int where = 0x100;
  char been_there[0x1000];
  memset(been_there, 0, 0x1000);
  do
    {
      u32 header;
      int id;

      if (!config_fetch(d, where, 4))
	break;
      header = get_conf_long(d, where);
      if (!header)
	break;
      id = header & 0xffff;
      printf("\tCapabilities: [%03x] ", where);
      if (been_there[where]++)
	{
	  printf("<chain looped>\n");
	  break;
	}
      switch (id)
	{
	  case PCI_EXT_CAP_ID_AER:
	    printf("Advanced Error Reporting <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_VC:
	    printf("Virtual Channel <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_DSN:
	    cap_dsn(d, where);
	    break;
	  case PCI_EXT_CAP_ID_PB:
	    printf("Power Budgeting <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RCLINK:
	    printf("Root Complex Link <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RCILINK:
	    printf("Root Complex Internal Link <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RCECOLL:
	    printf("Root Complex Event Collector <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_MFVC:
	    printf("Multi-Function Virtual Channel <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RBCB:
	    printf("Root Bridge Control Block <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_VNDR:
	    printf("Vendor Specific Information <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_ACS:
	    printf("Access Controls <?>\n");
	    break;
	  default:
	    printf("#%02x\n", id);
	    break;
	}
      where = header >> 20;
    } while (where);
}

static void
show_caps(struct device *d)
{
  int can_have_ext_caps = 0;

  if (get_conf_word(d, PCI_STATUS) & PCI_STATUS_CAP_LIST)
    {
      int where = get_conf_byte(d, PCI_CAPABILITY_LIST) & ~3;
      byte been_there[256];
      memset(been_there, 0, 256);
      while (where)
	{
	  int id, next, cap;
	  printf("\tCapabilities: ");
	  if (!config_fetch(d, where, 4))
	    {
	      puts("<access denied>");
	      break;
	    }
	  id = get_conf_byte(d, where + PCI_CAP_LIST_ID);
	  next = get_conf_byte(d, where + PCI_CAP_LIST_NEXT) & ~3;
	  cap = get_conf_word(d, where + PCI_CAP_FLAGS);
	  printf("[%02x] ", where);
	  if (been_there[where]++)
	    {
	      printf("<chain looped>\n");
	      break;
	    }
	  if (id == 0xff)
	    {
	      printf("<chain broken>\n");
	      break;
	    }
	  switch (id)
	    {
	    case PCI_CAP_ID_PM:
	      cap_pm(d, where, cap);
	      break;
	    case PCI_CAP_ID_AGP:
	      cap_agp(d, where, cap);
	      break;
	    case PCI_CAP_ID_VPD:
	      printf("Vital Product Data <?>\n");
	      break;
	    case PCI_CAP_ID_SLOTID:
	      cap_slotid(cap);
	      break;
	    case PCI_CAP_ID_MSI:
	      cap_msi(d, where, cap);
	      break;
	    case PCI_CAP_ID_CHSWP:
	      printf("CompactPCI hot-swap <?>\n");
	      break;
	    case PCI_CAP_ID_PCIX:
	      cap_pcix(d, where);
	      can_have_ext_caps = 1;
	      break;
	    case PCI_CAP_ID_HT:
	      cap_ht(d, where, cap);
	      break;
	    case PCI_CAP_ID_VNDR:
	      printf("Vendor Specific Information <?>\n");
	      break;
	    case PCI_CAP_ID_DBG:
	      cap_debug_port(cap);
	      break;
	    case PCI_CAP_ID_CCRC:
	      printf("CompactPCI central resource control <?>\n");
	      break;
	    case PCI_CAP_ID_HOTPLUG:
	      printf("Hot-plug capable\n");
	      break;
	    case PCI_CAP_ID_SSVID:
	      cap_ssvid(d, where);
	      break;
	    case PCI_CAP_ID_AGP3:
	      printf("AGP3 <?>\n");
	      break;
	    case PCI_CAP_ID_SECURE:
	      printf("Secure device <?>\n");
	      break;
	    case PCI_CAP_ID_EXP:
	      cap_express(d, where, cap);
	      can_have_ext_caps = 1;
	      break;
	    case PCI_CAP_ID_MSIX:
	      cap_msix(d, where, cap);
	      break;
	    case PCI_CAP_ID_SATA:
	      printf("SATA HBA <?>\n");
	      break;
	    case PCI_CAP_ID_AF:
	      printf("PCIe advanced features <?>\n");
	      break;
	    default:
	      printf("#%02x [%04x]\n", id, cap);
	    }
	  where = next;
	}
    }
  if (can_have_ext_caps)
    show_ext_caps(d);
}

/*** Verbose output ***/

static void
show_size(pciaddr_t x)
{
  if (!x)
    return;
  printf(" [size=");
  if (x < 1024)
    printf("%d", (int) x);
  else if (x < 1048576)
    printf("%dK", (int)(x / 1024));
  else if (x < 0x80000000)
    printf("%dM", (int)(x / 1048576));
  else
    printf(PCIADDR_T_FMT, x);
  putchar(']');
}

static void
show_bases(struct device *d, int cnt)
{
  struct pci_dev *p = d->dev;
  word cmd = get_conf_word(d, PCI_COMMAND);
  int i;

  for(i=0; i<cnt; i++)
    {
      pciaddr_t pos = p->base_addr[i];
      pciaddr_t len = (p->known_fields & PCI_FILL_SIZES) ? p->size[i] : 0;
      u32 flg = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
      if (flg == 0xffffffff)
	flg = 0;
      if (!pos && !flg && !len)
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
	  pciaddr_t a = pos & PCI_BASE_ADDRESS_IO_MASK;
	  printf("I/O ports at ");
	  if (a)
	    printf(PCIADDR_PORT_FMT, a);
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
	  pciaddr_t a = pos & PCI_ADDR_MEM_MASK;
	  int done = 0;
	  u32 z = 0;

	  printf("Memory at ");
	  if (t == PCI_BASE_ADDRESS_MEM_TYPE_64)
	    {
	      if (i >= cnt - 1)
		{
		  printf("<invalid-64bit-slot>");
		  done = 1;
		}
	      else
		{
		  i++;
		  z = get_conf_long(d, PCI_BASE_ADDRESS_0 + 4*i);
		  if (opt_buscentric)
		    {
		      u32 y = a & 0xffffffff;
		      if (a || z)
			printf("%08x%08x", z, y);
		      else
			printf("<unassigned>");
		      done = 1;
		    }
		}
	    }
	  if (!done)
	    {
	      if (a)
		printf(PCIADDR_T_FMT, a);
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
  u32 flg = get_conf_long(d, reg);
  word cmd = get_conf_word(d, PCI_COMMAND);

  if (!rom && !flg && !len)
    return;
  putchar('\t');
  if ((rom & PCI_ROM_ADDRESS_MASK) && !(flg & PCI_ROM_ADDRESS_MASK))
    {
      printf("[virtual] ");
      flg = rom;
    }
  printf("Expansion ROM at ");
  if (rom & PCI_ROM_ADDRESS_MASK)
    printf(PCIADDR_T_FMT, rom & PCI_ROM_ADDRESS_MASK);
  else if (flg & PCI_ROM_ADDRESS_MASK)
    printf("<ignored>");
  else
    printf("<unassigned>");
  if (!(flg & PCI_ROM_ADDRESS_ENABLE))
    printf(" [disabled]");
  else if (!(cmd & PCI_COMMAND_MEMORY))
    printf(" [disabled by cmd]");
  show_size(len);
  putchar('\n');
}

static void
show_htype0(struct device *d)
{
  show_bases(d, 6);
  show_rom(d, PCI_ROM_ADDRESS);
  show_caps(d);
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
  word sec_stat = get_conf_word(d, PCI_SEC_STATUS);
  word brc = get_conf_word(d, PCI_BRIDGE_CONTROL);
  int verb = verbose > 2;

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
      if (io_base <= io_limit || verb)
	printf("\tI/O behind bridge: %08x-%08x\n", io_base, io_limit+0xfff);
    }

  if (mem_type != (mem_limit & PCI_MEMORY_RANGE_TYPE_MASK) ||
      mem_type)
    printf("\t!!! Unknown memory range types %x/%x\n", mem_base, mem_limit);
  else
    {
      mem_base = (mem_base & PCI_MEMORY_RANGE_MASK) << 16;
      mem_limit = (mem_limit & PCI_MEMORY_RANGE_MASK) << 16;
      if (mem_base <= mem_limit || verb)
	printf("\tMemory behind bridge: %08x-%08x\n", mem_base, mem_limit + 0xfffff);
    }

  if (pref_type != (pref_limit & PCI_PREF_RANGE_TYPE_MASK) ||
      (pref_type != PCI_PREF_RANGE_TYPE_32 && pref_type != PCI_PREF_RANGE_TYPE_64))
    printf("\t!!! Unknown prefetchable memory range types %x/%x\n", pref_base, pref_limit);
  else
    {
      pref_base = (pref_base & PCI_PREF_RANGE_MASK) << 16;
      pref_limit = (pref_limit & PCI_PREF_RANGE_MASK) << 16;
      if (pref_base <= pref_limit || verb)
	{
	  if (pref_type == PCI_PREF_RANGE_TYPE_32)
	    printf("\tPrefetchable memory behind bridge: %08x-%08x\n", pref_base, pref_limit + 0xfffff);
	  else
	    printf("\tPrefetchable memory behind bridge: %08x%08x-%08x%08x\n",
		   get_conf_long(d, PCI_PREF_BASE_UPPER32),
		   pref_base,
		   get_conf_long(d, PCI_PREF_LIMIT_UPPER32),
		   pref_limit + 0xfffff);
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
      printf("\tBridgeCtl: Parity%c SERR%c NoISA%c VGA%c MAbort%c >Reset%c FastB2B%c\n",
	FLAG(brc, PCI_BRIDGE_CTL_PARITY),
	FLAG(brc, PCI_BRIDGE_CTL_SERR),
	FLAG(brc, PCI_BRIDGE_CTL_NO_ISA),
	FLAG(brc, PCI_BRIDGE_CTL_VGA),
	FLAG(brc, PCI_BRIDGE_CTL_MASTER_ABORT),
	FLAG(brc, PCI_BRIDGE_CTL_BUS_RESET),
	FLAG(brc, PCI_BRIDGE_CTL_FAST_BACK));
      printf("\t\tPriDiscTmr%c SecDiscTmr%c DiscTmrStat%c DiscTmrSERREn%c\n",
	FLAG(brc, PCI_BRIDGE_CTL_PRI_DISCARD_TIMER),
	FLAG(brc, PCI_BRIDGE_CTL_SEC_DISCARD_TIMER),
	FLAG(brc, PCI_BRIDGE_CTL_DISCARD_TIMER_STATUS),
	FLAG(brc, PCI_BRIDGE_CTL_DISCARD_TIMER_SERR_EN));
    }

  show_caps(d);
}

static void
show_htype2(struct device *d)
{
  int i;
  word cmd = get_conf_word(d, PCI_COMMAND);
  word brc = get_conf_word(d, PCI_CB_BRIDGE_CONTROL);
  word exca;
  int verb = verbose > 2;

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
      if (limit > base || verb)
	printf("\tMemory window %d: %08x-%08x%s%s\n", i, base, limit,
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
}

static void
show_verbose(struct device *d)
{
  struct pci_dev *p = d->dev;
  word status = get_conf_word(d, PCI_STATUS);
  word cmd = get_conf_word(d, PCI_COMMAND);
  word class = p->device_class;
  byte bist = get_conf_byte(d, PCI_BIST);
  byte htype = get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f;
  byte latency = get_conf_byte(d, PCI_LATENCY_TIMER);
  byte cache_line = get_conf_byte(d, PCI_CACHE_LINE_SIZE);
  byte max_lat, min_gnt;
  byte int_pin = get_conf_byte(d, PCI_INTERRUPT_PIN);
  unsigned int irq = p->irq;
  word subsys_v = 0, subsys_d = 0;
  char ssnamebuf[256];

  show_terse(d);

  switch (htype)
    {
    case PCI_HEADER_TYPE_NORMAL:
      if (class == PCI_CLASS_BRIDGE_PCI)
	printf("\t!!! Invalid class %04x for header type %02x\n", class, htype);
      max_lat = get_conf_byte(d, PCI_MAX_LAT);
      min_gnt = get_conf_byte(d, PCI_MIN_GNT);
      subsys_v = get_conf_word(d, PCI_SUBSYSTEM_VENDOR_ID);
      subsys_d = get_conf_word(d, PCI_SUBSYSTEM_ID);
      break;
    case PCI_HEADER_TYPE_BRIDGE:
      if ((class >> 8) != PCI_BASE_CLASS_BRIDGE)
	printf("\t!!! Invalid class %04x for header type %02x\n", class, htype);
      irq = int_pin = min_gnt = max_lat = 0;
      break;
    case PCI_HEADER_TYPE_CARDBUS:
      if ((class >> 8) != PCI_BASE_CLASS_BRIDGE)
	printf("\t!!! Invalid class %04x for header type %02x\n", class, htype);
      min_gnt = max_lat = 0;
      if (d->config_cached >= 128)
	{
	  subsys_v = get_conf_word(d, PCI_CB_SUBSYSTEM_VENDOR_ID);
	  subsys_d = get_conf_word(d, PCI_CB_SUBSYSTEM_ID);
	}
      break;
    default:
      printf("\t!!! Unknown header type %02x\n", htype);
      return;
    }

  if (subsys_v && subsys_v != 0xffff)
    printf("\tSubsystem: %s\n",
	   pci_lookup_name(pacc, ssnamebuf, sizeof(ssnamebuf),
			   PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			   p->vendor_id, p->device_id, subsys_v, subsys_d));

  if (verbose > 1)
    {
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
      if (int_pin || irq)
	printf("\tInterrupt: pin %c routed to IRQ " PCIIRQ_FMT "\n",
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
	printf("66MHz, ");
      if (status & PCI_STATUS_UDF)
	printf("user-definable features, ");
      printf("%s devsel",
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_SLOW) ? "slow" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_MEDIUM) ? "medium" :
	     ((status & PCI_STATUS_DEVSEL_MASK) == PCI_STATUS_DEVSEL_FAST) ? "fast" : "??");
      if (cmd & PCI_COMMAND_MASTER)
	printf(", latency %d", latency);
      if (irq)
	printf(", IRQ " PCIIRQ_FMT, irq);
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

/*** Machine-readable dumps ***/

static void
show_hex_dump(struct device *d)
{
  unsigned int i, cnt;

  cnt = d->config_cached;
  if (opt_hex >= 3 && config_fetch(d, cnt, 256-cnt))
    {
      cnt = 256;
      if (opt_hex >= 4 && config_fetch(d, 256, 4096-256))
	cnt = 4096;
    }

  for(i=0; i<cnt; i++)
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
      if (d->config_cached >= 128)
	{
	  sv_id = get_conf_word(d, PCI_CB_SUBSYSTEM_VENDOR_ID);
	  sd_id = get_conf_word(d, PCI_CB_SUBSYSTEM_ID);
	}
      break;
    }

  if (verbose)
    {
      printf((opt_machine >= 2) ? "Slot:\t" : "Device:\t");
      show_slot_name(d);
      putchar('\n');
      printf("Class:\t%s\n",
	     pci_lookup_name(pacc, classbuf, sizeof(classbuf), PCI_LOOKUP_CLASS, p->device_class));
      printf("Vendor:\t%s\n",
	     pci_lookup_name(pacc, vendbuf, sizeof(vendbuf), PCI_LOOKUP_VENDOR, p->vendor_id, p->device_id));
      printf("Device:\t%s\n",
	     pci_lookup_name(pacc, devbuf, sizeof(devbuf), PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id));
      if (sv_id && sv_id != 0xffff)
	{
	  printf("SVendor:\t%s\n",
		 pci_lookup_name(pacc, svbuf, sizeof(svbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR, sv_id));
	  printf("SDevice:\t%s\n",
		 pci_lookup_name(pacc, sdbuf, sizeof(sdbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id, sv_id, sd_id));
	}
      if (c = get_conf_byte(d, PCI_REVISION_ID))
	printf("Rev:\t%02x\n", c);
      if (c = get_conf_byte(d, PCI_CLASS_PROG))
	printf("ProgIf:\t%02x\n", c);
    }
  else
    {
      show_slot_name(d);
      print_shell_escaped(pci_lookup_name(pacc, classbuf, sizeof(classbuf), PCI_LOOKUP_CLASS, p->device_class));
      print_shell_escaped(pci_lookup_name(pacc, vendbuf, sizeof(vendbuf), PCI_LOOKUP_VENDOR, p->vendor_id, p->device_id));
      print_shell_escaped(pci_lookup_name(pacc, devbuf, sizeof(devbuf), PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id));
      if (c = get_conf_byte(d, PCI_REVISION_ID))
	printf(" -r%02x", c);
      if (c = get_conf_byte(d, PCI_CLASS_PROG))
	printf(" -p%02x", c);
      if (sv_id && sv_id != 0xffff)
	{
	  print_shell_escaped(pci_lookup_name(pacc, svbuf, sizeof(svbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_VENDOR, sv_id));
	  print_shell_escaped(pci_lookup_name(pacc, sdbuf, sizeof(sdbuf), PCI_LOOKUP_SUBSYSTEM | PCI_LOOKUP_DEVICE, p->vendor_id, p->device_id, sv_id, sd_id));
	}
      else
	printf(" \"\" \"\"");
      putchar('\n');
    }
}

/*** Main show function ***/

static void
show_device(struct device *d)
{
  if (opt_machine)
    show_machine(d);
  else if (verbose)
    show_verbose(d);
  else
    show_terse(d);
  if (opt_hex)
    show_hex_dump(d);
  if (verbose || opt_hex)
    putchar('\n');
}

static void
show(void)
{
  struct device *d;

  for(d=first_dev; d; d=d->next)
    show_device(d);
}

/*** Tree output ***/

struct bridge {
  struct bridge *chain;			/* Single-linked list of bridges */
  struct bridge *next, *child;		/* Tree of bridges */
  struct bus *first_bus;		/* List of buses connected to this bridge */
  unsigned int domain;
  unsigned int primary, secondary, subordinate;	/* Bus numbers */
  struct device *br_dev;
};

struct bus {
  unsigned int domain;
  unsigned int number;
  struct bus *sibling;
  struct device *first_dev, **last_dev;
};

static struct bridge host_bridge = { NULL, NULL, NULL, NULL, 0, ~0, 0, ~0, NULL };

static struct bus *
find_bus(struct bridge *b, unsigned int domain, unsigned int n)
{
  struct bus *bus;

  for(bus=b->first_bus; bus; bus=bus->sibling)
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
      for(c=b->child; c; c=c->next)
	if (c->domain == p->domain && c->secondary <= p->bus && p->bus <= c->subordinate)
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
      word class = d->dev->device_class;
      byte ht = get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f;
      if (class == PCI_CLASS_BRIDGE_PCI &&
	  (ht == PCI_HEADER_TYPE_BRIDGE || ht == PCI_HEADER_TYPE_CARDBUS))
	{
	  b = xmalloc(sizeof(struct bridge));
	  b->domain = d->dev->domain;
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
	}
    }
  *last_br = NULL;

  /* Create a bridge tree */

  for(b=&host_bridge; b; b=b->chain)
    {
      struct bridge *c, *best;
      best = NULL;
      for(c=&host_bridge; c; c=c->chain)
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

  for(b=&host_bridge; b; b=b->chain)
    if (!find_bus(b, b->domain, b->secondary))
      new_bus(b, b->domain, b->secondary);

  /* Create bus structs and link devices */

  for(d=first_dev; d;)
    {
      d2 = d->next;
      insert_dev(d, &host_bridge);
      d = d2;
    }
}

static void
print_it(char *line, char *p)
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

static void show_tree_bridge(struct bridge *, char *, char *);

static void
show_tree_dev(struct device *d, char *line, char *p)
{
  struct pci_dev *q = d->dev;
  struct bridge *b;
  char namebuf[256];

  p += sprintf(p, "%02x.%x", q->dev, q->func);
  for(b=&host_bridge; b; b=b->chain)
    if (b->br_dev == d)
      {
	if (b->secondary == b->subordinate)
	  p += sprintf(p, "-[%04x:%02x]-", b->domain, b->secondary);
	else
	  p += sprintf(p, "-[%04x:%02x-%02x]-", b->domain, b->secondary, b->subordinate);
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
show_tree_bus(struct bus *b, char *line, char *p)
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

static void
show_forest(void)
{
  char line[256];

  grow_tree();
  show_tree_bridge(&host_bridge, line, line);
}

/*** Bus mapping mode ***/

struct bus_bridge {
  struct bus_bridge *next;
  byte this, dev, func, first, last, bug;
};

struct bus_info {
  byte exists;
  byte guestbook;
  struct bus_bridge *bridges, *via;
};

static struct bus_info *bus_info;

static void
map_bridge(struct bus_info *bi, struct device *d, int np, int ns, int nl)
{
  struct bus_bridge *b = xmalloc(sizeof(struct bus_bridge));
  struct pci_dev *p = d->dev;

  b->next = bi->bridges;
  bi->bridges = b;
  b->this = get_conf_byte(d, np);
  b->dev = p->dev;
  b->func = p->func;
  b->first = get_conf_byte(d, ns);
  b->last = get_conf_byte(d, nl);
  printf("## %02x.%02x:%d is a bridge from %02x to %02x-%02x\n",
	 p->bus, p->dev, p->func, b->this, b->first, b->last);
  if (b->this != p->bus)
    printf("!!! Bridge points to invalid primary bus.\n");
  if (b->first > b->last)
    {
      printf("!!! Bridge points to invalid bus range.\n");
      b->last = b->first;
    }
}

static void
do_map_bus(int bus)
{
  int dev, func;
  int verbose = pacc->debugging;
  struct bus_info *bi = bus_info + bus;
  struct device *d;

  if (verbose)
    printf("Mapping bus %02x\n", bus);
  for(dev = 0; dev < 32; dev++)
    if (filter.slot < 0 || filter.slot == dev)
      {
	int func_limit = 1;
	for(func = 0; func < func_limit; func++)
	  if (filter.func < 0 || filter.func == func)
	    {
	      /* XXX: Bus mapping supports only domain 0 */
	      struct pci_dev *p = pci_get_dev(pacc, 0, bus, dev, func);
	      u16 vendor = pci_read_word(p, PCI_VENDOR_ID);
	      if (vendor && vendor != 0xffff)
		{
		  if (!func && (pci_read_byte(p, PCI_HEADER_TYPE) & 0x80))
		    func_limit = 8;
		  if (verbose)
		    printf("Discovered device %02x:%02x.%d\n", bus, dev, func);
		  bi->exists = 1;
		  if (d = scan_device(p))
		    {
		      show_device(d);
		      switch (get_conf_byte(d, PCI_HEADER_TYPE) & 0x7f)
			{
			case PCI_HEADER_TYPE_BRIDGE:
			  map_bridge(bi, d, PCI_PRIMARY_BUS, PCI_SECONDARY_BUS, PCI_SUBORDINATE_BUS);
			  break;
			case PCI_HEADER_TYPE_CARDBUS:
			  map_bridge(bi, d, PCI_CB_PRIMARY_BUS, PCI_CB_CARD_BUS, PCI_CB_SUBORDINATE_BUS);
			  break;
			}
		      free(d);
		    }
		  else if (verbose)
		    printf("But it was filtered out.\n");
		}
	      pci_free_dev(p);
	    }
      }
}

static void
do_map_bridges(int bus, int min, int max)
{
  struct bus_info *bi = bus_info + bus;
  struct bus_bridge *b;

  bi->guestbook = 1;
  for(b=bi->bridges; b; b=b->next)
    {
      if (bus_info[b->first].guestbook)
	b->bug = 1;
      else if (b->first < min || b->last > max)
	b->bug = 2;
      else
	{
	  bus_info[b->first].via = b;
	  do_map_bridges(b->first, b->first, b->last);
	}
    }
}

static void
map_bridges(void)
{
  int i;

  printf("\nSummary of buses:\n\n");
  for(i=0; i<256; i++)
    if (bus_info[i].exists && !bus_info[i].guestbook)
      do_map_bridges(i, 0, 255);
  for(i=0; i<256; i++)
    {
      struct bus_info *bi = bus_info + i;
      struct bus_bridge *b = bi->via;

      if (bi->exists)
	{
	  printf("%02x: ", i);
	  if (b)
	    printf("Entered via %02x:%02x.%d\n", b->this, b->dev, b->func);
	  else if (!i)
	    printf("Primary host bus\n");
	  else
	    printf("Secondary host bus (?)\n");
	}
      for(b=bi->bridges; b; b=b->next)
	{
	  printf("\t%02x.%d Bridge to %02x-%02x", b->dev, b->func, b->first, b->last);
	  switch (b->bug)
	    {
	    case 1:
	      printf(" <overlap bug>");
	      break;
	    case 2:
	      printf(" <crossing bug>");
	      break;
	    }
	  putchar('\n');
	}
    }
}

static void
map_the_bus(void)
{
  if (pacc->method == PCI_ACCESS_PROC_BUS_PCI ||
      pacc->method == PCI_ACCESS_DUMP)
    printf("WARNING: Bus mapping can be reliable only with direct hardware access enabled.\n\n");
  bus_info = xmalloc(sizeof(struct bus_info) * 256);
  memset(bus_info, 0, sizeof(struct bus_info) * 256);
  if (filter.bus >= 0)
    do_map_bus(filter.bus);
  else
    {
      int bus;
      for(bus=0; bus<256; bus++)
	do_map_bus(bus);
    }
  map_bridges();
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
	opt_buscentric = 1;
	break;
      case 's':
	if (msg = pci_filter_parse_slot(&filter, optarg))
	  die("-s: %s", msg);
	break;
      case 'd':
	if (msg = pci_filter_parse_id(&filter, optarg))
	  die("-d: %s", msg);
	break;
      case 'x':
	opt_hex++;
	break;
      case 't':
	opt_tree++;
	break;
      case 'i':
        pci_set_name_list_path(pacc, optarg, 0);
	break;
      case 'm':
	opt_machine++;
	break;
      case 'M':
	opt_map_mode++;
	break;
      case 'D':
	opt_domains = 2;
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
  if (opt_map_mode)
    map_the_bus();
  else
    {
      scan_devices();
      sort_them();
      if (opt_tree)
	show_forest();
      else
	show();
    }
  pci_cleanup(pacc);

  return (seen_errors ? 2 : 0);
}
