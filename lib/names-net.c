/*
 *	The PCI Library -- Resolving ID's via DNS
 *
 *	Copyright (c) 2007--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "internal.h"
#include "names.h"

#ifdef PCI_USE_DNS

/*
 * Our definition of BYTE_ORDER confuses arpa/nameser_compat.h on
 * Solaris so we must undef it before including arpa/nameser.h.
 */
#ifdef PCI_OS_SUNOS
#undef BYTE_ORDER
#endif

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>

/*
 * Unfortunately, there are no portable functions for DNS RR parsing,
 * so we will do the bit shuffling with our own bare hands.
 */

#define GET16(x) do { if (p+2 > end) goto err; x = (p[0] << 8) | p[1]; p += 2; } while (0)
#define GET32(x) do { if (p+4 > end) goto err; x = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; p += 4; } while (0)

enum dns_section {
  DNS_SEC_QUESTION,
  DNS_SEC_ANSWER,
  DNS_SEC_AUTHORITY,
  DNS_SEC_ADDITIONAL,
  DNS_NUM_SECTIONS
};

struct dns_state {
  u16 counts[DNS_NUM_SECTIONS];
  byte *sections[DNS_NUM_SECTIONS+1];
  byte *sec_ptr, *sec_end;

  /* Result of dns_parse_rr(): */
  u16 rr_type;
  u16 rr_class;
  u32 rr_ttl;
  u16 rr_len;
  byte *rr_data;
};

static byte *
dns_skip_name(byte *p, byte *end)
{
  while (p < end)
    {
      unsigned int x = *p++;
      if (!x)
	return p;
      switch (x & 0xc0)
	{
	case 0:		/* Uncompressed: x = length */
	  p += x;
	  break;
	case 0xc0:	/* Indirection: 1 byte more for offset */
	  p++;
	  return (p < end) ? p : NULL;
	default:	/* RFU */
	  return NULL;
	}
    }
  return NULL;
}

static int
dns_parse_packet(struct dns_state *s, byte *p, unsigned int plen)
{
  byte *end = p + plen;
  unsigned int i, j, len;
  unsigned int UNUSED x;

#if 0
  /* Dump the packet */
  for (i=0; i<plen; i++)
    {
      if (!(i%16)) printf("%04x:", i);
      printf(" %02x", p[i]);
      if ((i%16)==15 || i==plen-1) putchar('\n');
    }
#endif

  GET32(x);				/* ID and flags are ignored */
  for (i=0; i<DNS_NUM_SECTIONS; i++)
    GET16(s->counts[i]);
  for (i=0; i<DNS_NUM_SECTIONS; i++)
    {
      s->sections[i] = p;
      for (j=0; j < s->counts[i]; j++)
	{
	  p = dns_skip_name(p, end);	/* Name */
	  if (!p)
	    goto err;
	  GET32(x);			/* Type and class */
	  if (i != DNS_SEC_QUESTION)
	    {
	      GET32(x);			/* TTL */
	      GET16(len);		/* Length of data */
	      p += len;
	      if (p > end)
		goto err;
	    }
	}
    }
  s->sections[i] = p;
  return 0;

err:
  return -1;
}

static void
dns_init_section(struct dns_state *s, int i)
{
  s->sec_ptr = s->sections[i];
  s->sec_end = s->sections[i+1];
}

static int
dns_parse_rr(struct dns_state *s)
{
  byte *p = s->sec_ptr;
  byte *end = s->sec_end;

  if (p == end)
    return 0;
  p = dns_skip_name(p, end);
  if (!p)
    goto err;
  GET16(s->rr_type);
  GET16(s->rr_class);
  GET32(s->rr_ttl);
  GET16(s->rr_len);
  s->rr_data = p;
  s->sec_ptr = p + s->rr_len;
  return 1;

err:
  return -1;
}

char
*pci_id_net_lookup(struct pci_access *a, int cat, int id1, int id2, int id3, int id4)
{
  static int resolver_inited;
  char name[256], dnsname[256], txt[256], *domain;
  byte answer[4096];
  const byte *data;
  int res, j, dlen;
  struct dns_state ds;

  domain = pci_get_param(a, "net.domain");
  if (!domain || !domain[0])
    return NULL;

  switch (cat)
    {
    case ID_VENDOR:
      sprintf(name, "%04x", id1);
      break;
    case ID_DEVICE:
      sprintf(name, "%04x.%04x", id2, id1);
      break;
    case ID_SUBSYSTEM:
      sprintf(name, "%04x.%04x.%04x.%04x", id4, id3, id2, id1);
      break;
    case ID_GEN_SUBSYSTEM:
      sprintf(name, "%04x.%04x.s", id2, id1);
      break;
    case ID_CLASS:
      sprintf(name, "%02x.c", id1);
      break;
    case ID_SUBCLASS:
      sprintf(name, "%02x.%02x.c", id2, id1);
      break;
    case ID_PROGIF:
      sprintf(name, "%02x.%02x.%02x.c", id3, id2, id1);
      break;
    default:
      return NULL;
    }
  sprintf(dnsname, "%.100s.%.100s", name, domain);

  a->debug("Resolving %s\n", dnsname);
  if (!resolver_inited)
    {
      resolver_inited = 1;
      res_init();
    }
  res = res_query(dnsname, ns_c_in, ns_t_txt, answer, sizeof(answer));
  if (res < 0)
    {
      a->debug("\tfailed, h_errno=%d\n", h_errno);
      return NULL;
    }
  if (dns_parse_packet(&ds, answer, res) < 0)
    {
      a->debug("\tMalformed DNS packet received\n");
      return NULL;
    }
  dns_init_section(&ds, DNS_SEC_ANSWER);
  while (dns_parse_rr(&ds) > 0)
    {
      if (ds.rr_class != ns_c_in || ds.rr_type != ns_t_txt)
	{
	  a->debug("\tUnexpected RR in answer: class %d, type %d\n", ds.rr_class, ds.rr_type);
	  continue;
	}
      data = ds.rr_data;
      dlen = ds.rr_len;
      j = 0;
      while (j < dlen && j+1+data[j] <= dlen)
	{
	  memcpy(txt, &data[j+1], data[j]);
	  txt[data[j]] = 0;
	  j += 1+data[j];
	  a->debug("\t\"%s\"\n", txt);
	  if (txt[0] == 'i' && txt[1] == '=')
	    return strdup(txt+2);
	}
    }

  return NULL;
}

#else

char *pci_id_net_lookup(struct pci_access *a UNUSED, int cat UNUSED, int id1 UNUSED, int id2 UNUSED, int id3 UNUSED, int id4 UNUSED)
{
  return NULL;
}

#endif
