/*
 *	The PCI Utilities -- Show Extended Capabilities
 *
 *	Copyright (c) 1997--2022 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
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
  printf("\t\tUESta:\tDLP%c SDES%c TLP%c FCP%c CmpltTO%c CmpltAbrt%c UnxCmplt%c RxOF%c MalfTLP%c\n"
  "\t\t\tECRC%c UnsupReq%c ACSViol%c UncorrIntErr%c BlockedTLP%c AtomicOpBlocked%c TLPBlockedErr%c\n"
  "\t\t\tPoisonTLPBlocked%c DMWrReqBlocked%c IDECheck%c MisIDETLP%c PCRC_CHECK%c TLPXlatBlocked%c\n",
	FLAG(l, PCI_ERR_UNC_DLP), FLAG(l, PCI_ERR_UNC_SDES), FLAG(l, PCI_ERR_UNC_POISON_TLP),
	FLAG(l, PCI_ERR_UNC_FCP), FLAG(l, PCI_ERR_UNC_COMP_TIME), FLAG(l, PCI_ERR_UNC_COMP_ABORT),
	FLAG(l, PCI_ERR_UNC_UNX_COMP), FLAG(l, PCI_ERR_UNC_RX_OVER), FLAG(l, PCI_ERR_UNC_MALF_TLP),
	FLAG(l, PCI_ERR_UNC_ECRC), FLAG(l, PCI_ERR_UNC_UNSUP), FLAG(l, PCI_ERR_UNC_ACS_VIOL),
	FLAG(l, PCI_ERR_UNC_INTERNAL), FLAG(l, PCI_ERR_UNC_MC_BLOCKED_TLP),
	FLAG(l, PCI_ERR_UNC_ATOMICOP_EGRESS_BLOCKED), FLAG(l, PCI_ERR_UNC_TLP_PREFIX_BLOCKED),
	FLAG(l, PCI_ERR_UNC_POISONED_TLP_EGRESS), FLAG(l, PCI_ERR_UNC_DMWR_REQ_EGRESS_BLOCKED),
	FLAG(l, PCI_ERR_UNC_IDE_CHECK), FLAG(l, PCI_ERR_UNC_MISR_IDE_TLP), FLAG(l, PCI_ERR_UNC_PCRC_CHECK),
	FLAG(l, PCI_ERR_UNC_TLP_XLAT_EGRESS_BLOCKED));
  l = get_conf_long(d, where + PCI_ERR_UNCOR_MASK);
  printf("\t\tUEMsk:\tDLP%c SDES%c TLP%c FCP%c CmpltTO%c CmpltAbrt%c UnxCmplt%c RxOF%c MalfTLP%c\n"
  "\t\t\tECRC%c UnsupReq%c ACSViol%c UncorrIntErr%c BlockedTLP%c AtomicOpBlocked%c TLPBlockedErr%c\n"
  "\t\t\tPoisonTLPBlocked%c DMWrReqBlocked%c IDECheck%c MisIDETLP%c PCRC_CHECK%c TLPXlatBlocked%c\n",
	FLAG(l, PCI_ERR_UNC_DLP), FLAG(l, PCI_ERR_UNC_SDES), FLAG(l, PCI_ERR_UNC_POISON_TLP),
	FLAG(l, PCI_ERR_UNC_FCP), FLAG(l, PCI_ERR_UNC_COMP_TIME), FLAG(l, PCI_ERR_UNC_COMP_ABORT),
	FLAG(l, PCI_ERR_UNC_UNX_COMP), FLAG(l, PCI_ERR_UNC_RX_OVER), FLAG(l, PCI_ERR_UNC_MALF_TLP),
	FLAG(l, PCI_ERR_UNC_ECRC), FLAG(l, PCI_ERR_UNC_UNSUP), FLAG(l, PCI_ERR_UNC_ACS_VIOL),
	FLAG(l, PCI_ERR_UNC_INTERNAL), FLAG(l, PCI_ERR_UNC_MC_BLOCKED_TLP),
	FLAG(l, PCI_ERR_UNC_ATOMICOP_EGRESS_BLOCKED), FLAG(l, PCI_ERR_UNC_TLP_PREFIX_BLOCKED),
	FLAG(l, PCI_ERR_UNC_POISONED_TLP_EGRESS), FLAG(l, PCI_ERR_UNC_DMWR_REQ_EGRESS_BLOCKED),
	FLAG(l, PCI_ERR_UNC_IDE_CHECK), FLAG(l, PCI_ERR_UNC_MISR_IDE_TLP), FLAG(l, PCI_ERR_UNC_PCRC_CHECK),
	FLAG(l, PCI_ERR_UNC_TLP_XLAT_EGRESS_BLOCKED));
  l = get_conf_long(d, where + PCI_ERR_UNCOR_SEVER);
  printf("\t\tUESvrt:\tDLP%c SDES%c TLP%c FCP%c CmpltTO%c CmpltAbrt%c UnxCmplt%c RxOF%c MalfTLP%c\n"
  "\t\t\tECRC%c UnsupReq%c ACSViol%c UncorrIntErr%c BlockedTLP%c AtomicOpBlocked%c TLPBlockedErr%c\n"
  "\t\t\tPoisonTLPBlocked%c DMWrReqBlocked%c IDECheck%c MisIDETLP%c PCRC_CHECK%c TLPXlatBlocked%c\n",
	FLAG(l, PCI_ERR_UNC_DLP), FLAG(l, PCI_ERR_UNC_SDES), FLAG(l, PCI_ERR_UNC_POISON_TLP),
	FLAG(l, PCI_ERR_UNC_FCP), FLAG(l, PCI_ERR_UNC_COMP_TIME), FLAG(l, PCI_ERR_UNC_COMP_ABORT),
	FLAG(l, PCI_ERR_UNC_UNX_COMP), FLAG(l, PCI_ERR_UNC_RX_OVER), FLAG(l, PCI_ERR_UNC_MALF_TLP),
	FLAG(l, PCI_ERR_UNC_ECRC), FLAG(l, PCI_ERR_UNC_UNSUP), FLAG(l, PCI_ERR_UNC_ACS_VIOL),
	FLAG(l, PCI_ERR_UNC_INTERNAL), FLAG(l, PCI_ERR_UNC_MC_BLOCKED_TLP),
	FLAG(l, PCI_ERR_UNC_ATOMICOP_EGRESS_BLOCKED), FLAG(l, PCI_ERR_UNC_TLP_PREFIX_BLOCKED),
	FLAG(l, PCI_ERR_UNC_POISONED_TLP_EGRESS), FLAG(l, PCI_ERR_UNC_DMWR_REQ_EGRESS_BLOCKED),
	FLAG(l, PCI_ERR_UNC_IDE_CHECK), FLAG(l, PCI_ERR_UNC_MISR_IDE_TLP), FLAG(l, PCI_ERR_UNC_PCRC_CHECK),
	FLAG(l, PCI_ERR_UNC_TLP_XLAT_EGRESS_BLOCKED));
  l = get_conf_long(d, where + PCI_ERR_COR_STATUS);
  printf("\t\tCESta:\tRxErr%c BadTLP%c BadDLLP%c Rollover%c Timeout%c AdvNonFatalErr%c "
  "CorrIntErr%c HeaderOF%c\n",
	FLAG(l, PCI_ERR_COR_RCVR), FLAG(l, PCI_ERR_COR_BAD_TLP), FLAG(l, PCI_ERR_COR_BAD_DLLP),
	FLAG(l, PCI_ERR_COR_REP_ROLL), FLAG(l, PCI_ERR_COR_REP_TIMER), FLAG(l, PCI_ERR_COR_REP_ANFE),
	FLAG(l, PCI_ERR_COR_INTERNAL), FLAG(l, PCI_ERR_COR_HDRLOG_OVER));
  l = get_conf_long(d, where + PCI_ERR_COR_MASK);
  printf("\t\tCEMsk:\tRxErr%c BadTLP%c BadDLLP%c Rollover%c Timeout%c AdvNonFatalErr%c "
  "CorrIntErr%c HeaderOF%c\n",
	FLAG(l, PCI_ERR_COR_RCVR), FLAG(l, PCI_ERR_COR_BAD_TLP), FLAG(l, PCI_ERR_COR_BAD_DLLP),
	FLAG(l, PCI_ERR_COR_REP_ROLL), FLAG(l, PCI_ERR_COR_REP_TIMER), FLAG(l, PCI_ERR_COR_REP_ANFE),
	FLAG(l, PCI_ERR_COR_INTERNAL), FLAG(l, PCI_ERR_COR_HDRLOG_OVER));
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
	    "\t\t\t FirstFatal%c NonFatalMsg%c FatalMsg%c IntMsgNum %d\n",
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
  printf("\t\tDpcCap:\tIntMsgNum %d, RPExt%c PoisonedTLP%c SwTrigger%c RP PIO Log %d, DL_ActiveErr%c\n",
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
  printf("\t\tPRISta: RF%c UPRGI%c Stopped%c PASID%c\n",
	FLAG(w, PCI_PRI_STATUS_RF), FLAG(w, PCI_PRI_STATUS_UPRGI),
	FLAG(w, PCI_PRI_STATUS_STOPPED), FLAG(w, PCI_PRI_STATUS_PASID));
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
  printf("\t\tIOVCap:\tMigration%c 10BitTagReq%c IntMsgNum %d\n",
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
	BITS(rcap, 16, 7) + 1,
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
cap_lmr(struct device *d, int where)
{
  printf("Lane Margining at the Receiver\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where, 8))
    return;

  u16 port_caps = get_conf_word(d, where + PCI_LMR_CAPS);
  u16 port_status = get_conf_word(d, where + PCI_LMR_PORT_STS);

  printf("\t\tPortCap: Uses Driver%c\n", FLAG(port_caps, PCI_LMR_CAPS_DRVR));
  printf("\t\tPortSta: MargReady%c MargSoftReady%c\n",
         FLAG(port_status, PCI_LMR_PORT_STS_READY),
         FLAG(port_status, PCI_LMR_PORT_STS_SOFT_READY));
}

static void
cap_phy_16gt(struct device *d, int where)
{
  printf("Physical Layer 16.0 GT/s\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_16GT_CAP, 0x18)) {
    printf("\t\t<unreadable>\n");
    return;
  }

  u32 status = get_conf_long(d, where + PCI_16GT_STATUS);

  printf("\t\tPhy16Sta: EquComplete%c EquPhase1%c EquPhase2%c EquPhase3%c LinkEquRequest%c\n",
         FLAG(status, PCI_16GT_STATUS_EQU_COMP),
         FLAG(status, PCI_16GT_STATUS_EQU_PHASE1),
         FLAG(status, PCI_16GT_STATUS_EQU_PHASE2),
         FLAG(status, PCI_16GT_STATUS_EQU_PHASE3),
         FLAG(status, PCI_16GT_STATUS_EQU_REQ));
}

static void
cap_phy_32gt(struct device *d, int where)
{
  static const char * const mod_ts_modes[] = {
    "PCI Express",
    "Training Set Messages",
    "Alternate Protocol Negotiation"
  };
  static const char * const enh_link_ctl[] = {
    "Full Equalization required",
    "Equalization bypass to highest rate support",
    "No Equalization Needed",
    "Modified TS1/TS2 Ordered Sets supported"
  };
  char buf[48];

  printf("Physical Layer 32.0 GT/s\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_32GT_CAP, 0x1C)) {
    printf("\t\t<unreadable>\n");
    return;
  }

  u32 cap = get_conf_long(d, where + PCI_32GT_CAP);
  u32 ctl = get_conf_long(d, where + PCI_32GT_CTL);
  u32 status = get_conf_long(d, where + PCI_32GT_STATUS);

  printf("\t\tPhy32Cap: EqualizationBypass%c NoEqualizationNeeded%c\n"
         "\t\t\t  ModTsMode0%c ModTsMode1%c ModTsMode2%c\n",
         FLAG(cap, PCI_32GT_CAP_EQU_BYPASS),
         FLAG(cap, PCI_32GT_CAP_NO_EQU_NEEDED),
         FLAG(cap, PCI_32GT_CAP_MOD_TS_MODE_0),
         FLAG(cap, PCI_32GT_CAP_MOD_TS_MODE_1),
         FLAG(cap, PCI_32GT_CAP_MOD_TS_MODE_2));

  printf("\t\tPhy32Ctl: EqualizationBypassDis%c NoEqualizationNeededDis%c\n"
         "\t\t\t  Modified TS Usage Mode: %s\n",
         FLAG(ctl, PCI_32GT_CTL_EQU_BYPASS_DIS),
         FLAG(ctl, PCI_32GT_CTL_NO_EQU_NEEDED_DIS),
         TABLE(mod_ts_modes, PCI_32GT_CTL_MOD_TS_MODE(ctl), buf));

  printf("\t\tPhy32Sta: EquComplete%c EquPhase1%c EquPhase2%c EquPhase3%c LinkEquRequest%c\n"
         "\t\t\t  Received Enhanced Link Behavior Control: %s\n"
         "\t\t\t  ModTsRecv%c TxPrecodeOn%c TxPrecodeReq%c NoEqualizationNeededRecv%c\n",
         FLAG(status, PCI_32GT_STATUS_EQU_COMP),
         FLAG(status, PCI_32GT_STATUS_EQU_PHASE1),
         FLAG(status, PCI_32GT_STATUS_EQU_PHASE2),
         FLAG(status, PCI_32GT_STATUS_EQU_PHASE3),
         FLAG(status, PCI_32GT_STATUS_EQU_REQ),
         TABLE(enh_link_ctl, PCI_32GT_STATUS_RCV_ENH_LINK(status), buf),
         FLAG(status, PCI_32GT_STATUS_MOD_TS),
         FLAG(status, PCI_32GT_STATUS_TX_PRE_ON),
         FLAG(status, PCI_32GT_STATUS_TX_PRE_REQ),
         FLAG(status, PCI_32GT_STATUS_NO_EQU));
}

static void
cap_phy_64gt(struct device *d, int where)
{
  printf("Physical Layer 64.0 GT/s\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_64GT_CAP, 0x0C)) {
    printf("\t\t<unreadable>\n");
    return;
  }

  u32 status = get_conf_long(d, where + PCI_64GT_STATUS);

  printf("\t\tPhy64Sta: EquComplete%c EquPhase1%c EquPhase2%c EquPhase3%c LinkEquRequest%c\n"
         "\t\t\t  TxPrecodeOn%c TxPrecodeReq%c NoEqualizationNeededRecv%c\n",
         FLAG(status, PCI_64GT_STATUS_EQU_COMP),
         FLAG(status, PCI_64GT_STATUS_EQU_PHASE1),
         FLAG(status, PCI_64GT_STATUS_EQU_PHASE2),
         FLAG(status, PCI_64GT_STATUS_EQU_PHASE3),
         FLAG(status, PCI_64GT_STATUS_EQU_REQ),
         FLAG(status, PCI_64GT_STATUS_TX_PRE_ON),
         FLAG(status, PCI_64GT_STATUS_TX_PRE_REQ),
         FLAG(status, PCI_64GT_STATUS_NO_EQU));
}

static void
cxl_range(u64 base, u64 size, int n)
{
  u32 interleave[] = { 0, 256, 4096, 512, 1024, 2048, 8192, 16384 };
  const char * const type[] = { "Volatile", "Non-volatile", "CDAT" };
  const char * const class[] = { "DRAM", "Storage", "CDAT" };
  u16 w;

  w = (u16) size;

  size &= ~0x0fffffffULL;

  printf("\t\tRange%d: %016"PCI_U64_FMT_X"-%016"PCI_U64_FMT_X" [size=0x%"PCI_U64_FMT_X"]\n", n, base, base + size - 1, size);
  printf("\t\t\tValid%c Active%c Type=%s Class=%s interleave=%d timeout=%ds\n",
    FLAG(w, PCI_CXL_RANGE_VALID), FLAG(w, PCI_CXL_RANGE_ACTIVE),
    type[PCI_CXL_RANGE_TYPE(w)], class[PCI_CXL_RANGE_CLASS(w)],
    interleave[PCI_CXL_RANGE_INTERLEAVE(w)],
    1 << (PCI_CXL_RANGE_TIMEOUT(w) * 2));
}

static void
dvsec_cxl_device(struct device *d, int rev, int where, int len)
{
  u32 cache_size, cache_unit_size;
  u64 range_base, range_size;
  u16 w;

  /* Legacy 1.1 revs aren't handled */
  if (rev == 0)
    return;

  if (rev >= 1 && len >= PCI_CXL_DEV_LEN)
    {
      w = get_conf_word(d, where + PCI_CXL_DEV_CAP);
      printf("\t\tCXLCap:\tCache%c IO%c Mem%c MemHWInit%c HDMCount %d Viral%c\n",
	FLAG(w, PCI_CXL_DEV_CAP_CACHE), FLAG(w, PCI_CXL_DEV_CAP_IO), FLAG(w, PCI_CXL_DEV_CAP_MEM),
	FLAG(w, PCI_CXL_DEV_CAP_MEM_HWINIT), PCI_CXL_DEV_CAP_HDM_CNT(w), FLAG(w, PCI_CXL_DEV_CAP_VIRAL));

      w = get_conf_word(d, where + PCI_CXL_DEV_CTRL);
      printf("\t\tCXLCtl:\tCache%c IO%c Mem%c CacheSFCov %d CacheSFGran %d CacheClean%c Viral%c\n",
	FLAG(w, PCI_CXL_DEV_CTRL_CACHE), FLAG(w, PCI_CXL_DEV_CTRL_IO), FLAG(w, PCI_CXL_DEV_CTRL_MEM),
	PCI_CXL_DEV_CTRL_CACHE_SF_COV(w), PCI_CXL_DEV_CTRL_CACHE_SF_GRAN(w), FLAG(w, PCI_CXL_DEV_CTRL_CACHE_CLN),
	FLAG(w, PCI_CXL_DEV_CTRL_VIRAL));

      w = get_conf_word(d, where + PCI_CXL_DEV_STATUS);
      printf("\t\tCXLSta:\tViral%c\n", FLAG(w, PCI_CXL_DEV_STATUS_VIRAL));

      w = get_conf_word(d, where + PCI_CXL_DEV_CTRL2);
      printf("\t\tCXLCtl2:\tDisableCaching%c InitCacheWB&Inval%c InitRst%c RstMemClrEn%c",
	FLAG(w, PCI_CXL_DEV_CTRL2_DISABLE_CACHING),
	FLAG(w, PCI_CXL_DEV_CTRL2_INIT_WB_INVAL),
	FLAG(w, PCI_CXL_DEV_CTRL2_INIT_CXL_RST),
	FLAG(w, PCI_CXL_DEV_CTRL2_INIT_CXL_RST_CLR_EN));
      if (rev >= 2)
	  printf(" DesiredVolatileHDMStateAfterHotReset%c", FLAG(w, PCI_CXL_DEV_CTRL2_INIT_CXL_HDM_STATE_HOTRST));
      printf("\n");

      w = get_conf_word(d, where + PCI_CXL_DEV_STATUS2);
      printf("\t\tCXLSta2:\tResetComplete%c ResetError%c PMComplete%c\n",
	FLAG(w, PCI_CXL_DEV_STATUS_RC), FLAG(w,PCI_CXL_DEV_STATUS_RE), FLAG(w, PCI_CXL_DEV_STATUS_PMC));

      w = get_conf_word(d, where + PCI_CXL_DEV_CAP2);
      printf("\t\tCXLCap2:\t");
      cache_unit_size = BITS(w, 0, 4);
      cache_size = BITS(w, 8, 8);
      switch (cache_unit_size)
	{
	case PCI_CXL_DEV_CAP2_CACHE_1M:
	  printf("Cache Size: %08x\n", cache_size * (1<<20));
	  break;
	case PCI_CXL_DEV_CAP2_CACHE_64K:
	  printf("Cache Size: %08x\n", cache_size * (64<<10));
	  break;
	case PCI_CXL_DEV_CAP2_CACHE_UNK:
	  printf("Cache Size Not Reported\n");
	  break;
	default:
	  printf("Cache Size: %d of unknown unit size (%d)\n", cache_size, cache_unit_size);
	  break;
	}

      range_size = (u64) get_conf_long(d, where + PCI_CXL_DEV_RANGE1_SIZE_HI) << 32;
      range_size |= get_conf_long(d, where + PCI_CXL_DEV_RANGE1_SIZE_LO);
      range_base = (u64) get_conf_long(d, where + PCI_CXL_DEV_RANGE1_BASE_HI) << 32;
      range_base |= get_conf_long(d, where + PCI_CXL_DEV_RANGE1_BASE_LO);
      cxl_range(range_base, range_size, 1);

      range_size = (u64) get_conf_long(d, where + PCI_CXL_DEV_RANGE2_SIZE_HI) << 32;
      range_size |= get_conf_long(d, where + PCI_CXL_DEV_RANGE2_SIZE_LO);
      range_base = (u64) get_conf_long(d, where + PCI_CXL_DEV_RANGE2_BASE_HI) << 32;
      range_base |= get_conf_long(d, where + PCI_CXL_DEV_RANGE2_BASE_LO);
      cxl_range(range_base, range_size, 2);
    }

  if (rev >= 2 && len >= PCI_CXL_DEV_LEN_REV2)
    {
      w = get_conf_word(d, where + PCI_CXL_DEV_CAP3);
      printf("\t\tCXLCap3:\tDefaultVolatile HDM State After:\tColdReset%c WarmReset%c HotReset%c HotResetConfigurability%c\n",
	FLAG(w, PCI_CXL_DEV_CAP3_HDM_STATE_RST_COLD),
	FLAG(w, PCI_CXL_DEV_CAP3_HDM_STATE_RST_WARM),
	FLAG(w, PCI_CXL_DEV_CAP3_HDM_STATE_RST_HOT),
	FLAG(w, PCI_CXL_DEV_CAP3_HDM_STATE_RST_HOT_CFG));
    }

  // Unparsed data
  if (len > PCI_CXL_DEV_LEN_REV2)
    printf("\t\t<?>\n");
}

