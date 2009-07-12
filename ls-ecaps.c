/*
 *	The PCI Utilities -- Show Extended Capabilities
 *
 *	Copyright (c) 1997--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>

#include "lspci.h"

static void
cap_dsn(struct device *d, int where)
{
  u32 t1, t2;
  if (!config_fetch(d, where + 4, 8))
    return;
  t1 = get_conf_long(d, where + 4);
  t2 = get_conf_long(d, where + 8);
  printf("Device Serial Number %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n",
	t2 >> 24, (t2 >> 16) & 0xff, (t2 >> 8) & 0xff, t2 & 0xff,
	t1 >> 24, (t1 >> 16) & 0xff, (t1 >> 8) & 0xff, t1 & 0xff);
}

static void
cap_aer(struct device *d, int where)
{
  u32 l;

  printf("Advanced Error Reporting\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_ERR_UNCOR_STATUS, 24))
    return;

  l = get_conf_long(d, where + PCI_ERR_UNCOR_STATUS);
  printf("\t\tUESta:\tDLP%c SDES%c TLP%c FCP%c CmpltTO%c CmpltAbrt%c UnxCmplt%c RxOF%c "
	"MalfTLP%c ECRC%c UnsupReq%c ACSViol%c\n",
	FLAG(l, PCI_ERR_UNC_DLP), FLAG(l, PCI_ERR_UNC_SDES), FLAG(l, PCI_ERR_UNC_POISON_TLP),
	FLAG(l, PCI_ERR_UNC_FCP), FLAG(l, PCI_ERR_UNC_COMP_TIME), FLAG(l, PCI_ERR_UNC_COMP_ABORT),
	FLAG(l, PCI_ERR_UNC_UNX_COMP), FLAG(l, PCI_ERR_UNC_RX_OVER), FLAG(l, PCI_ERR_UNC_MALF_TLP),
	FLAG(l, PCI_ERR_UNC_ECRC), FLAG(l, PCI_ERR_UNC_UNSUP), FLAG(l, PCI_ERR_UNC_ACS_VIOL));
  l = get_conf_long(d, where + PCI_ERR_UNCOR_MASK);
  printf("\t\tUEMsk:\tDLP%c SDES%c TLP%c FCP%c CmpltTO%c CmpltAbrt%c UnxCmplt%c RxOF%c "
	"MalfTLP%c ECRC%c UnsupReq%c ACSViol%c\n",
	FLAG(l, PCI_ERR_UNC_DLP), FLAG(l, PCI_ERR_UNC_SDES), FLAG(l, PCI_ERR_UNC_POISON_TLP),
	FLAG(l, PCI_ERR_UNC_FCP), FLAG(l, PCI_ERR_UNC_COMP_TIME), FLAG(l, PCI_ERR_UNC_COMP_ABORT),
	FLAG(l, PCI_ERR_UNC_UNX_COMP), FLAG(l, PCI_ERR_UNC_RX_OVER), FLAG(l, PCI_ERR_UNC_MALF_TLP),
	FLAG(l, PCI_ERR_UNC_ECRC), FLAG(l, PCI_ERR_UNC_UNSUP), FLAG(l, PCI_ERR_UNC_ACS_VIOL));
  l = get_conf_long(d, where + PCI_ERR_UNCOR_SEVER);
  printf("\t\tUESvrt:\tDLP%c SDES%c TLP%c FCP%c CmpltTO%c CmpltAbrt%c UnxCmplt%c RxOF%c "
	"MalfTLP%c ECRC%c UnsupReq%c ACSViol%c\n",
	FLAG(l, PCI_ERR_UNC_DLP), FLAG(l, PCI_ERR_UNC_SDES), FLAG(l, PCI_ERR_UNC_POISON_TLP),
	FLAG(l, PCI_ERR_UNC_FCP), FLAG(l, PCI_ERR_UNC_COMP_TIME), FLAG(l, PCI_ERR_UNC_COMP_ABORT),
	FLAG(l, PCI_ERR_UNC_UNX_COMP), FLAG(l, PCI_ERR_UNC_RX_OVER), FLAG(l, PCI_ERR_UNC_MALF_TLP),
	FLAG(l, PCI_ERR_UNC_ECRC), FLAG(l, PCI_ERR_UNC_UNSUP), FLAG(l, PCI_ERR_UNC_ACS_VIOL));
  l = get_conf_long(d, where + PCI_ERR_COR_STATUS);
  printf("\t\tCESta:\tRxErr%c BadTLP%c BadDLLP%c Rollover%c Timeout%c NonFatalErr%c\n",
	FLAG(l, PCI_ERR_COR_RCVR), FLAG(l, PCI_ERR_COR_BAD_TLP), FLAG(l, PCI_ERR_COR_BAD_DLLP),
	FLAG(l, PCI_ERR_COR_REP_ROLL), FLAG(l, PCI_ERR_COR_REP_TIMER), FLAG(l, PCI_ERR_COR_REP_ANFE));
  l = get_conf_long(d, where + PCI_ERR_COR_MASK);
  printf("\t\tCEMsk:\tRxErr%c BadTLP%c BadDLLP%c Rollover%c Timeout%c NonFatalErr%c\n",
	FLAG(l, PCI_ERR_COR_RCVR), FLAG(l, PCI_ERR_COR_BAD_TLP), FLAG(l, PCI_ERR_COR_BAD_DLLP),
	FLAG(l, PCI_ERR_COR_REP_ROLL), FLAG(l, PCI_ERR_COR_REP_TIMER), FLAG(l, PCI_ERR_COR_REP_ANFE));
  l = get_conf_long(d, where + PCI_ERR_CAP);
  printf("\t\tAERCap:\tFirst Error Pointer: %02x, GenCap%c CGenEn%c ChkCap%c ChkEn%c\n",
	PCI_ERR_CAP_FEP(l), FLAG(l, PCI_ERR_CAP_ECRC_GENC), FLAG(l, PCI_ERR_CAP_ECRC_GENE),
	FLAG(l, PCI_ERR_CAP_ECRC_CHKC), FLAG(l, PCI_ERR_CAP_ECRC_CHKE));

}

static void
cap_acs(struct device *d, int where)
{
  u16 w;

  printf("Access Control Services\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_ACS_CAP, 4))
    return;

  w = get_conf_word(d, where + PCI_ACS_CAP);
  printf("\t\tACSCap:\tSrcValid%c TransBlk%c ReqRedir%c CmpltRedir%c UpstreamFwd%c EgressCtrl%c "
	"DirectTrans%c\n",
	FLAG(w, PCI_ACS_CAP_VALID), FLAG(w, PCI_ACS_CAP_BLOCK), FLAG(w, PCI_ACS_CAP_REQ_RED),
	FLAG(w, PCI_ACS_CAP_CMPLT_RED), FLAG(w, PCI_ACS_CAP_FORWARD), FLAG(w, PCI_ACS_CAP_EGRESS),
	FLAG(w, PCI_ACS_CAP_TRANS));
  w = get_conf_word(d, where + PCI_ACS_CTRL);
  printf("\t\tACSCtl:\tSrcValid%c TransBlk%c ReqRedir%c CmpltRedir%c UpstreamFwd%c EgressCtrl%c "
	"DirectTrans%c\n",
	FLAG(w, PCI_ACS_CTRL_VALID), FLAG(w, PCI_ACS_CTRL_BLOCK), FLAG(w, PCI_ACS_CTRL_REQ_RED),
	FLAG(w, PCI_ACS_CTRL_CMPLT_RED), FLAG(w, PCI_ACS_CTRL_FORWARD), FLAG(w, PCI_ACS_CTRL_EGRESS),
	FLAG(w, PCI_ACS_CTRL_TRANS));
}

static void
cap_ari(struct device *d, int where)
{
  u16 w;

  printf("Alternative Routing-ID Interpretation (ARI)\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_ARI_CAP, 4))
    return;

  w = get_conf_word(d, where + PCI_ARI_CAP);
  printf("\t\tARICap:\tMFVC%c ACS%c, Next Function: %d\n",
	FLAG(w, PCI_ARI_CAP_MFVC), FLAG(w, PCI_ARI_CAP_ACS),
	PCI_ARI_CAP_NFN(w));
  w = get_conf_word(d, where + PCI_ARI_CTRL);
  printf("\t\tARICtl:\tMFVC%c ACS%c, Function Group: %d\n",
	FLAG(w, PCI_ARI_CTRL_MFVC), FLAG(w, PCI_ARI_CTRL_ACS),
	PCI_ARI_CTRL_FG(w));
}

static void
cap_ats(struct device *d, int where)
{
  u16 w;

  printf("Address Translation Service (ATS)\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_ATS_CAP, 4))
    return;

  w = get_conf_word(d, where + PCI_ATS_CAP);
  printf("\t\tATSCap:\tInvalidate Queue Depth: %02x\n", PCI_ATS_CAP_IQD(w));
  w = get_conf_word(d, where + PCI_ATS_CTRL);
  printf("\t\tATSCtl:\tEnable%c, Smallest Translation Unit: %02x\n",
	FLAG(w, PCI_ATS_CTRL_ENABLE), PCI_ATS_CTRL_STU(w));
}

static void
cap_sriov(struct device *d, int where)
{
  u16 b;
  u16 w;
  u32 l;
  int i;

  printf("Single Root I/O Virtualization (SR-IOV)\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_IOV_CAP, 0x3c))
    return;

  l = get_conf_long(d, where + PCI_IOV_CAP);
  printf("\t\tIOVCap:\tMigration%c, Interrupt Message Number: %03x\n",
	FLAG(l, PCI_IOV_CAP_VFM), PCI_IOV_CAP_IMN(l));
  w = get_conf_word(d, where + PCI_IOV_CTRL);
  printf("\t\tIOVCtl:\tEnable%c Migration%c Interrupt%c MSE%c ARIHierarchy%c\n",
	FLAG(w, PCI_IOV_CTRL_VFE), FLAG(w, PCI_IOV_CTRL_VFME),
	FLAG(w, PCI_IOV_CTRL_VFMIE), FLAG(w, PCI_IOV_CTRL_MSE),
	FLAG(w, PCI_IOV_CTRL_ARI));
  w = get_conf_word(d, where + PCI_IOV_STATUS);
  printf("\t\tIOVSta:\tMigration%c\n", FLAG(w, PCI_IOV_STATUS_MS));
  w = get_conf_word(d, where + PCI_IOV_INITIALVF);
  printf("\t\tInitial VFs: %d, ", w);
  w = get_conf_word(d, where + PCI_IOV_TOTALVF);
  printf("Total VFs: %d, ", w);
  w = get_conf_word(d, where + PCI_IOV_NUMVF);
  printf("Number of VFs: %d, ", w);
  b = get_conf_byte(d, where + PCI_IOV_FDL);
  printf("Function Dependency Link: %02x\n", b);
  w = get_conf_word(d, where + PCI_IOV_OFFSET);
  printf("\t\tVF offset: %d, ", w);
  w = get_conf_word(d, where + PCI_IOV_STRIDE);
  printf("stride: %d, ", w);
  w = get_conf_word(d, where + PCI_IOV_DID);
  printf("Device ID: %04x\n", w);
  l = get_conf_long(d, where + PCI_IOV_SUPPS);
  printf("\t\tSupported Page Size: %08x, ", l);
  l = get_conf_long(d, where + PCI_IOV_SYSPS);
  printf("System Page Size: %08x\n", l);

  for (i=0; i < PCI_IOV_NUM_BAR; i++)
    {
      pciaddr_t addr;
      int type;
      u32 h;
      l = get_conf_long(d, where + PCI_IOV_BAR_BASE + 4*i);
      if (l == 0xffffffff)
	l = 0;
      if (!l)
	continue;
      printf("\t\tRegion %d: Memory at ", i);
      addr = l & PCI_ADDR_MEM_MASK;
      type = l & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
      if (type == PCI_BASE_ADDRESS_MEM_TYPE_64)
	{
	  i++;
	  h = get_conf_long(d, where + PCI_IOV_BAR_BASE + (i*4));
	  addr |= (pciaddr_t)h<<32;
	}
      printf(PCIADDR_T_FMT, addr);
      printf(" (%s-bit, %sprefetchable)\n",
	(type == PCI_BASE_ADDRESS_MEM_TYPE_32) ? "32" : "64",
	(l & PCI_BASE_ADDRESS_MEM_PREFETCH) ? "" : "non-");
    }

  l = get_conf_long(d, where + PCI_IOV_MSAO);
  printf("\t\tVF Migration: offset: %08x, BIR: %x\n", PCI_IOV_MSA_OFFSET(l),
	PCI_IOV_MSA_BIR(l));
}

void
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
	    cap_aer(d, where);
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
	    cap_acs(d, where);
	    break;
	  case PCI_EXT_CAP_ID_ARI:
	    cap_ari(d, where);
	    break;
	  case PCI_EXT_CAP_ID_ATS:
	    cap_ats(d, where);
	    break;
	  case PCI_EXT_CAP_ID_SRIOV:
	    cap_sriov(d, where);
	    break;
	  default:
	    printf("#%02x\n", id);
	    break;
	}
      where = header >> 20;
    } while (where);
}
