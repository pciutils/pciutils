/*
 *	The PCI Utilities -- Show Extended Capabilities
 *
 *	Copyright (c) 1997--2020 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>

#include "lspci.h"

static void
cap_tph(struct device *d, int where)
{
  u32 tph_cap;
  printf("Transaction Processing Hints\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_TPH_CAPABILITIES, 4))
    return;

  tph_cap = get_conf_long(d, where + PCI_TPH_CAPABILITIES);

  if (tph_cap & PCI_TPH_INTVEC_SUP)
    printf("\t\tInterrupt vector mode supported\n");
  if (tph_cap & PCI_TPH_DEV_SUP)
    printf("\t\tDevice specific mode supported\n");
  if (tph_cap & PCI_TPH_EXT_REQ_SUP)
    printf("\t\tExtended requester support\n");

  switch (tph_cap & PCI_TPH_ST_LOC_MASK) {
  case PCI_TPH_ST_NONE:
    printf("\t\tNo steering table available\n");
    break;
  case PCI_TPH_ST_CAP:
    printf("\t\tSteering table in TPH capability structure\n");
    break;
  case PCI_TPH_ST_MSIX:
    printf("\t\tSteering table in MSI-X table\n");
    break;
  default:
    printf("\t\tReserved steering table location\n");
    break;
  }
}

static u32
cap_ltr_scale(u8 scale)
{
  return 1 << (scale * 5);
}

static void
cap_ltr(struct device *d, int where)
{
  u32 scale;
  u16 snoop, nosnoop;
  printf("Latency Tolerance Reporting\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_LTR_MAX_SNOOP, 4))
    return;

  snoop = get_conf_word(d, where + PCI_LTR_MAX_SNOOP);
  scale = cap_ltr_scale((snoop >> PCI_LTR_SCALE_SHIFT) & PCI_LTR_SCALE_MASK);
  printf("\t\tMax snoop latency: %" PCI_U64_FMT_U "ns\n",
	 ((u64)snoop & PCI_LTR_VALUE_MASK) * scale);

  nosnoop = get_conf_word(d, where + PCI_LTR_MAX_NOSNOOP);
  scale = cap_ltr_scale((nosnoop >> PCI_LTR_SCALE_SHIFT) & PCI_LTR_SCALE_MASK);
  printf("\t\tMax no snoop latency: %" PCI_U64_FMT_U "ns\n",
	 ((u64)nosnoop & PCI_LTR_VALUE_MASK) * scale);
}

static void
cap_sec(struct device *d, int where)
{
  u32 ctrl3, lane_err_stat;
  u8 lane;
  printf("Secondary PCI Express\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_SEC_LNKCTL3, 12))
    return;

  ctrl3 = get_conf_word(d, where + PCI_SEC_LNKCTL3);
  printf("\t\tLnkCtl3: LnkEquIntrruptEn%c PerformEqu%c\n",
	FLAG(ctrl3, PCI_SEC_LNKCTL3_LNK_EQU_REQ_INTR_EN),
	FLAG(ctrl3, PCI_SEC_LNKCTL3_PERFORM_LINK_EQU));

  lane_err_stat = get_conf_word(d, where + PCI_SEC_LANE_ERR);
  printf("\t\tLaneErrStat: ");
  if (lane_err_stat)
    {
      printf("LaneErr at lane:");
      for (lane = 0; lane_err_stat; lane_err_stat >>= 1, lane += 1)
        if (BITS(lane_err_stat, 0, 1))
          printf(" %u", lane);
    }
  else
    printf("0");
  printf("\n");
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
	t2 >> 24, (t2 >> 16) & 0xff, (t2 >> 8) & 0xff, t2 & 0xff,
	t1 >> 24, (t1 >> 16) & 0xff, (t1 >> 8) & 0xff, t1 & 0xff);
}

static void
cap_aer(struct device *d, int where, int type)
{
  u32 l, l0, l1, l2, l3;
  u16 w;

  printf("Advanced Error Reporting\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_ERR_UNCOR_STATUS, 40))
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
  printf("\t\tCESta:\tRxErr%c BadTLP%c BadDLLP%c Rollover%c Timeout%c AdvNonFatalErr%c\n",
	FLAG(l, PCI_ERR_COR_RCVR), FLAG(l, PCI_ERR_COR_BAD_TLP), FLAG(l, PCI_ERR_COR_BAD_DLLP),
	FLAG(l, PCI_ERR_COR_REP_ROLL), FLAG(l, PCI_ERR_COR_REP_TIMER), FLAG(l, PCI_ERR_COR_REP_ANFE));
  l = get_conf_long(d, where + PCI_ERR_COR_MASK);
  printf("\t\tCEMsk:\tRxErr%c BadTLP%c BadDLLP%c Rollover%c Timeout%c AdvNonFatalErr%c\n",
	FLAG(l, PCI_ERR_COR_RCVR), FLAG(l, PCI_ERR_COR_BAD_TLP), FLAG(l, PCI_ERR_COR_BAD_DLLP),
	FLAG(l, PCI_ERR_COR_REP_ROLL), FLAG(l, PCI_ERR_COR_REP_TIMER), FLAG(l, PCI_ERR_COR_REP_ANFE));
  l = get_conf_long(d, where + PCI_ERR_CAP);
  printf("\t\tAERCap:\tFirst Error Pointer: %02x, ECRCGenCap%c ECRCGenEn%c ECRCChkCap%c ECRCChkEn%c\n"
	"\t\t\tMultHdrRecCap%c MultHdrRecEn%c TLPPfxPres%c HdrLogCap%c\n",
	PCI_ERR_CAP_FEP(l), FLAG(l, PCI_ERR_CAP_ECRC_GENC), FLAG(l, PCI_ERR_CAP_ECRC_GENE),
	FLAG(l, PCI_ERR_CAP_ECRC_CHKC), FLAG(l, PCI_ERR_CAP_ECRC_CHKE),
	FLAG(l, PCI_ERR_CAP_MULT_HDRC), FLAG(l, PCI_ERR_CAP_MULT_HDRE),
	FLAG(l, PCI_ERR_CAP_TLP_PFX), FLAG(l, PCI_ERR_CAP_HDR_LOG));

  l0 = get_conf_long(d, where + PCI_ERR_HEADER_LOG);
  l1 = get_conf_long(d, where + PCI_ERR_HEADER_LOG + 4);
  l2 = get_conf_long(d, where + PCI_ERR_HEADER_LOG + 8);
  l3 = get_conf_long(d, where + PCI_ERR_HEADER_LOG + 12);
  printf("\t\tHeaderLog: %08x %08x %08x %08x\n", l0, l1, l2, l3);

  if (type == PCI_EXP_TYPE_ROOT_PORT || type == PCI_EXP_TYPE_ROOT_EC)
    {
      if (!config_fetch(d, where + PCI_ERR_ROOT_COMMAND, 12))
        return;

      l = get_conf_long(d, where + PCI_ERR_ROOT_COMMAND);
      printf("\t\tRootCmd: CERptEn%c NFERptEn%c FERptEn%c\n",
	    FLAG(l, PCI_ERR_ROOT_CMD_COR_EN),
	    FLAG(l, PCI_ERR_ROOT_CMD_NONFATAL_EN),
	    FLAG(l, PCI_ERR_ROOT_CMD_FATAL_EN));

      l = get_conf_long(d, where + PCI_ERR_ROOT_STATUS);
      printf("\t\tRootSta: CERcvd%c MultCERcvd%c UERcvd%c MultUERcvd%c\n"
	    "\t\t\t FirstFatal%c NonFatalMsg%c FatalMsg%c IntMsg %d\n",
	    FLAG(l, PCI_ERR_ROOT_COR_RCV),
	    FLAG(l, PCI_ERR_ROOT_MULTI_COR_RCV),
	    FLAG(l, PCI_ERR_ROOT_UNCOR_RCV),
	    FLAG(l, PCI_ERR_ROOT_MULTI_UNCOR_RCV),
	    FLAG(l, PCI_ERR_ROOT_FIRST_FATAL),
	    FLAG(l, PCI_ERR_ROOT_NONFATAL_RCV),
	    FLAG(l, PCI_ERR_ROOT_FATAL_RCV),
	    PCI_ERR_MSG_NUM(l));

      w = get_conf_word(d, where + PCI_ERR_ROOT_COR_SRC);
      printf("\t\tErrorSrc: ERR_COR: %04x ", w);

      w = get_conf_word(d, where + PCI_ERR_ROOT_SRC);
      printf("ERR_FATAL/NONFATAL: %04x\n", w);
    }
}

static void cap_dpc(struct device *d, int where)
{
  u16 l;

  printf("Downstream Port Containment\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_DPC_CAP, 8))
    return;

  l = get_conf_word(d, where + PCI_DPC_CAP);
  printf("\t\tDpcCap:\tINT Msg #%d, RPExt%c PoisonedTLP%c SwTrigger%c RP PIO Log %d, DL_ActiveErr%c\n",
    PCI_DPC_CAP_INT_MSG(l), FLAG(l, PCI_DPC_CAP_RP_EXT), FLAG(l, PCI_DPC_CAP_TLP_BLOCK),
    FLAG(l, PCI_DPC_CAP_SW_TRIGGER), PCI_DPC_CAP_RP_LOG(l), FLAG(l, PCI_DPC_CAP_DL_ACT_ERR));

  l = get_conf_word(d, where + PCI_DPC_CTL);
  printf("\t\tDpcCtl:\tTrigger:%x Cmpl%c INT%c ErrCor%c PoisonedTLP%c SwTrigger%c DL_ActiveErr%c\n",
    PCI_DPC_CTL_TRIGGER(l), FLAG(l, PCI_DPC_CTL_CMPL), FLAG(l, PCI_DPC_CTL_INT),
    FLAG(l, PCI_DPC_CTL_ERR_COR), FLAG(l, PCI_DPC_CTL_TLP), FLAG(l, PCI_DPC_CTL_SW_TRIGGER),
    FLAG(l, PCI_DPC_CTL_DL_ACTIVE));

  l = get_conf_word(d, where + PCI_DPC_STATUS);
  printf("\t\tDpcSta:\tTrigger%c Reason:%02x INT%c RPBusy%c TriggerExt:%02x RP PIO ErrPtr:%02x\n",
    FLAG(l, PCI_DPC_STS_TRIGGER), PCI_DPC_STS_REASON(l), FLAG(l, PCI_DPC_STS_INT),
    FLAG(l, PCI_DPC_STS_RP_BUSY), PCI_DPC_STS_TRIGGER_EXT(l), PCI_DPC_STS_PIO_FEP(l));

  l = get_conf_word(d, where + PCI_DPC_SOURCE);
  printf("\t\tSource:\t%04x\n", l);
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
cap_pri(struct device *d, int where)
{
  u16 w;
  u32 l;

  printf("Page Request Interface (PRI)\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_PRI_CTRL, 0xc))
    return;

  w = get_conf_word(d, where + PCI_PRI_CTRL);
  printf("\t\tPRICtl: Enable%c Reset%c\n",
	FLAG(w, PCI_PRI_CTRL_ENABLE), FLAG(w, PCI_PRI_CTRL_RESET));
  w = get_conf_word(d, where + PCI_PRI_STATUS);
  printf("\t\tPRISta: RF%c UPRGI%c Stopped%c\n",
	FLAG(w, PCI_PRI_STATUS_RF), FLAG(w, PCI_PRI_STATUS_UPRGI),
	FLAG(w, PCI_PRI_STATUS_STOPPED));
  l = get_conf_long(d, where + PCI_PRI_MAX_REQ);
  printf("\t\tPage Request Capacity: %08x, ", l);
  l = get_conf_long(d, where + PCI_PRI_ALLOC_REQ);
  printf("Page Request Allocation: %08x\n", l);
}

static void
cap_pasid(struct device *d, int where)
{
  u16 w;

  printf("Process Address Space ID (PASID)\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_PASID_CAP, 4))
    return;

  w = get_conf_word(d, where + PCI_PASID_CAP);
  printf("\t\tPASIDCap: Exec%c Priv%c, Max PASID Width: %02x\n",
	FLAG(w, PCI_PASID_CAP_EXEC), FLAG(w, PCI_PASID_CAP_PRIV),
	PCI_PASID_CAP_WIDTH(w));
  w = get_conf_word(d, where + PCI_PASID_CTRL);
  printf("\t\tPASIDCtl: Enable%c Exec%c Priv%c\n",
	FLAG(w, PCI_PASID_CTRL_ENABLE), FLAG(w, PCI_PASID_CTRL_EXEC),
	FLAG(w, PCI_PASID_CTRL_PRIV));
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
  printf("\t\tIOVCap:\tMigration%c 10BitTagReq%c Interrupt Message Number: %03x\n",
	FLAG(l, PCI_IOV_CAP_VFM), FLAG(l, PCI_IOV_CAP_VF_10BIT_TAG_REQ), PCI_IOV_CAP_IMN(l));
  w = get_conf_word(d, where + PCI_IOV_CTRL);
  printf("\t\tIOVCtl:\tEnable%c Migration%c Interrupt%c MSE%c ARIHierarchy%c 10BitTagReq%c\n",
	FLAG(w, PCI_IOV_CTRL_VFE), FLAG(w, PCI_IOV_CTRL_VFME),
	FLAG(w, PCI_IOV_CTRL_VFMIE), FLAG(w, PCI_IOV_CTRL_MSE),
	FLAG(w, PCI_IOV_CTRL_ARI), FLAG(w, PCI_IOV_CTRL_VF_10BIT_TAG_REQ_EN));
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
      u32 addr;
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
	  printf("%08x", h);
	}
      printf("%08x (%s-bit, %sprefetchable)\n",
	addr,
	(type == PCI_BASE_ADDRESS_MEM_TYPE_32) ? "32" : "64",
	(l & PCI_BASE_ADDRESS_MEM_PREFETCH) ? "" : "non-");
    }

  l = get_conf_long(d, where + PCI_IOV_MSAO);
  printf("\t\tVF Migration: offset: %08x, BIR: %x\n", PCI_IOV_MSA_OFFSET(l),
	PCI_IOV_MSA_BIR(l));
}

static void
cap_multicast(struct device *d, int where, int type)
{
  u16 w;
  u32 l;
  u64 bar, rcv, block;

  printf("Multicast\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_MCAST_CAP, 0x30))
    return;

  w = get_conf_word(d, where + PCI_MCAST_CAP);
  printf("\t\tMcastCap: MaxGroups %d", PCI_MCAST_CAP_MAX_GROUP(w) + 1);
  if (type == PCI_EXP_TYPE_ENDPOINT || type == PCI_EXP_TYPE_ROOT_INT_EP)
    printf(", WindowSz %d (%d bytes)",
      PCI_MCAST_CAP_WIN_SIZE(w), 1 << PCI_MCAST_CAP_WIN_SIZE(w));
  if (type == PCI_EXP_TYPE_ROOT_PORT ||
      type == PCI_EXP_TYPE_UPSTREAM || type == PCI_EXP_TYPE_DOWNSTREAM)
    printf(", ECRCRegen%c\n", FLAG(w, PCI_MCAST_CAP_ECRC));
  w = get_conf_word(d, where + PCI_MCAST_CTRL);
  printf("\t\tMcastCtl: NumGroups %d, Enable%c\n",
    PCI_MCAST_CTRL_NUM_GROUP(w) + 1, FLAG(w, PCI_MCAST_CTRL_ENABLE));
  bar = get_conf_long(d, where + PCI_MCAST_BAR);
  l = get_conf_long(d, where + PCI_MCAST_BAR + 4);
  bar |= (u64) l << 32;
  printf("\t\tMcastBAR: IndexPos %d, BaseAddr %016" PCI_U64_FMT_X "\n",
    PCI_MCAST_BAR_INDEX_POS(bar), bar & PCI_MCAST_BAR_MASK);
  rcv = get_conf_long(d, where + PCI_MCAST_RCV);
  l = get_conf_long(d, where + PCI_MCAST_RCV + 4);
  rcv |= (u64) l << 32;
  printf("\t\tMcastReceiveVec:      %016" PCI_U64_FMT_X "\n", rcv);
  block = get_conf_long(d, where + PCI_MCAST_BLOCK);
  l = get_conf_long(d, where + PCI_MCAST_BLOCK + 4);
  block |= (u64) l << 32;
  printf("\t\tMcastBlockAllVec:     %016" PCI_U64_FMT_X "\n", block);
  block = get_conf_long(d, where + PCI_MCAST_BLOCK_UNTRANS);
  l = get_conf_long(d, where + PCI_MCAST_BLOCK_UNTRANS + 4);
  block |= (u64) l << 32;
  printf("\t\tMcastBlockUntransVec: %016" PCI_U64_FMT_X "\n", block);

  if (type == PCI_EXP_TYPE_ENDPOINT || type == PCI_EXP_TYPE_ROOT_INT_EP)
    return;
  bar = get_conf_long(d, where + PCI_MCAST_OVL_BAR);
  l = get_conf_long(d, where + PCI_MCAST_OVL_BAR + 4);
  bar |= (u64) l << 32;
  printf("\t\tMcastOverlayBAR: OverlaySize %d ", PCI_MCAST_OVL_SIZE(bar));
  if (PCI_MCAST_OVL_SIZE(bar) >= 6)
    printf("(%d bytes)", 1 << PCI_MCAST_OVL_SIZE(bar));
  else
    printf("(disabled)");
  printf(", BaseAddr %016" PCI_U64_FMT_X "\n", bar & PCI_MCAST_OVL_MASK);
}

static void
cap_vc(struct device *d, int where)
{
  u32 cr1, cr2;
  u16 ctrl, status;
  int evc_cnt;
  int arb_table_pos;
  int i, j;
  static const char ref_clocks[][6] = { "100ns" };
  static const char arb_selects[8][7] = { "Fixed", "WRR32", "WRR64", "WRR128", "??4", "??5", "??6", "??7" };
  static const char vc_arb_selects[8][8] = { "Fixed", "WRR32", "WRR64", "WRR128", "TWRR128", "WRR256", "??6", "??7" };
  char buf[8];

  printf("Virtual Channel\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + 4, 0x1c - 4))
    return;

  cr1 = get_conf_long(d, where + PCI_VC_PORT_REG1);
  cr2 = get_conf_long(d, where + PCI_VC_PORT_REG2);
  ctrl = get_conf_word(d, where + PCI_VC_PORT_CTRL);
  status = get_conf_word(d, where + PCI_VC_PORT_STATUS);

  evc_cnt = BITS(cr1, 0, 3);
  printf("\t\tCaps:\tLPEVC=%d RefClk=%s PATEntryBits=%d\n",
    BITS(cr1, 4, 3),
    TABLE(ref_clocks, BITS(cr1, 8, 2), buf),
    1 << BITS(cr1, 10, 2));

  printf("\t\tArb:");
  for (i=0; i<8; i++)
    if (arb_selects[i][0] != '?' || cr2 & (1 << i))
      printf("%c%s%c", (i ? ' ' : '\t'), arb_selects[i], FLAG(cr2, 1 << i));
  arb_table_pos = BITS(cr2, 24, 8);

  printf("\n\t\tCtrl:\tArbSelect=%s\n", TABLE(arb_selects, BITS(ctrl, 1, 3), buf));
  printf("\t\tStatus:\tInProgress%c\n", FLAG(status, 1));

  if (arb_table_pos)
    {
      arb_table_pos = where + 16*arb_table_pos;
      printf("\t\tPort Arbitration Table [%x] <?>\n", arb_table_pos);
    }

  for (i=0; i<=evc_cnt; i++)
    {
      int pos = where + PCI_VC_RES_CAP + 12*i;
      u32 rcap, rctrl;
      u16 rstatus;
      int pat_pos;

      printf("\t\tVC%d:\t", i);
      if (!config_fetch(d, pos, 12))
	{
	  printf("<unreadable>\n");
	  continue;
	}
      rcap = get_conf_long(d, pos);
      rctrl = get_conf_long(d, pos+4);
      rstatus = get_conf_word(d, pos+10);

      pat_pos = BITS(rcap, 24, 8);
      printf("Caps:\tPATOffset=%02x MaxTimeSlots=%d RejSnoopTrans%c\n",
	pat_pos,
	BITS(rcap, 16, 6) + 1,
	FLAG(rcap, 1 << 15));

      printf("\t\t\tArb:");
      for (j=0; j<8; j++)
	if (vc_arb_selects[j][0] != '?' || rcap & (1 << j))
	  printf("%c%s%c", (j ? ' ' : '\t'), vc_arb_selects[j], FLAG(rcap, 1 << j));

      printf("\n\t\t\tCtrl:\tEnable%c ID=%d ArbSelect=%s TC/VC=%02x\n",
	FLAG(rctrl, 1 << 31),
	BITS(rctrl, 24, 3),
	TABLE(vc_arb_selects, BITS(rctrl, 17, 3), buf),
	BITS(rctrl, 0, 8));

      printf("\t\t\tStatus:\tNegoPending%c InProgress%c\n",
	FLAG(rstatus, 2),
	FLAG(rstatus, 1));

      if (pat_pos)
	printf("\t\t\tPort Arbitration Table <?>\n");
    }
}

static void
cap_rclink(struct device *d, int where)
{
  u32 esd;
  int num_links;
  int i;
  static const char elt_types[][9] = { "Config", "Egress", "Internal" };
  char buf[8];

  printf("Root Complex Link\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where + 4, PCI_RCLINK_LINK1 - 4))
    return;

  esd = get_conf_long(d, where + PCI_RCLINK_ESD);
  num_links = BITS(esd, 8, 8);
  printf("\t\tDesc:\tPortNumber=%02x ComponentID=%02x EltType=%s\n",
    BITS(esd, 24, 8),
    BITS(esd, 16, 8),
    TABLE(elt_types, BITS(esd, 0, 8), buf));

  for (i=0; i<num_links; i++)
    {
      int pos = where + PCI_RCLINK_LINK1 + i*PCI_RCLINK_LINK_SIZE;
      u32 desc;
      u32 addr_lo, addr_hi;

      printf("\t\tLink%d:\t", i);
      if (!config_fetch(d, pos, PCI_RCLINK_LINK_SIZE))
	{
	  printf("<unreadable>\n");
	  return;
	}
      desc = get_conf_long(d, pos + PCI_RCLINK_LINK_DESC);
      addr_lo = get_conf_long(d, pos + PCI_RCLINK_LINK_ADDR);
      addr_hi = get_conf_long(d, pos + PCI_RCLINK_LINK_ADDR + 4);

      printf("Desc:\tTargetPort=%02x TargetComponent=%02x AssocRCRB%c LinkType=%s LinkValid%c\n",
	BITS(desc, 24, 8),
	BITS(desc, 16, 8),
	FLAG(desc, 4),
	((desc & 2) ? "Config" : "MemMapped"),
	FLAG(desc, 1));

      if (desc & 2)
	{
	  int n = addr_lo & 7;
	  if (!n)
	    n = 8;
	  printf("\t\t\tAddr:\t%02x:%02x.%d  CfgSpace=%08x%08x\n",
	    BITS(addr_lo, 20, n),
	    BITS(addr_lo, 15, 5),
	    BITS(addr_lo, 12, 3),
	    addr_hi, addr_lo);
	}
      else
	printf("\t\t\tAddr:\t%08x%08x\n", addr_hi, addr_lo);
    }
}

static void
cap_rcec(struct device *d, int where)
{
  printf("Root Complex Event Collector Endpoint Association\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where, 12))
    return;

  u32 hdr = get_conf_long(d, where);
  byte cap_ver = PCI_RCEC_EP_CAP_VER(hdr);
  u32 bmap = get_conf_long(d, where + PCI_RCEC_RCIEP_BMAP);
  printf("\t\tRCiEPBitmap: ");
  if (bmap)
    {
      int prevmatched=0;
      int adjcount=0;
      int prevdev=0;
      printf("RCiEP at Device(s):");
      for (int dev=0; dev < 32; dev++)
        {
	  if (BITS(bmap, dev, 1))
	    {
	      if (!adjcount)
	        printf("%s %u", (prevmatched) ? "," : "", dev);
	      adjcount++;
	      prevdev=dev;
	      prevmatched=1;
            }
	  else
	    {
	      if (adjcount > 1)
	        printf("-%u", prevdev);
	      adjcount=0;
            }
        }
   }
  else
    printf("%s", (verbose > 2) ? "00000000 [none]" : "[none]");
  printf("\n");

  if (cap_ver < PCI_RCEC_BUSN_REG_VER)
    return;

  u32 busn = get_conf_long(d, where + PCI_RCEC_BUSN_REG);
  u8 lastbusn = BITS(busn, 16, 8);
  u8 nextbusn = BITS(busn, 8, 8);

  if ((lastbusn == 0x00) && (nextbusn == 0xff))
    printf("\t\tAssociatedBusNumbers: %s\n", (verbose > 2) ? "ff-00 [none]" : "[none]");
  else
    printf("\t\tAssociatedBusNumbers: %02x-%02x\n", nextbusn, lastbusn );
}

static void
cap_dvsec_cxl(struct device *d, int id, int where)
{
  u16 w;

  printf(": CXL\n");
  if (verbose < 2)
    return;

  if (id != 0)
    return;

  if (!config_fetch(d, where + PCI_CXL_CAP, 12))
    return;

  w = get_conf_word(d, where + PCI_CXL_CAP);
  printf("\t\tCXLCap:\tCache%c IO%c Mem%c Mem HW Init%c HDMCount %d Viral%c\n",
    FLAG(w, PCI_CXL_CAP_CACHE), FLAG(w, PCI_CXL_CAP_IO), FLAG(w, PCI_CXL_CAP_MEM),
    FLAG(w, PCI_CXL_CAP_MEM_HWINIT), PCI_CXL_CAP_HDM_CNT(w), FLAG(w, PCI_CXL_CAP_VIRAL));

  w = get_conf_word(d, where + PCI_CXL_CTRL);
  printf("\t\tCXLCtl:\tCache%c IO%c Mem%c Cache SF Cov %d Cache SF Gran %d Cache Clean%c Viral%c\n",
    FLAG(w, PCI_CXL_CTRL_CACHE), FLAG(w, PCI_CXL_CTRL_IO), FLAG(w, PCI_CXL_CTRL_MEM),
    PCI_CXL_CTRL_CACHE_SF_COV(w), PCI_CXL_CTRL_CACHE_SF_GRAN(w), FLAG(w, PCI_CXL_CTRL_CACHE_CLN),
    FLAG(w, PCI_CXL_CTRL_VIRAL));

  w = get_conf_word(d, where + PCI_CXL_STATUS);
  printf("\t\tCXLSta:\tViral%c\n", FLAG(w, PCI_CXL_STATUS_VIRAL));
}

static void
cap_dvsec(struct device *d, int where)
{
  printf("Designated Vendor-Specific: ");
  if (!config_fetch(d, where + PCI_DVSEC_HEADER1, 8))
    {
      printf("<unreadable>\n");
      return;
    }

  u32 hdr = get_conf_long(d, where + PCI_DVSEC_HEADER1);
  u16 vendor = BITS(hdr, 0, 16);
  byte rev = BITS(hdr, 16, 4);
  u16 len = BITS(hdr, 20, 12);

  u16 id = get_conf_long(d, where + PCI_DVSEC_HEADER2);

  printf("Vendor=%04x ID=%04x Rev=%d Len=%d", vendor, id, rev, len);
  if (vendor == PCI_DVSEC_VENDOR_ID_CXL && len >= 16)
    cap_dvsec_cxl(d, id, where);
  else
    printf(" <?>\n");
}

static void
cap_evendor(struct device *d, int where)
{
  u32 hdr;

  printf("Vendor Specific Information: ");
  if (!config_fetch(d, where + PCI_EVNDR_HEADER, 4))
    {
      printf("<unreadable>\n");
      return;
    }

  hdr = get_conf_long(d, where + PCI_EVNDR_HEADER);
  printf("ID=%04x Rev=%d Len=%03x <?>\n",
    BITS(hdr, 0, 16),
    BITS(hdr, 16, 4),
    BITS(hdr, 20, 12));
}

static int l1pm_calc_pwron(int scale, int value)
{
  switch (scale)
    {
      case 0:
	return 2 * value;
      case 1:
	return 10 * value;
      case 2:
	return 100 * value;
    }
  return -1;
}

static void
cap_l1pm(struct device *d, int where)
{
  u32 l1_cap, val, scale;
  int time;

  printf("L1 PM Substates\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_L1PM_SUBSTAT_CAP, 12))
    {
      printf("\t\t<unreadable>\n");
      return;
    }

  l1_cap = get_conf_long(d, where + PCI_L1PM_SUBSTAT_CAP);
  printf("\t\tL1SubCap: ");
  printf("PCI-PM_L1.2%c PCI-PM_L1.1%c ASPM_L1.2%c ASPM_L1.1%c L1_PM_Substates%c\n",
    FLAG(l1_cap, PCI_L1PM_SUBSTAT_CAP_PM_L12),
    FLAG(l1_cap, PCI_L1PM_SUBSTAT_CAP_PM_L11),
    FLAG(l1_cap, PCI_L1PM_SUBSTAT_CAP_ASPM_L12),
    FLAG(l1_cap, PCI_L1PM_SUBSTAT_CAP_ASPM_L11),
    FLAG(l1_cap, PCI_L1PM_SUBSTAT_CAP_L1PM_SUPP));

  if (l1_cap & PCI_L1PM_SUBSTAT_CAP_PM_L12 || l1_cap & PCI_L1PM_SUBSTAT_CAP_ASPM_L12)
    {
      printf("\t\t\t  PortCommonModeRestoreTime=%dus ", BITS(l1_cap, 8, 8));
      time = l1pm_calc_pwron(BITS(l1_cap, 16, 2), BITS(l1_cap, 19, 5));
      if (time != -1)
	printf("PortTPowerOnTime=%dus\n", time);
      else
	printf("PortTPowerOnTime=<error>\n");
    }

  val = get_conf_long(d, where + PCI_L1PM_SUBSTAT_CTL1);
  printf("\t\tL1SubCtl1: PCI-PM_L1.2%c PCI-PM_L1.1%c ASPM_L1.2%c ASPM_L1.1%c\n",
    FLAG(val, PCI_L1PM_SUBSTAT_CTL1_PM_L12),
    FLAG(val, PCI_L1PM_SUBSTAT_CTL1_PM_L11),
    FLAG(val, PCI_L1PM_SUBSTAT_CTL1_ASPM_L12),
    FLAG(val, PCI_L1PM_SUBSTAT_CTL1_ASPM_L11));

  if (l1_cap & PCI_L1PM_SUBSTAT_CAP_PM_L12 || l1_cap & PCI_L1PM_SUBSTAT_CAP_ASPM_L12)
    {
      printf("\t\t\t   T_CommonMode=%dus", BITS(val, 8, 8));

      if (l1_cap & PCI_L1PM_SUBSTAT_CAP_ASPM_L12)
	{
	  scale = BITS(val, 29, 3);
	  if (scale > 5)
	    printf(" LTR1.2_Threshold=<error>");
	  else
	    printf(" LTR1.2_Threshold=%" PCI_U64_FMT_U "ns", BITS(val, 16, 10) * (u64) cap_ltr_scale(scale));
	}
      printf("\n");
    }

  val = get_conf_long(d, where + PCI_L1PM_SUBSTAT_CTL2);
  printf("\t\tL1SubCtl2:");
  if (l1_cap & PCI_L1PM_SUBSTAT_CAP_PM_L12 || l1_cap & PCI_L1PM_SUBSTAT_CAP_ASPM_L12)
    {
      time = l1pm_calc_pwron(BITS(val, 0, 2), BITS(val, 3, 5));
      if (time != -1)
	printf(" T_PwrOn=%dus", time);
      else
	printf(" T_PwrOn=<error>");
    }
  printf("\n");
}

static void
cap_ptm(struct device *d, int where)
{
  u32 buff;
  u16 clock;

  printf("Precision Time Measurement\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + 4, 8))
    {
      printf("\t\t<unreadable>\n");
      return;
    }

  buff = get_conf_long(d, where + 4);
  printf("\t\tPTMCap: ");
  printf("Requester:%c Responder:%c Root:%c\n",
    FLAG(buff, 0x1),
    FLAG(buff, 0x2),
    FLAG(buff, 0x4));

  clock = BITS(buff, 8, 8);
  printf("\t\tPTMClockGranularity: ");
  switch (clock)
    {
      case 0x00:
        printf("Unimplemented\n");
        break;
      case 0xff:
        printf("Greater than 254ns\n");
        break;
      default:
        printf("%huns\n", clock);
    }

  buff = get_conf_long(d, where + 8);
  printf("\t\tPTMControl: ");
  printf("Enabled:%c RootSelected:%c\n",
    FLAG(buff, 0x1),
    FLAG(buff, 0x2));

  clock = BITS(buff, 8, 8);
  printf("\t\tPTMEffectiveGranularity: ");
  switch (clock)
    {
      case 0x00:
        printf("Unknown\n");
        break;
      case 0xff:
        printf("Greater than 254ns\n");
        break;
      default:
        printf("%huns\n", clock);
    }
}

static void
print_rebar_range_size(int ld2_size)
{
  // This function prints the input as a power-of-2 size value
  // It is biased with 1MB = 0, ...
  // Maximum resizable BAR value supported is 2^63 bytes = 43
  // for the extended resizable BAR capability definition
  // (otherwise it would stop at 2^28)

  if (ld2_size >= 0 && ld2_size < 10)
    printf(" %dMB", (1 << ld2_size));
  else if (ld2_size >= 10 && ld2_size < 20)
    printf(" %dGB", (1 << (ld2_size-10)));
  else if (ld2_size >= 20 && ld2_size < 30)
    printf(" %dTB", (1 << (ld2_size-20)));
  else if (ld2_size >= 30 && ld2_size < 40)
    printf(" %dPB", (1 << (ld2_size-30)));
  else if (ld2_size >= 40 && ld2_size < 44)
    printf(" %dEB", (1 << (ld2_size-40)));
  else
    printf(" <unknown>");
}

static void
cap_rebar(struct device *d, int where, int virtual)
{
  u32 sizes_buffer, control_buffer, ext_sizes, current_size;
  u16 bar_index, barcount, i;
  // If the structure exists, at least one bar is defined
  u16 num_bars = 1;

  printf("%s Resizable BAR\n", (virtual) ? "Virtual" : "Physical");

  if (verbose < 2)
    return;

  // Go through all defined BAR definitions of the caps, at minimum 1
  // (loop also terminates if num_bars read from caps is > 6)
  for (barcount = 0; barcount < num_bars; barcount++)
    {
      where += 4;

      // Get the next BAR configuration
      if (!config_fetch(d, where, 8))
        {
          printf("\t\t<unreadable>\n");
          return;
        }

      sizes_buffer = get_conf_long(d, where) >> 4;
      where += 4;
      control_buffer = get_conf_long(d, where);

      bar_index  = BITS(control_buffer, 0, 3);
      current_size = BITS(control_buffer, 8, 6);
      ext_sizes = BITS(control_buffer, 16, 16);

      if (barcount == 0)
        {
          // Only index 0 controlreg has the num_bar count definition
          num_bars = BITS(control_buffer, 5, 3);
	  if (num_bars < 1 || num_bars > 6)
	    {
	      printf("\t\t<error in resizable BAR: num_bars=%d is out of specification>\n", num_bars);
	      break;
	    }
        }

      // Resizable BAR list entry have an arbitrary index and current size
      printf("\t\tBAR %d: current size:", bar_index);
      print_rebar_range_size(current_size);

      if (sizes_buffer || ext_sizes)
	{
	  printf(", supported:");

	  for (i=0; i<28; i++)
	    if (sizes_buffer & (1U << i))
	      print_rebar_range_size(i);

	  for (i=0; i<16; i++)
	    if (ext_sizes & (1U << i))
	      print_rebar_range_size(i + 28);
	}

      printf("\n");
    }
}

void
show_ext_caps(struct device *d, int type)
{
  int where = 0x100;
  char been_there[0x1000];
  memset(been_there, 0, 0x1000);
  do
    {
      u32 header;
      int id, version;

      if (!config_fetch(d, where, 4))
	break;
      header = get_conf_long(d, where);
      if (!header)
	break;
      id = header & 0xffff;
      version = (header >> 16) & 0xf;
      printf("\tCapabilities: [%03x", where);
      if (verbose > 1)
	printf(" v%d", version);
      printf("] ");
      if (been_there[where]++)
	{
	  printf("<chain looped>\n");
	  break;
	}
      switch (id)
	{
	  case PCI_EXT_CAP_ID_NULL:
	    printf("Null\n");
	    break;
	  case PCI_EXT_CAP_ID_AER:
	    cap_aer(d, where, type);
	    break;
	  case PCI_EXT_CAP_ID_DPC:
	    cap_dpc(d, where);
	    break;
	  case PCI_EXT_CAP_ID_VC:
	  case PCI_EXT_CAP_ID_VC2:
	    cap_vc(d, where);
	    break;
	  case PCI_EXT_CAP_ID_DSN:
	    cap_dsn(d, where);
	    break;
	  case PCI_EXT_CAP_ID_PB:
	    printf("Power Budgeting <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RCLINK:
	    cap_rclink(d, where);
	    break;
	  case PCI_EXT_CAP_ID_RCILINK:
	    printf("Root Complex Internal Link <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RCEC:
	    cap_rcec(d, where);
	    break;
	  case PCI_EXT_CAP_ID_MFVC:
	    printf("Multi-Function Virtual Channel <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RCRB:
	    printf("Root Complex Register Block <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_VNDR:
	    cap_evendor(d, where);
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
	  case PCI_EXT_CAP_ID_MRIOV:
	    printf("Multi-Root I/O Virtualization <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_MCAST:
	    cap_multicast(d, where, type);
	    break;
	  case PCI_EXT_CAP_ID_PRI:
	    cap_pri(d, where);
	    break;
	  case PCI_EXT_CAP_ID_REBAR:
	    cap_rebar(d, where, 0);
	    break;
	  case PCI_EXT_CAP_ID_DPA:
	    printf("Dynamic Power Allocation <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_TPH:
	    cap_tph(d, where);
	    break;
	  case PCI_EXT_CAP_ID_LTR:
	    cap_ltr(d, where);
	    break;
	  case PCI_EXT_CAP_ID_SECPCI:
	    cap_sec(d, where);
	    break;
	  case PCI_EXT_CAP_ID_PMUX:
	    printf("Protocol Multiplexing <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_PASID:
	    cap_pasid(d, where);
	    break;
	  case PCI_EXT_CAP_ID_LNR:
	    printf("LN Requester <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_L1PM:
	    cap_l1pm(d, where);
	    break;
	  case PCI_EXT_CAP_ID_PTM:
	    cap_ptm(d, where);
	    break;
	  case PCI_EXT_CAP_ID_M_PCIE:
	    printf("PCI Express over M_PHY <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_FRS:
	    printf("FRS Queueing <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_RTR:
	    printf("Readiness Time Reporting <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_DVSEC:
	    cap_dvsec(d, where);
	    break;
	  case PCI_EXT_CAP_ID_VF_REBAR:
	    cap_rebar(d, where, 1);
	    break;
	  case PCI_EXT_CAP_ID_DLNK:
	    printf("Data Link Feature <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_16GT:
	    printf("Physical Layer 16.0 GT/s <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_LMR:
	    printf("Lane Margining at the Receiver <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_HIER_ID:
	    printf("Hierarchy ID <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_NPEM:
	    printf("Native PCIe Enclosure Management <?>\n");
	    break;
	  default:
	    printf("Extended Capability ID %#02x\n", id);
	    break;
	}
      where = (header >> 20) & ~3;
    } while (where);
}