static void
dvsec_cxl_port(struct device *d, int where, int len)
{
  u16 w, m1, m2;
  u8 b1, b2;

  if (len < PCI_CXL_PORT_EXT_LEN)
    return;

  w = get_conf_word(d, where + PCI_CXL_PORT_EXT_STATUS);
  printf("\t\tCXLPortSta:\tPMComplete%c\n", FLAG(w, PCI_CXL_PORT_EXT_STATUS));

  w = get_conf_word(d, where + PCI_CXL_PORT_CTRL);
  printf("\t\tCXLPortCtl:\tUnmaskSBR%c UnmaskLinkDisable%c AltMem%c AltBME%c ViralEnable%c\n",
    FLAG(w, PCI_CXL_PORT_UNMASK_SBR), FLAG(w, PCI_CXL_PORT_UNMASK_LINK),
    FLAG(w, PCI_CXL_PORT_ALT_MEMORY), FLAG(w, PCI_CXL_PORT_ALT_BME),
    FLAG(w, PCI_CXL_PORT_VIRAL_EN));

  b1 = get_conf_byte(d, where + PCI_CXL_PORT_ALT_BUS_BASE);
  b2 = get_conf_byte(d, where + PCI_CXL_PORT_ALT_BUS_LIMIT);
  printf("\t\tAlternateBus:\t%02x-%02x\n", b1, b2);
  m1 = get_conf_word(d, where + PCI_CXL_PORT_ALT_MEM_BASE);
  m2 = get_conf_word(d, where + PCI_CXL_PORT_ALT_MEM_LIMIT);
  printf("\t\tAlternateBus:\t%04x-%04x\n", m1, m2);
}

static void
dvsec_cxl_register_locator(struct device *d, int where, int len)
{
  static const char * const id_names[] = {
    "empty",
    "component registers",
    "BAR virtualization",
    "CXL device registers",
    "CPMU registers",
  };

  for (int i=0; ; i++)
    {
      int pos = where + PCI_CXL_RL_BLOCK1_LO + 8*i;
      if (pos + 7 >= where + len)
	break;

      u32 lo = get_conf_long(d, pos);
      u32 hi = get_conf_long(d, pos + 4);

      unsigned int bir = BITS(lo, 0, 3);
      unsigned int block_id = BITS(lo, 8, 8);
      u64 base = (BITS(lo, 16, 16) << 16) | ((u64) hi << 32);

      if (!block_id)
	continue;

      const char *id_name;
      if (block_id < sizeof(id_names) / sizeof(*id_names))
	id_name = id_names[block_id];
      else if (block_id == 0xff)
	id_name = "vendor-specific";
      else
	id_name = "<?>";

      printf("\t\tBlock%d: BIR: bar%d, ID: %s, offset: %016" PCI_U64_FMT_X "\n", i + 1, bir, id_name, base);
    }
}

static void
dvsec_cxl_gpf_device(struct device *d, int where)
{
  u32 l;
  u16 w, duration;
  u8 time_base, time_scale;

  w = get_conf_word(d, where + PCI_CXL_GPF_DEV_PHASE2_DUR);
  time_base = BITS(w, 0, 4);
  time_scale = BITS(w, 8, 4);

  switch (time_scale)
    {
      case PCI_CXL_GPF_DEV_100US:
      case PCI_CXL_GPF_DEV_100MS:
        duration = time_base * 100;
        break;
      case PCI_CXL_GPF_DEV_10US:
      case PCI_CXL_GPF_DEV_10MS:
      case PCI_CXL_GPF_DEV_10S:
        duration = time_base * 10;
        break;
      case PCI_CXL_GPF_DEV_1US:
      case PCI_CXL_GPF_DEV_1MS:
      case PCI_CXL_GPF_DEV_1S:
        duration = time_base;
        break;
      default:
        /* Reserved */
        printf("\t\tReserved time scale encoding %x\n", time_scale);
        duration = time_base;
    }

  printf("\t\tGPF Phase 2 Duration: %u%s\n", duration,
      (time_scale < PCI_CXL_GPF_DEV_1MS) ? "us":
      (time_scale < PCI_CXL_GPF_DEV_1S) ? "ms" :
      (time_scale <= PCI_CXL_GPF_DEV_10S) ? "s" : "<?>");

  l = get_conf_long(d, where + PCI_CXL_GPF_DEV_PHASE2_POW);
  printf("\t\tGPF Phase 2 Power: %umW\n", (unsigned int)l);
}

static void
dvsec_cxl_gpf_port(struct device *d, int where)
{
  u16 w, timeout;
  u8 time_base, time_scale;

  w = get_conf_word(d, where + PCI_CXL_GPF_PORT_PHASE1_CTRL);
  time_base = BITS(w, 0, 4);
  time_scale = BITS(w, 8, 4);

  switch (time_scale)
    {
      case PCI_CXL_GPF_PORT_100US:
      case PCI_CXL_GPF_PORT_100MS:
        timeout = time_base * 100;
        break;
      case PCI_CXL_GPF_PORT_10US:
      case PCI_CXL_GPF_PORT_10MS:
      case PCI_CXL_GPF_PORT_10S:
        timeout = time_base * 10;
        break;
      case PCI_CXL_GPF_PORT_1US:
      case PCI_CXL_GPF_PORT_1MS:
      case PCI_CXL_GPF_PORT_1S:
        timeout = time_base;
        break;
      default:
        /* Reserved */
        printf("\t\tReserved time scale encoding %x\n", time_scale);
        timeout = time_base;
    }

  printf("\t\tGPF Phase 1 Timeout: %d%s\n", timeout,
      (time_scale < PCI_CXL_GPF_PORT_1MS) ? "us":
      (time_scale < PCI_CXL_GPF_PORT_1S) ? "ms" :
      (time_scale <= PCI_CXL_GPF_PORT_10S) ? "s" : "<?>");

  w = get_conf_word(d, where + PCI_CXL_GPF_PORT_PHASE2_CTRL);
  time_base = BITS(w, 0, 4);
  time_scale = BITS(w, 8, 4);

  switch (time_scale)
    {
      case PCI_CXL_GPF_PORT_100US:
      case PCI_CXL_GPF_PORT_100MS:
        timeout = time_base * 100;
        break;
      case PCI_CXL_GPF_PORT_10US:
      case PCI_CXL_GPF_PORT_10MS:
      case PCI_CXL_GPF_PORT_10S:
        timeout = time_base * 10;
        break;
      case PCI_CXL_GPF_PORT_1US:
      case PCI_CXL_GPF_PORT_1MS:
      case PCI_CXL_GPF_PORT_1S:
        timeout = time_base;
        break;
      default:
        /* Reserved */
        printf("\t\tReserved time scale encoding %x\n", time_scale);
        timeout = time_base;
    }

  printf("\t\tGPF Phase 2 Timeout: %d%s\n", timeout,
      (time_scale < PCI_CXL_GPF_PORT_1MS) ? "us":
      (time_scale < PCI_CXL_GPF_PORT_1S) ? "ms" :
      (time_scale <= PCI_CXL_GPF_PORT_10S) ? "s" : "<?>");
}

static void
dvsec_cxl_flex_bus(struct device *d, int where, int rev, int len)
{
  u16 w;
  u32 l, data;

  // Sanity check: Does the length correspond to its revision?
  switch (rev) {
    case 0:
      if (len != PCI_CXL_FB_MOD_TS_DATA)
        printf("\t\t<Wrong length for Revision %d>\n", rev);
      break;
    case 1:
      if (len != PCI_CXL_FB_PORT_CAP2)
        printf("\t\t<Wrong length for Revision %d>\n", rev);
      break;
    case 2:
      if (len != PCI_CXL_FB_NEXT_UNSUPPORTED)
        printf("\t\t<Wrong length for Revision %d>\n", rev);
      break;
    default:
      break;
  }

  // From Rev 0
  w = get_conf_word(d, where + PCI_CXL_FB_PORT_CAP);
  printf("\t\tFBCap:\tCache%c IO%c Mem%c 68BFlit%c MltLogDev%c",
      FLAG(w, PCI_CXL_FB_CAP_CACHE), FLAG(w, PCI_CXL_FB_CAP_IO),
      FLAG(w, PCI_CXL_FB_CAP_MEM), FLAG(w, PCI_CXL_FB_CAP_68B_FLIT),
      FLAG(w, PCI_CXL_FB_CAP_MULT_LOG_DEV));

  if (rev > 1)
    printf(" 256BFlit%c PBRFlit%c",
        FLAG(w, PCI_CXL_FB_CAP_256B_FLIT), FLAG(w, PCI_CXL_FB_CAP_PBR_FLIT));

  w = get_conf_word(d, where + PCI_CXL_FB_PORT_CTRL);
  printf("\n\t\tFBCtl:\tCache%c IO%c Mem%c SynHdrByp%c DrftBuf%c 68BFlit%c MltLogDev%c RCD%c Retimer1%c Retimer2%c",
      FLAG(w, PCI_CXL_FB_CTRL_CACHE), FLAG(w, PCI_CXL_FB_CTRL_IO),
      FLAG(w, PCI_CXL_FB_CTRL_MEM), FLAG(w, PCI_CXL_FB_CTRL_SYNC_HDR_BYP),
      FLAG(w, PCI_CXL_FB_CTRL_DRFT_BUF), FLAG(w, PCI_CXL_FB_CTRL_68B_FLIT),
      FLAG(w, PCI_CXL_FB_CTRL_MULT_LOG_DEV), FLAG(w, PCI_CXL_FB_CTRL_RCD),
      FLAG(w, PCI_CXL_FB_CTRL_RETIMER1), FLAG(w, PCI_CXL_FB_CTRL_RETIMER2));

  if (rev > 1)
    printf(" 256BFlit%c PBRFlit%c",
        FLAG(w, PCI_CXL_FB_CTRL_256B_FLIT), FLAG(w, PCI_CXL_FB_CTRL_PBR_FLIT));

  w = get_conf_word(d, where + PCI_CXL_FB_PORT_STATUS);
  printf("\n\t\tFBSta:\tCache%c IO%c Mem%c SynHdrByp%c DrftBuf%c 68BFlit%c MltLogDev%c",
      FLAG(w, PCI_CXL_FB_STAT_CACHE), FLAG(w, PCI_CXL_FB_STAT_IO),
      FLAG(w, PCI_CXL_FB_STAT_MEM), FLAG(w, PCI_CXL_FB_STAT_SYNC_HDR_BYP),
      FLAG(w, PCI_CXL_FB_STAT_DRFT_BUF), FLAG(w, PCI_CXL_FB_STAT_68B_FLIT),
      FLAG(w, PCI_CXL_FB_STAT_MULT_LOG_DEV));

  if (rev > 1)
    printf(" 256BFlit%c PBRFlit%c",
        FLAG(w, PCI_CXL_FB_STAT_256B_FLIT), FLAG(w, PCI_CXL_FB_STAT_PBR_FLIT));
  printf("\n");

  // From Rev 1
  if (rev >= 1)
    {
      l = get_conf_long(d, where + PCI_CXL_FB_MOD_TS_DATA);
      data = BITS(l, 0, 24);
      printf("\t\tFBModTS:\tReceived FB Data: %06x\n", (unsigned int)data);
    }

  // From Rev 2
  if (rev >= 2)
    {
      u8 nop;

      l = get_conf_long(d, where + PCI_CXL_FB_PORT_CAP2);
      printf("\t\tFBCap2:\tNOPHint%c\n", FLAG(l, PCI_CXL_FB_CAP2_NOP_HINT));

      l = get_conf_long(d, where + PCI_CXL_FB_PORT_CTRL2);
      printf("\t\tFBCtl2:\tNOPHint%c\n", FLAG(l, PCI_CXL_FB_CTRL2_NOP_HINT));

      l = get_conf_long(d, where + PCI_CXL_FB_PORT_STATUS2);
      nop = BITS(l, 0, 2);
      printf("\t\tFBSta2:\tNOPHintInfo: %x\n", nop);
    }

  // Unparsed data
  if (len > PCI_CXL_FB_LEN)
    printf("\t\t<?>\n");
}

static void
dvsec_cxl_mld(struct device *d, int where)
{
  u16 w;

  w = get_conf_word(d, where + PCI_CXL_MLD_NUM_LD);

  /* Encodings greater than 16 are reserved */
  if (w && w <= PCI_CXL_MLD_MAX_LD)
    printf("\t\tNumLogDevs: %d\n", w);
}

static void
dvsec_cxl_function_map(struct device *d, int where)
{

  printf("\t\tFuncMap 0: %08x\n",
      (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_0)));

  printf("\t\tFuncMap 1: %08x\n",
    (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_1)));

  printf("\t\tFuncMap 2: %08x\n",
    (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_2)));

  printf("\t\tFuncMap 3: %08x\n",
      (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_3)));

  printf("\t\tFuncMap 4: %08x\n",
      (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_4)));

  printf("\t\tFuncMap 5: %08x\n",
      (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_5)));

  printf("\t\tFuncMap 6: %08x\n",
      (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_6)));

  printf("\t\tFuncMap 7: %08x\n",
      (unsigned int)(get_conf_word(d, where + PCI_CXL_FUN_MAP_REG_7)));
}

static void
cap_dvsec_cxl(struct device *d, int id, int rev, int where, int len)
{
  printf(": CXL\n");
  if (verbose < 2)
    return;

  if (!config_fetch(d, where, len))
    return;

  switch (id)
    {
    case 0:
      printf("\t\tPCIe DVSEC for CXL Devices\n");
      dvsec_cxl_device(d, rev, where, len);
      break;
    case 2:
      printf("\t\tNon-CXL Function Map DVSEC\n");
      dvsec_cxl_function_map(d, where);
      break;
    case 3:
      printf("\t\tCXL Extensions DVSEC for Ports\n");
      dvsec_cxl_port(d, where, len);
      break;
    case 4:
      printf("\t\tGPF DVSEC for CXL Ports\n");
      dvsec_cxl_gpf_port(d, where);
      break;
    case 5:
      printf("\t\tGPF DVSEC for CXL Devices\n");
      dvsec_cxl_gpf_device(d, where);
      break;
    case 7:
      printf("\t\tPCIe DVSEC for Flex Bus Port\n");
      dvsec_cxl_flex_bus(d, where, rev, len);
      break;
    case 8:
      printf("\t\tRegister Locator DVSEC\n");
      dvsec_cxl_register_locator(d, where, len);
      break;
    case 9:
      printf("\t\tMLD DVSEC\n");
      dvsec_cxl_mld(d, where);
      break;
    case 0xa:
      printf("\t\tPCIe DVSEC for Test Capability <?>\n");
      break;
    default:
      printf("\t\tUnknown ID %04x\n", id);
    }
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
    cap_dvsec_cxl(d, id, rev, where, len);
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
  printf("Requester%c Responder%c Root%c\n",
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
  printf("Enabled%c RootSelected%c\n",
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

static void
cap_doe(struct device *d, int where)
{
  u32 l;

  printf("Data Object Exchange\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_DOE_CAP, 0x14))
    {
      printf("\t\t<unreadable>\n");
      return;
    }

  l = get_conf_long(d, where + PCI_DOE_CAP);
  printf("\t\tDOECap: IntSup%c\n",
	 FLAG(l, PCI_DOE_CAP_INT_SUPP));
  if (l & PCI_DOE_CAP_INT_SUPP)
    printf("\t\t\tIntMsgNum %d\n",
	   PCI_DOE_CAP_INT_MSG(l));

  l = get_conf_long(d, where + PCI_DOE_CTL);
  printf("\t\tDOECtl: IntEn%c\n",
	 FLAG(l, PCI_DOE_CTL_INT));

  l = get_conf_long(d, where + PCI_DOE_STS);
  printf("\t\tDOESta: Busy%c IntSta%c Error%c ObjectReady%c\n",
	 FLAG(l, PCI_DOE_STS_BUSY),
	 FLAG(l, PCI_DOE_STS_INT),
	 FLAG(l, PCI_DOE_STS_ERROR),
	 FLAG(l, PCI_DOE_STS_OBJECT_READY));
}

static const char *offstr(char *buf, u32 off)
{
    if (verbose < 3)
        return "";

    sprintf(buf, "[%x]", off);
    return buf;
}

static const char *ide_alg(char *buf, size_t len, u32 l)
{
    const char *algo[] = { "AES-GCM-256-96b" }; // AES-GCM 256 key size, 96b MAC

    if (l == 0)
        snprintf(buf, len, "%s", algo[l]);
    else
        snprintf(buf, len, "%s", "reserved");
    return buf;
}

static void
cap_ide(struct device *d, int where)
{
    const char * const hdr_enc_mode[] = { "no", "17:2", "25:2", "33:2", "41:2" };
    const char * const stream_state[] = { "insecure", "reserved", "secure" };
    const char * const aggr[] = { "-", "=2", "=4", "=8" };
    u32 l, l2, linknum = 0, selnum = 0, addrnum, off, i, j;
    char buf1[16], buf2[16], offs[16];

    printf("Integrity & Data Encryption\n");

    if (verbose < 2)
        return;

    if (!config_fetch(d, where + PCI_IDE_CAP, 8))
      {
        printf("\t\t<unreadable>\n");
        return;
      }

    l = get_conf_long(d, where + PCI_IDE_CAP);
    if (l & PCI_IDE_CAP_LINK_IDE_SUPP)
        linknum = PCI_IDE_CAP_LINK_TC_NUM(l) + 1;
    if (l & PCI_IDE_CAP_SELECTIVE_IDE_SUPP)
        selnum = PCI_IDE_CAP_SELECTIVE_STREAMS_NUM(l) + 1;

    printf("\t\tIDECap: Lnk=%d Sel=%d FlowThru%c PartHdr%c Aggr%c PCPC%c IDE_KM%c SelCfg%c Alg='%s' TCs=%d TeeLim%c XT%c\n",
      linknum,
      selnum,
      FLAG(l, PCI_IDE_CAP_FLOWTHROUGH_IDE_SUPP),
      FLAG(l, PCI_IDE_CAP_PARTIAL_HEADER_ENC_SUPP),
      FLAG(l, PCI_IDE_CAP_AGGREGATION_SUPP),
      FLAG(l, PCI_IDE_CAP_PCRC_SUPP),
      FLAG(l, PCI_IDE_CAP_IDE_KM_SUPP),
      FLAG(l, PCI_IDE_CAP_SEL_CFG_SUPP),
      ide_alg(buf2, sizeof(buf2), PCI_IDE_CAP_ALG(l)),
      PCI_IDE_CAP_LINK_TC_NUM(l) + 1,
      FLAG(l, PCI_IDE_CAP_TEE_LIMITED_SUPP),
      FLAG(l, PCI_IDE_CAP_XT_SUPP)
      );

    l = get_conf_long(d, where + PCI_IDE_CTL);
    printf("\t\tIDECtl: FTEn%c\n",
      FLAG(l, PCI_IDE_CTL_FLOWTHROUGH_IDE));

    // The rest of the capability is variable length arrays
    off = where + PCI_IDE_LINK_STREAM;

    // Link IDE Register Block repeated 0 to 8 times
    if (linknum)
      {
        if (!config_fetch(d, off, 8 * linknum))
          {
            printf("\t\t<unreadable>\n");
            return;
          }
        for (i = 0; i < linknum; ++i)
          {
            // Link IDE Stream Control Register
            l = get_conf_long(d, off);
            printf("\t\t%sLinkIDE#%d Ctl: En%c XT%c NPR%s PR%s CPL%s PCRC%c HdrEnc=%s Alg='%s' TC%d ID%d\n",
              offstr(offs, off),
              i,
              FLAG(l, PCI_IDE_LINK_CTL_EN),
              FLAG(l, PCI_IDE_LINK_CTL_XT),
              aggr[PCI_IDE_LINK_CTL_TX_AGGR_NPR(l)],
              aggr[PCI_IDE_LINK_CTL_TX_AGGR_PR(l)],
              aggr[PCI_IDE_LINK_CTL_TX_AGGR_CPL(l)],
              FLAG(l, PCI_IDE_LINK_CTL_EN),
              TABLE(hdr_enc_mode, PCI_IDE_LINK_CTL_PART_ENC(l), buf1),
              ide_alg(buf2, sizeof(buf2), PCI_IDE_LINK_CTL_ALG(l)),
              PCI_IDE_LINK_CTL_TC(l),
              PCI_IDE_LINK_CTL_ID(l)
              );
            off += 4;

            /* Link IDE Stream Status Register */
            l = get_conf_long(d, off);
            printf("\t\t%sLinkIDE#%d Sta: Status=%s RecvChkFail%c\n",
              offstr(offs, off),
              i,
              TABLE(stream_state, PCI_IDE_LINK_STS_STATUS(l), buf1),
              FLAG(l, PCI_IDE_LINK_STS_RECVD_INTEGRITY_CHECK));
            off += 4;
          }
      }

    for (i = 0; i < selnum; ++i)
      {
        // Fetching Selective IDE Stream Capability/Control/Status/RID1/RID2
        if (!config_fetch(d, off, 20))
          {
            printf("\t\t<unreadable>\n");
            return;
          }

        // Selective IDE Stream Capability Register
        l = get_conf_long(d, off);
        printf("\t\t%sSelectiveIDE#%d Cap: RID#=%d\n",
          offstr(offs, off),
          i,
          PCI_IDE_SEL_CAP_BLOCKS_NUM(l));
        off += 4;
        addrnum = PCI_IDE_SEL_CAP_BLOCKS_NUM(l);

        // Selective IDE Stream Control Register
        l = get_conf_long(d, off);

        printf("\t\t%sSelectiveIDE#%d Ctl: En%c XT%c NPR%s PR%s CPL%s PCRC%c CFG%c HdrEnc=%s Alg='%s' TC%d TeeLim%c ID%d%s\n",
          offstr(offs, off),
          i,
          FLAG(l, PCI_IDE_SEL_CTL_EN),
          FLAG(l, PCI_IDE_SEL_CTL_XT),
          aggr[PCI_IDE_SEL_CTL_TX_AGGR_NPR(l)],
          aggr[PCI_IDE_SEL_CTL_TX_AGGR_PR(l)],
          aggr[PCI_IDE_SEL_CTL_TX_AGGR_CPL(l)],
          FLAG(l, PCI_IDE_SEL_CTL_PCRC_EN),
          FLAG(l, PCI_IDE_SEL_CTL_CFG_EN),
          TABLE(hdr_enc_mode, PCI_IDE_SEL_CTL_PART_ENC(l), buf1),
          ide_alg(buf2, sizeof(buf2), PCI_IDE_SEL_CTL_ALG(l)),
          PCI_IDE_SEL_CTL_TC(l),
          FLAG(l, PCI_IDE_SEL_CTL_TEE_LIMITED),
          PCI_IDE_SEL_CTL_ID(l),
          (l & PCI_IDE_SEL_CTL_DEFAULT) ? " Default" : ""
          );
        off += 4;

        // Selective IDE Stream Status Register
        l = get_conf_long(d, off);
        printf("\t\t%sSelectiveIDE#%d Sta: %s RecvChkFail%c\n",
          offstr(offs, off),
          i ,
          TABLE(stream_state, PCI_IDE_SEL_STS_STATUS(l), buf1),
          FLAG(l, PCI_IDE_SEL_STS_RECVD_INTEGRITY_CHECK));
        off += 4;

        // IDE RID Association Registers
        l = get_conf_long(d, off);
        l2 = get_conf_long(d, off + 4);

        printf("\t\t%sSelectiveIDE#%d RID: Valid%c Base=%x Limit=%x SegBase=%x\n",
          offstr(offs, off),
          i,
          FLAG(l2, PCI_IDE_SEL_RID_2_VALID),
          PCI_IDE_SEL_RID_2_BASE(l2),
          PCI_IDE_SEL_RID_1_LIMIT(l),
          PCI_IDE_SEL_RID_2_SEG_BASE(l2));
        off += 8;

        if (!config_fetch(d, off, addrnum * 12))
          {
            printf("\t\t<unreadable>\n");
            return;
          }

        // IDE Address Association Registers
        for (j = 0; j < addrnum; ++j)
          {
            u64 limit, base;

            l = get_conf_long(d, off);
            limit = get_conf_long(d, off + 4);
            limit <<= 32;
            limit |= (PCI_IDE_SEL_ADDR_1_LIMIT_LOW(l) << 20) | 0xFFFFF;
            base = get_conf_long(d, off + 8);
            base <<= 32;
            base |= PCI_IDE_SEL_ADDR_1_BASE_LOW(l) << 20;
            printf("\t\t%sSelectiveIDE#%d RID#%d: Valid%c Base=%" PCI_U64_FMT_X " Limit=%" PCI_U64_FMT_X "\n",
              offstr(offs, off),
              i,
              j,
              FLAG(l, PCI_IDE_SEL_ADDR_1_VALID),
              base,
              limit);
            off += 12;
          }
      }
}

static const char *l0p_exit_latency(int value)
{
  static const char * const latencies[] = {
    "Less than 1us",
    "1us to less than 2us",
    "2us to less than 4us",
    "4us to less than 8us",
    "8us to less than 16us",
    "16us to less than 32us",
    "32us-64us",
    "More than 64us"
  };

  if (value >= 0 && value <= 7)
    return latencies[value];
  return "Unknown";
}

static const char *link_width_str(char *buf, size_t buflen, int width)
{
  switch (width)
    {
      case 0:
        return "x1";
      case 1:
        return "x2";
      case 2:
        return "x4";
      case 3:
        return "x8";
      case 4:
        return "x16";
      case 7:
        return "Dynamic";
      default:
        snprintf(buf, buflen, "Unknown (%d)", width);
        return buf;
    }
}

static void
cap_dev3(struct device *d, int where)
{
  u32 devcap3;
  u16 devctl3, devsta3;
  char buf[16];

  printf("Device 3\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_DEV3_DEVCAP3, 4))
    return;
  devcap3 = get_conf_long(d, where + PCI_DEV3_DEVCAP3);

  printf("\t\tDevCap3: DMWr Request Routing%c, 14-Bit Tag Completer%c, 14-Bit Tag Requester%c\n"
         "\t\t\t L0p%c",
         FLAG(devcap3, PCI_DEV3_DEVCAP3_DMWR_REQ),
         FLAG(devcap3, PCI_DEV3_DEVCAP3_14BIT_TAG_COMP),
         FLAG(devcap3, PCI_DEV3_DEVCAP3_14BIT_TAG_REQ),
         FLAG(devcap3, PCI_DEV3_DEVCAP3_L0P_SUPP));

  if (devcap3 & PCI_DEV3_DEVCAP3_L0P_SUPP)
    printf(", Port L0p Exit Latency: %s, Retimer L0p Exit Latency: %s",
           l0p_exit_latency(PCI_DEV3_DEVCAP3_PORT_L0P_EXIT(devcap3)),
           l0p_exit_latency(PCI_DEV3_DEVCAP3_RETIMER_L0P_EXIT(devcap3)));

  printf("\n\t\t\t UIO Mem RdWr Completer%c, UIO Mem RdWr Requester%c\n",
         FLAG(devcap3, PCI_DEV3_DEVCAP3_UIO_MEM_RDWR_COMP),
         FLAG(devcap3, PCI_DEV3_DEVCAP3_UIO_MEM_RDWR_REQ));

  if (!config_fetch(d, where + PCI_DEV3_DEVCTL3, 2))
    return;
  devctl3 = get_conf_word(d, where + PCI_DEV3_DEVCTL3);

  printf("\t\tDevCtl3: DMWr Requester%c, DMWr Egress Blocking%c, 14-Bit Tag Requester%c\n"
         "\t\t\t L0p%c, Target Link Width: %s\n"
         "\t\t\t UIO Mem RdWr Requester%c, UIO Request 256B Boundary%c\n",
         FLAG(devctl3, PCI_DEV3_DEVCTL3_DMWR_REQ_EN),
         FLAG(devctl3, PCI_DEV3_DEVCTL3_DMWR_EGRESS_BLK),
         FLAG(devctl3, PCI_DEV3_DEVCTL3_14BIT_TAG_REQ_EN),
         FLAG(devctl3, PCI_DEV3_DEVCTL3_L0P_EN),
         link_width_str(buf, sizeof(buf), PCI_DEV3_DEVCTL3_TARGET_LINK_WIDTH(devctl3)),
         FLAG(devctl3, PCI_DEV3_DEVCTL3_UIO_MEM_RDWR_REQ_EN),
         FLAG(~devctl3, PCI_DEV3_DEVCTL3_UIO_REQ_256B_DIS));

  if (!config_fetch(d, where + PCI_DEV3_DEVSTA3, 2))
    return;
  devsta3 = get_conf_word(d, where + PCI_DEV3_DEVSTA3);

  printf("\t\tDevSta3: Initial Link Width: %s, Segment Captured%c, Remote L0p%c\n",
         link_width_str(buf, sizeof(buf), PCI_DEV3_DEVSTA3_INIT_LINK_WIDTH(devsta3)),
         FLAG(devsta3, PCI_DEV3_DEVSTA3_SEGMENT_CAPTURED),
         FLAG(devsta3, PCI_DEV3_DEVSTA3_REMOTE_L0P_SUPP));
}

static const char *mmio_rbl_bid(char *buf, size_t buflen, u8 bid)
{
  switch (bid)
    {
      case 0x00:
        return "Empty";
      case 0x01:
        return "MCAP";
      case 0xFF:
        return "MDVS";
      default:
        snprintf(buf, buflen, "Reserved (%u)", bid);
        return buf;
    }
}

static void
cap_mmio_rbl(struct device *d, int where)
{
  char buf[16];

  printf("MMIO Register Block Locator\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_MRBL_CAP, 0x0C))
    {
      printf("\t\t<unreadable>\n");
      return;
    }

  u32 cap = get_conf_long(d, where + PCI_MRBL_CAP);
  u32 mrbllen = PCI_MRBL_CAP_STRUCT_LEN(cap);

  if (!config_fetch(d, where + PCI_MRBL_REG, mrbllen))
    {
      printf("\t\t<unreadable>\n");
      return;
    }

  u32 num_mrbl = (mrbllen / 8) - 1;

  for (u32 i = 0; i < num_mrbl; i++)
    {
      unsigned int pos = where + PCI_MRBL_REG + i * PCI_MRBL_REG_SIZE;
      if (!config_fetch(d, pos, PCI_MRBL_REG_SIZE))
	{
	  printf("\t\t<unreadable>\n");
	  return;
	}

      u32 lo = get_conf_long(d, pos);
      u32 hi = get_conf_long(d, pos + 0x04);

      u64 offs = ((u64) hi << 32) | PCI_MRBL_LOC_OFF_LOW(lo);

      printf("\t\tLocator%u: BIR: BAR%u, ID: %s, offset: %016" PCI_U64_FMT_X "\n",
	     i,
	     PCI_MRBL_LOC_BIR(lo),
	     mmio_rbl_bid(buf, sizeof(buf), PCI_MRBL_LOC_BID(lo)),
	     offs);
    }
}

static const char *flit_ei_flit_type_str(char *buf, size_t buflen, u8 type)
{
  switch (type)
    {
      case 0b000:
        return "any";
      case 0b001:
        return "any non-IDLE";
      case 0b010:
        return "only payload";
      case 0b011:
        return "only NOP";
      case 0b100:
        return "only IDLE";
      case 0b101:
        return "only payload+seq";
      case 0b110:
        return "only payload+1seq";
      default:
        snprintf(buf, buflen, "Unknown (%u)", type);
        return buf;
    }
}

static const char *flit_ei_consec_str(char *buf, size_t buflen, u8 consec)
{
  switch (consec)
    {
      case 0b000:
        return "none";
      case 0b001:
      case 0b010:
      case 0b011:
      case 0b101:
      case 0b110:
        snprintf(buf, buflen, "%u", consec);
        return buf;
      case 0b111:
        return "pseudo-random";
      default:
        snprintf(buf, buflen, "Unknown (%u)", consec);
        return buf;
    }
}

static const char *flit_ei_err_type_str(char *buf, size_t buflen, u8 type)
{
  switch (type)
    {
      case 0b00:
        return "random";
      case 0b01:
        return "correctable single group";
      case 0b10:
        return "correctable three groups";
      case 0b011:
        return "uncorrectable";
      default:
        snprintf(buf, buflen, "Unknown (%u)", type);
        return buf;
    }
}

static const char *flit_ei_sts_str(char *buf, size_t buflen, u8 sts)
{
  switch (sts)
    {
      case 0b00:
        return "not started";
      case 0b001:
        return "started";
      case 0b010:
        return "completed";
      case 0b011:
        return "error";
      default:
        snprintf(buf, buflen, "Unknown (%u)", sts);
        return buf;
    }
}

static void
cap_flit_ei(struct device *d, int where)
{
  char buf0[16], buf1[16], buf2[16];

  printf("Flit Error Injection\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_FLIT_EI_CAP, 32))
    return;

  u32 flit_ei_ctl1 = get_conf_long(d, where + PCI_FLIT_EI_CTL1);

  printf("\t\tFlitEiCtl1:   En%c Tx%c Rx%c 2.5GT/s%c 5.0GT/s%c 8.0GT/s%c 16.0GT/s%c 32.0GT/s%c 64.0GT/s%c\n"
         "\t\t\t      Number of errors: %u, Spacing between errors: %u, Flit Type: %s\n",
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_EN),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_TX),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_RX),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_25GT),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_50GT),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_80GT),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_16GT),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_32GT),
         FLAG(flit_ei_ctl1, PCI_FLIT_EI_CTL1_64GT),
         PCI_FLIT_EI_CTL1_NUM_ERR(flit_ei_ctl1),
         PCI_FLIT_EI_CTL1_SPACING(flit_ei_ctl1),
         flit_ei_flit_type_str(buf0, sizeof(buf0), PCI_FLIT_EI_CTL1_FLIT_TY(flit_ei_ctl1)));

  u32 flit_ei_ctl2 = get_conf_long(d, where + PCI_FLIT_EI_CTL2);
  printf("\t\tFlitEiCtl2:   Consecutive: %s, Type: %s, Offset: %u, Magnitude: %u\n",
         flit_ei_consec_str(buf0, sizeof(buf0), PCI_FLIT_EI_CTL2_CONSEC(flit_ei_ctl2)),
         flit_ei_err_type_str(buf1, sizeof(buf1), PCI_FLIT_EI_CTL2_TYPE(flit_ei_ctl2)),
         PCI_FLIT_EI_CTL2_OFFS(flit_ei_ctl2),
         PCI_FLIT_EI_CTL2_MAG(flit_ei_ctl2));

  u32 flit_ei_sts = get_conf_long(d, where + PCI_FLIT_EI_STS);
  printf("\t\tFlitEiSts:    Tx: %s, Rx: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_STS_TX(flit_ei_sts)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_STS_RX(flit_ei_sts)));

  u32 flit_ei_os_ctl1 = get_conf_long(d, where + PCI_FLIT_EI_OS_CTL1);
  printf("\t\tFlitEiOsCtl1: En%c Tx%c Rx%c TS0%c TS1%c TS2%c SKP%c EIEOS%c EIOS%c\n"
        "\t\t\t      SDS%c Poll%c Conf%c L0%c NoEq%c Eq01%c Eq2%c Eq3%c\n",
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_EN),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_TX),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_RX),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_TS0),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_TS1),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_TS2),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_SKP),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_EIEOS),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_EIOS),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_SDS),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_POLL),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_CONF),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_L0),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_NOEQ),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_EQ01),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_EQ2),
        FLAG(flit_ei_os_ctl1, PCI_FLIT_EI_OS_CTL1_EQ3));

  u32 flit_ei_os_ctl2 = get_conf_long(d, where + PCI_FLIT_EI_OS_CTL2);
  printf("\t\tFlitEiOsCtl2: Bytes: %04x, Lanes: %04x\n",
         PCI_FLIT_EI_OS_CTL2_BYTES(flit_ei_os_ctl2),
         PCI_FLIT_EI_OS_CTL2_LANES(flit_ei_os_ctl2));

  u32 flit_ei_os_tx = get_conf_long(d, where + PCI_FLIT_EI_OS_TX);
  printf("\t\tFlitEiOsTx:   TS0: %s, TS1: %s, TS2: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_TX_TS0(flit_ei_os_tx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_TX_TS1(flit_ei_os_tx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_TX_TS2(flit_ei_os_tx)));
  printf("\t\t\t      SKP: %s, EIEOS: %s, EIOS: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_TX_SKP(flit_ei_os_tx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_TX_EIEOS(flit_ei_os_tx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_TX_EIOS(flit_ei_os_tx)));
  printf("\t\t\t      SDS: %s, Polling: %s, Configuration: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_TX_SDS(flit_ei_os_tx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_TX_POLL(flit_ei_os_tx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_TX_CONF(flit_ei_os_tx)));
  printf("\t\t\t      L0: %s, non-EQ recovery: %s, Eq01: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_TX_L0(flit_ei_os_tx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_TX_NOEQ(flit_ei_os_tx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_TX_EQ01(flit_ei_os_tx)));
  printf("\t\t\t      Eq2: %s, Eq3: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_TX_EQ2(flit_ei_os_tx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_TX_EQ3(flit_ei_os_tx)));

  u32 flit_ei_os_rx = get_conf_long(d, where + PCI_FLIT_EI_OS_RX);
  printf("\t\tFlitEiOsRx:   TS0: %s, TS1: %s, TS2: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_RX_TS0(flit_ei_os_rx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_RX_TS1(flit_ei_os_rx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_RX_TS2(flit_ei_os_rx)));
  printf("\t\t\t      SKP: %s, EIEOS: %s, EIOS: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_RX_SKP(flit_ei_os_rx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_RX_EIEOS(flit_ei_os_rx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_RX_EIOS(flit_ei_os_rx)));
  printf("\t\t\t      SDS: %s, Polling: %s, Configuration: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_RX_SDS(flit_ei_os_rx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_RX_POLL(flit_ei_os_rx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_RX_CONF(flit_ei_os_rx)));
  printf("\t\t\t      L0: %s, non-EQ recovery: %s, Eq01: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_RX_L0(flit_ei_os_rx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_RX_NOEQ(flit_ei_os_rx)),
         flit_ei_sts_str(buf2, sizeof(buf2), PCI_FLIT_EI_OS_RX_EQ01(flit_ei_os_rx)));
  printf("\t\t\t      Eq2: %s, Eq3: %s\n",
         flit_ei_sts_str(buf0, sizeof(buf0), PCI_FLIT_EI_OS_RX_EQ2(flit_ei_os_rx)),
         flit_ei_sts_str(buf1, sizeof(buf1), PCI_FLIT_EI_OS_RX_EQ3(flit_ei_os_rx)));
}

static const char *flit_log_mes_ctl_gran(char *buf, size_t buflen, u8 sts)
{
  switch (sts)
    {
      case 0b00:
        return "all";
      case 0b001:
        return "even";
      case 0b010:
        return "odd";
      case 0b011:
        return "mismatch";
      default:
        snprintf(buf, buflen, "Unknown (%u)", sts);
        return buf;
    }
}

static void
cap_flit_log(struct device *d, int where)
{
  char buf[16];

  printf("Flit Logging\n");

  if (verbose < 2)
    return;

  if (!config_fetch(d, where + PCI_FLIT_LOG_ERR1, 56))
    return;

  u32 err_log_1 = get_conf_long(d, where + PCI_FLIT_LOG_ERR1);
  printf("\t\tLog1:      Valid%c, Link Width: %s\n"
         "\t\t\t   More entries%c Unrecognized flit%c FEC uncorrectable%c\n"
         "\t\t\t   SyndParityGrp0: %02x, SyndCheckGrp0: %02x\n",
         FLAG(err_log_1, PCI_FLIT_LOG_ERR1_VLD),
         link_width_str(buf, sizeof(buf), PCI_FLIT_LOG_ERR1_WIDTH(err_log_1)),
         FLAG(err_log_1, PCI_FLIT_LOG_ERR1_MORE),
         FLAG(err_log_1, PCI_FLIT_LOG_ERR1_UNREC),
         FLAG(err_log_1, PCI_FLIT_LOG_ERR1_UNCOR),
         PCI_FLIT_LOG_ERR1_PAR_GRP0(err_log_1),
         PCI_FLIT_LOG_ERR1_CHK_GRP0(err_log_1));

  u32 err_log_2 = get_conf_long(d, where + PCI_FLIT_LOG_ERR2);
  printf("\t\tLog2:      SyndParityGrp1: %02x, SyndCheckGrp1: %02x\n"
         "\t\t\t   SyndParityGrp2: %02x, SyndCheckGrp2: %02x\n",
         PCI_FLIT_LOG_ERR2_PAR_GRP1(err_log_2),
         PCI_FLIT_LOG_ERR2_CHK_GRP1(err_log_2),
         PCI_FLIT_LOG_ERR2_PAR_GRP2(err_log_2),
         PCI_FLIT_LOG_ERR2_CHK_GRP2(err_log_2));

  u16 err_cnt_ctl = get_conf_word(d, where + PCI_FLIT_LOG_CNT_CTL);
  printf("\t\tCntCtl:    Flit Error Counter En%c Flit Error Counter Interrupt En%c\n"
         "\t\t\t   Events to count: %u, Trigger Event on Error Count: %02x\n",
         FLAG(err_cnt_ctl, PCI_FLIT_LOG_CNT_CTL_EN),
         FLAG(err_cnt_ctl, PCI_FLIT_LOG_CNT_CTL_INT),
         PCI_FLIT_LOG_CNT_CTL_EVNT(err_cnt_ctl),
         PCI_FLIT_LOG_CNT_CTL_TRG(err_cnt_ctl));

  u16 err_cnt_sts = get_conf_word(d, where + PCI_FLIT_LOG_CNT_STS);
  printf("\t\tCntSts:    Link Width when Error Counter Started %s\n"
         "\t\t\t   Interrupt Generated based on Trigger Event Count%c, FlitErr: %u\n",
         link_width_str(buf, sizeof(buf), PCI_FLIT_LOG_CNT_STS_WIDTH(err_cnt_sts)),
         FLAG(err_cnt_sts, PCI_FLIT_LOG_CNT_STS_INT),
         PCI_FLIT_LOG_CNT_STS_CNT(err_cnt_sts));

  u32 mes_ctl = get_conf_long(d, where + PCI_FLIT_LOG_MES_CTL);
  printf("\t\tMeasCtl:   En%c Granularity: %s\n",
         FLAG(mes_ctl, PCI_FLIT_LOG_MES_CTL_EN),
         flit_log_mes_ctl_gran(buf, sizeof(buf), PCI_FLIT_LOG_MES_CTL_GRAN(mes_ctl)));

  u32 mes_sts1 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS1);
  printf("\t\tMeasSts1:  Flit Counter: %u\n", mes_sts1);
  u32 mes_sts2 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS2);
  printf("\t\tMeasSts2:  Invalid Flit Counter: %u\n",
         PCI_FLIT_LOG_MES_STS2_INV(mes_sts2));
  u32 mes_sts3 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS3);
  printf("\t\tMeasSts3:  Lane0:  %5u, Lane1:  %5u\n",
         PCI_FLIT_LOG_MES_STS3_LN0(mes_sts3),
         PCI_FLIT_LOG_MES_STS3_LN1(mes_sts3));
  u32 mes_sts4 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS4);
  printf("\t\tMeasSts4:  Lane2:  %5u, Lane3:  %5u\n",
         PCI_FLIT_LOG_MES_STS4_LN2(mes_sts4),
         PCI_FLIT_LOG_MES_STS4_LN3(mes_sts4));
  u32 mes_sts5 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS5);
  printf("\t\tMeasSts5:  Lane4:  %5u, Lane5:  %5u\n",
         PCI_FLIT_LOG_MES_STS5_LN4(mes_sts5),
         PCI_FLIT_LOG_MES_STS5_LN5(mes_sts5));
  u32 mes_sts6 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS6);
  printf("\t\tMeasSts6:  Lane6:  %5u, Lane7:  %5u\n",
         PCI_FLIT_LOG_MES_STS6_LN6(mes_sts6),
         PCI_FLIT_LOG_MES_STS6_LN7(mes_sts6));
  u32 mes_sts7 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS7);
  printf("\t\tMeasSts7:  Lane8:  %5u, Lane9:  %5u\n",
         PCI_FLIT_LOG_MES_STS7_LN8(mes_sts7),
         PCI_FLIT_LOG_MES_STS7_LN9(mes_sts7));
  u32 mes_sts8 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS8);
  printf("\t\tMeasSts8:  Lane10: %5u, Lane11: %5u\n",
         PCI_FLIT_LOG_MES_STS8_LN10(mes_sts8),
         PCI_FLIT_LOG_MES_STS8_LN11(mes_sts8));
  u32 mes_sts9 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS9);
  printf("\t\tMeasSts9:  Lane12: %5u, Lane13: %5u\n",
         PCI_FLIT_LOG_MES_STS9_LN12(mes_sts9),
         PCI_FLIT_LOG_MES_STS9_LN13(mes_sts9));
  u32 mes_sts10 = get_conf_long(d, where + PCI_FLIT_LOG_MES_STS10);
  printf("\t\tMeasSts10: Lane14: %5u, Lane15: %5u\n",
         PCI_FLIT_LOG_MES_STS10_LN14(mes_sts10),
         PCI_FLIT_LOG_MES_STS10_LN15(mes_sts10));
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
      if (!header || header == 0xffffffff)
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
	    cap_phy_16gt(d, where);
	    break;
	  case PCI_EXT_CAP_ID_LMR:
	    cap_lmr(d, where);
	    break;
	  case PCI_EXT_CAP_ID_HIER_ID:
	    printf("Hierarchy ID <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_NPEM:
	    printf("Native PCIe Enclosure Management <?>\n");
	    break;
	  case PCI_EXT_CAP_ID_32GT:
	    cap_phy_32gt(d, where);
	    break;
	  case PCI_EXT_CAP_ID_DOE:
	    cap_doe(d, where);
	    break;
	  case PCI_EXT_CAP_ID_IDE:
	    cap_ide(d, where);
	    break;
	  case PCI_EXT_CAP_ID_64GT:
	    cap_phy_64gt(d, where);
	    break;
	  case PCI_EXT_CAP_ID_DEV3:
	    cap_dev3(d, where);
	    break;
	  case PCI_EXT_CAP_ID_MMIO_RBL:
	    cap_mmio_rbl(d, where);
	    break;
	  case PCI_EXT_CAP_ID_FLIT_EI:
	    cap_flit_ei(d, where);
	    break;
	  case PCI_EXT_CAP_ID_FLIT_LOG:
	    cap_flit_log(d, where);
	    break;
	  default:
	    printf("Extended Capability ID %#02x\n", id);
	    break;
	}
      where = (header >> 20) & ~3;
    } while (where);
}
