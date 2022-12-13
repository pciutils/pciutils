/*
 *      The PCI Utilities -- Debug tool to diagnose PCIe link errors where
 *      			controller is Synopsys DesignWare Controller
 *
 *      Copyright (c) 2022 Samsung Electronics Co., Ltd. http://www.samsung.com
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/pci.h"
#include "lspci.h"
#include "pciutils.h"

/* Device filter */
struct pci_filter filter;
static unsigned int ras_des_base, aer_base, pcie_base;
static unsigned int max_lanes;

#define DEBUGPCI_VERSION		"1.0"
#define MAX_LANE			32

#define EVENT_OFFSET			0x8
#define EVENT_MASK			0xFF0000
#define EVENT_SHIFT			16
#define GROUP_MASK			0xF000000
#define GROUP_SHIFT			24
#define LANE_SEL_MASK			0xF00
#define LANE_SEL_SHIFT			8
#define EVENT_ENABLE_MASK		0x1C
#define EVENT_ENABLE			0x3
#define EVENT_ENABLE_SHIFT		2
#define EVENT_DATA_OFFSET		0xC

#define DWC_PL0_DEBUG0			0x728
#define DWC_SD_STATUS_LANE_SEL_MASK	0xF
#define DWC_SD_STATUS_L1LANE_REG	0xB0
#define DWC_SD_STATUS_L1LTSSM_REG	0xB4
#define DWC_SD_STATUS_PM_REG		0xB8
#define DWC_SD_STATUS_L2_REG		0xBC
#define DWC_SD_STATUS_L3FC_REG		0xC0
#define DWC_SD_STATUS_L3_REG		0xC4
#define DWC_SD_EQ_STATUS1_REG		0xE0
#define DWC_SD_EQ_STATUS2_REG		0xE4
#define DWC_SD_EQ_STATUS3_REG		0xE8
#define AER_UNAER_CORR_ERR_STATUS	0x4
#define AER_CORR_ERR_STATUS		0x10

/*
 * struct event_counters - store error details for RAS DES counter events
 * @field_id: enum to identify different event counters
 * @group_id: Group id of the RAS DES event counter
 * @event_id: Event id of the RAS DES event counter
 * @name: Name of the event
 */
struct event_counters {
	unsigned int field_id;
	unsigned int group_id;
	unsigned int event_id;
	const char *name;
};

/*
 * struct debug_data - store error details for RAS DES/AER debug register fields
 * @field_id: enum for identifying different debug fields
 * @offset: Offset of the debug field from base
 * @mask: Mask for the debug field
 * @shift: Start bit for debug field
 * @name: Name of the debug field
 */
struct debug_data {
	unsigned int field_id;
	unsigned int offset;
	unsigned int mask;
	unsigned int shift;
	const char *name;
};

/*
 * struct lane_results - store error counter or value and error message
 * 			for all lane specific errors
 * @val: Store the count of the error per lane
 * @err_msg: Store the corresponding error message
 */
struct lane_results {
	unsigned int field_id;
	unsigned int val[MAX_LANE];
	const char *err_msg;
};

/*
 * struct results - store error counter or value and error message
 * 			for all errors which are lane independent
 * @val: Store the count of the error
 * @err_msg: Store the corresponding error message
 */
struct results {
	unsigned int field_id;
	unsigned int val;
	const char *err_msg;
};

/* Lane specific RAS DES event counter identifiers */
enum lane_events_fields {
	EBUF_OVERFLOW,
	EBUF_UNDERRUN,
	DECODE,
	RUNNING_DISPARITY,
	SKP_OS_PARITY,
	SYNC_HEADER,
	RX_VALID_DEASSERTION,
	CTL_SKP_OS_PARITY,
	RETIMER_1_PARITY,
	RETIMER_2_PARITY,
	MARGIN_CRC,
	EBUF_SKP_ADD,
	EBUF_SKP_DIVIDE,
};

/* Lane invariant RAS DES event counter identifiers */
enum event_fields {
	DETECT_EI,
	RX_ERROR,
	RX_RECOVERY_RQST,
	N_FT3_TIMEOUT,
	FRAMING_ERROR,
	DESKEW_ERROR,
	BAD_TLP,
	LCRC_ERROR,
	BAD_DLLP,
	REPLAY_NUMBER,
	REPLAY_TIMEOUT,
	RX_NAK_DLLP,
	TX_NAK_DLLP,
	RETRY_TLP,
	FC_TIMEOUT,
	POISONED_TLP,
	ECRC_ERROR,
	UNSUPPORTED_RQST,
	COMPLETOR_ABORT,
	COMPLETION_TIMEOUT,
};

/* Lane specific RAS DES debug field identifiers */
enum lane_debug_fields {
	PIPE_RXPOLARITY,
	PIPE_DETECT_LANE,
	PIPE_RXVALID,
	PIPE_RXELECIDLE,
	PIPE_TXELECIDLE,
	DESKEW_POINTER,
};

/* Lane invariant RAS DES debug field identifiers */
enum debug_fields {
	FRAMING_ERR_PTR,
	FRAMING_ERR,
	PIPE_POWER_DOWN,
	LANE_REVERSAL,
	LTSSM_VARIABLE,
	INTERNAL_PM_MSTATE,
	INTERNAL_PM_SSTATE,
	PME_RESEND_FLAG,
	L1SUB_STATE,
	LATCHED_NFTS,
	TX_TLP_SEQ_NO,
	RX_ACK_SEQ_NO,
	DLCMSM,
	FC_INIT1,
	FC_INIT2,
	CREDIT_SEL_VC,
	CREDIT_SEL_CREDIT_TYPE,
	CREDIT_SEL_TLP_TYPE,
	CREDIT_SEL_HD,
	CREDIT_DATA0,
	CREDIT_DATA1,
	MFTLP_POINTER,
	MFTLP_STATUS,
	EQ_SEQUENCE,
	EQ_CONVERGENCE_INFO,
	EQ_RULEA_VIOLATION,
	EQ_RULEB_VIOLATION,
	EQ_RULEC_VIOLATION,
	EQ_REJECT_EVENT,
	EQ_LOCAL_PRE_CURSOR,
	EQ_LOCAL_CURSOR,
	EQ_LOCAL_POST_CURSOR,
	EQ_LOCAL_RX_HINT,
	EQ_LOCAL_FOM_VALUE,
	EQ_REMOTE_PRE_CURSOR,
	EQ_REMOTE_CURSOR,
	EQ_REMOTE_POST_CURSOR,
	EQ_REMOTE_LF,
	EQ_REMOTE_FS,
};

/* AER register field identifiers */
enum aer_err_fields {
	DL_PROTOCOL_ERR,
	SURPRISE_DOWN_ERR,
	FC_PROTOCOL_ERR,
	UNEXP_COMPLETION_ERR,
	REC_OVERFLOW_ERR,
	ACS_VIOLATION,
	INTERNAL_ERR,
	ATOMIC_EGRESS_BLOCKED,
	TLP_PREFIX_BLOCKED,
	POISONED_TLP_EGRESS_BLOCKED,
	ADVISORY_NON_FATAL_ERR,
	HEADER_LOG_OVERFLOW,
};

/* Data to access lane specific RAS Event counters */
static const struct event_counters lane_events[] = {
	{EBUF_OVERFLOW, 0x0, 0x00, "EBUF Overflow"},
	{EBUF_UNDERRUN, 0x0, 0x01, "EBUF Underrun"},
	{DECODE, 0x0, 0x02, "Decode Error"},
	{RUNNING_DISPARITY, 0x0, 0x03, "Running Disparity Error"},
	{SKP_OS_PARITY, 0x0, 0x04, "SKP OS Parity Error"},
	{SYNC_HEADER, 0x0, 0x05, "SYNC Header Error"},
	{RX_VALID_DEASSERTION, 0x0, 0x06, "Rx Valid de-assertion"},
	{CTL_SKP_OS_PARITY, 0x0, 0x07, "CTL SKP OS Parity Error"},
	{RETIMER_1_PARITY, 0x0, 0x08, "1st Retimer Parity Error"},
	{RETIMER_2_PARITY, 0x0, 0x09, "2nd Retimer Parity Error"},
	{MARGIN_CRC, 0x0, 0x0A, "Margin CRC and Parity Error"},
	{EBUF_SKP_ADD, 0x4, 0x00, "EBUF SKP Add"},
	{EBUF_SKP_DIVIDE, 0x4, 0x01, "EBUF SKP Divide"},
};

/* Data to access RAS Event counters which are not lane specific */
static const struct event_counters events[] = {
	{DETECT_EI, 0x1, 0x05, "Detect EI infer"},
	{RX_ERROR, 0x1, 0x06, "Receiver Error"},
	{RX_RECOVERY_RQST, 0x1, 0x07, "Rx Recovery Request"},
	{N_FT3_TIMEOUT, 0x1, 0x08, "N_FT3 Timeout"},
	{FRAMING_ERROR, 0x1, 0x09, "Framing Error"},
	{DESKEW_ERROR, 0x1, 0x0A, "Deskew Error"},
	{BAD_TLP, 0x2, 0x00, "BAD TLP"},
	{LCRC_ERROR, 0x2, 0x01, "LCRC Error"},
	{BAD_DLLP, 0x2, 0x02, "BAD DLLP"},
	{REPLAY_NUMBER, 0x2, 0x03, "Replay Number Rollover"},
	{REPLAY_TIMEOUT, 0x2, 0x04, "Replay Timeout"},
	{RX_NAK_DLLP, 0x2, 0x05, "Rx Nak DLLP"},
	{TX_NAK_DLLP, 0x2, 0x06, "Tx Nak DLLP"},
	{RETRY_TLP, 0x2, 0x07, "Retry TLP"},
	{FC_TIMEOUT, 0x3, 0x00, "FC Timeout"},
	{POISONED_TLP, 0x3, 0x01, "Poisoned TLP"},
	{ECRC_ERROR, 0x3, 0x02, "ECRC Error"},
	{UNSUPPORTED_RQST, 0x3, 0x03, "Unsupported Request"},
	{COMPLETOR_ABORT, 0x3, 0x04, "Completer Abort"},
	{COMPLETION_TIMEOUT, 0x3, 0x05, "Completion Timeout"},
};

/* Data to access lane specific RAS debug information */
static const struct debug_data lane_debug[] = {
	{PIPE_RXPOLARITY, DWC_SD_STATUS_L1LANE_REG, 0x1, 16, "Pipe RX Polarity"},
	{PIPE_DETECT_LANE, DWC_SD_STATUS_L1LANE_REG, 0x1, 17, "Pipe Detect Lane"},
	{PIPE_RXVALID, DWC_SD_STATUS_L1LANE_REG, 0x1, 18, "Pipe RX Valid"},
	{PIPE_RXELECIDLE, DWC_SD_STATUS_L1LANE_REG, 0x1, 19, "Pipe RX Electrical Idle"},
	{PIPE_TXELECIDLE, DWC_SD_STATUS_L1LANE_REG, 0x1, 20, "Pipe TX Electrical Idle"},
	{DESKEW_POINTER, DWC_SD_STATUS_L1LANE_REG, 0xFF, 24, "Deskew Pointer"},
};

/* Data to access RAS debug fields which is not lane specific */
static const struct debug_data debug[] = {
	{FRAMING_ERR_PTR, DWC_SD_STATUS_L1LTSSM_REG, 0x7F,  0, "Framing Error Pointer"},
	{FRAMING_ERR, DWC_SD_STATUS_L1LTSSM_REG, 0x1,  7, "Framing Error"},
	{PIPE_POWER_DOWN, DWC_SD_STATUS_L1LTSSM_REG, 0x7,  8, "Pipe Power Down"},
	{LANE_REVERSAL, DWC_SD_STATUS_L1LTSSM_REG, 0x1, 15, "Lane Reversal"},
	{LTSSM_VARIABLE, DWC_SD_STATUS_L1LTSSM_REG, 0xFFFF, 16, "LTSSM Variable"},
	{INTERNAL_PM_MSTATE, DWC_SD_STATUS_PM_REG, 0x1F,  0, "Internal PM MState"},
	{INTERNAL_PM_SSTATE, DWC_SD_STATUS_PM_REG, 0xF,  8, "Internal PM SState"},
	{PME_RESEND_FLAG, DWC_SD_STATUS_PM_REG, 0x1, 12, "PME Resend Flag"},
	{L1SUB_STATE, DWC_SD_STATUS_PM_REG, 0x7, 13, "L1 Sub State"},
	{LATCHED_NFTS, DWC_SD_STATUS_PM_REG, 0xFF, 16, "Latched NFTS"},
	{TX_TLP_SEQ_NO, DWC_SD_STATUS_L2_REG, 0xFFF,  0, "TX TLP Seq Number"},
	{RX_ACK_SEQ_NO, DWC_SD_STATUS_L2_REG, 0xFFF, 12, "RX ACK Deq Number"},
	{DLCMSM, DWC_SD_STATUS_L2_REG, 0x3, 24, "DLCMSM"},
	{FC_INIT1, DWC_SD_STATUS_L2_REG, 0x1, 26, "FC INIT1"},
	{FC_INIT2, DWC_SD_STATUS_L2_REG, 0x1, 27, "FC INIT2"},
	{CREDIT_SEL_VC, DWC_SD_STATUS_L3FC_REG, 0x7,  0, "Credit Sel VC"},
	{CREDIT_SEL_CREDIT_TYPE, DWC_SD_STATUS_L3FC_REG, 0x1,  3, "Credit Type"},
	{CREDIT_SEL_TLP_TYPE, DWC_SD_STATUS_L3FC_REG, 0x3,  4, "Credit Sel TLP Type"},
	{CREDIT_SEL_HD, DWC_SD_STATUS_L3FC_REG, 0x1,  6, "Credit Sel HD"},
	{CREDIT_DATA0, DWC_SD_STATUS_L3FC_REG, 0xFFF,  8, "Credit DATA0"},
	{CREDIT_DATA1, DWC_SD_STATUS_L3FC_REG, 0xFFF, 20, "Credit DATA1"},
	{MFTLP_POINTER, DWC_SD_STATUS_L3_REG, 0x7F,  0, "Malformed TLP Pointer"},
	{MFTLP_STATUS, DWC_SD_STATUS_L3_REG, 0x1,  7, "Malformed TLP Status"},
	{EQ_SEQUENCE, DWC_SD_EQ_STATUS1_REG, 0x1,  0, "EQ Sequence"},
	{EQ_CONVERGENCE_INFO, DWC_SD_EQ_STATUS1_REG, 0x3,  1, "EQ Convergence Info"},
	{EQ_RULEA_VIOLATION, DWC_SD_EQ_STATUS1_REG, 0x1,  4, "EQ Rule A Violation"},
	{EQ_RULEB_VIOLATION, DWC_SD_EQ_STATUS1_REG, 0x1,  5, "EQ Rule B Violation"},
	{EQ_RULEC_VIOLATION, DWC_SD_EQ_STATUS1_REG, 0x1,  6, "EQ Rule C Violation"},
	{EQ_REJECT_EVENT, DWC_SD_EQ_STATUS1_REG, 0x1,  7, "EQ Reject Event"},
	{EQ_LOCAL_PRE_CURSOR, DWC_SD_EQ_STATUS2_REG, 0x3F,  0, "EQ Local Pre Cursor"},
	{EQ_LOCAL_CURSOR, DWC_SD_EQ_STATUS2_REG, 0x3F,  6, "EQ Local Cursor"},
	{EQ_LOCAL_POST_CURSOR, DWC_SD_EQ_STATUS2_REG, 0x3F, 12, "EQ Local Post Cursor"},
	{EQ_LOCAL_RX_HINT, DWC_SD_EQ_STATUS2_REG, 0x7, 18, "EQ Local RX Hint"},
	{EQ_LOCAL_FOM_VALUE, DWC_SD_EQ_STATUS2_REG, 0xFF, 24, "EQ Local FOM Value"},
	{EQ_REMOTE_PRE_CURSOR, DWC_SD_EQ_STATUS3_REG, 0x3F,  0, "EQ Remote Pre Cursor"},
	{EQ_REMOTE_CURSOR, DWC_SD_EQ_STATUS3_REG, 0x3F,  6, "EQ Remote Cursor"},
	{EQ_REMOTE_POST_CURSOR, DWC_SD_EQ_STATUS3_REG, 0x3F, 12, "EQ Remote Post Cursor"},
	{EQ_REMOTE_LF, DWC_SD_EQ_STATUS3_REG, 0x3F, 18, "EQ Remote LF"},
	{EQ_REMOTE_FS, DWC_SD_EQ_STATUS3_REG, 0x3F, 24, "EQ Remote FS"},
};

/* Data to access AER correctable and uncorrectable fields */
static const struct debug_data aer[] = {
	{DL_PROTOCOL_ERR, AER_UNAER_CORR_ERR_STATUS, 0x1, 4, "DL Protocol Error"},
	{SURPRISE_DOWN_ERR, AER_UNAER_CORR_ERR_STATUS, 0x1, 5, "Surprise Down Error"},
	{FC_PROTOCOL_ERR, AER_UNAER_CORR_ERR_STATUS, 0x1, 13, "FC Protocol Error"},
	{UNEXP_COMPLETION_ERR, AER_UNAER_CORR_ERR_STATUS, 0x1, 16, "Unexpected Completion Error"},
	{REC_OVERFLOW_ERR, AER_UNAER_CORR_ERR_STATUS, 0x1, 17, "REC Overflow Error"},
	{ACS_VIOLATION, AER_UNAER_CORR_ERR_STATUS, 0x1, 21, "ACS Violation"},
	{INTERNAL_ERR, AER_UNAER_CORR_ERR_STATUS, 0x1, 22, "Internal Error"},
	{ATOMIC_EGRESS_BLOCKED, AER_UNAER_CORR_ERR_STATUS, 0x1, 24, "Atomic Egress Blocked"},
	{TLP_PREFIX_BLOCKED, AER_UNAER_CORR_ERR_STATUS, 0x1, 25, "TLP Prefix Blocked"},
	{POISONED_TLP_EGRESS_BLOCKED, AER_UNAER_CORR_ERR_STATUS, 0x1, 26, "Poisoned TLP Egress Blocked"},
	{ADVISORY_NON_FATAL_ERR, AER_CORR_ERR_STATUS, 0x1, 13, "Advisory Non-Fatal Error"},
	{HEADER_LOG_OVERFLOW, AER_CORR_ERR_STATUS, 0x1, 15, "Header Log Overflow Error"},
};

static struct lane_results lane_events_res[] = {
	{EBUF_OVERFLOW, {}, "Check if the PHY is properly adding and/or removing SKP\n\n"},
	{EBUF_UNDERRUN, {}, "Check if the PHY is properly adding and/or removing SKP\n\n"},
	{DECODE, {}, ""},
	{RUNNING_DISPARITY, {}, ""},
	{SKP_OS_PARITY, {}, ""},
	{SYNC_HEADER, {}, ""},
	{RX_VALID_DEASSERTION, {}, ""},
	{CTL_SKP_OS_PARITY, {}, ""},
	{RETIMER_1_PARITY, {}, ""},
	{RETIMER_2_PARITY, {}, ""},
	{MARGIN_CRC, {}, ""},
};

static struct results events_res[] = {
	{DETECT_EI, 0x0, ""},
	{RX_ERROR, 0x0, "Check if PHY is also reporting these receiver errors by reading RXSTATUS\n"
			"RXSTATUS = 100b represents Decode Error\n"
			"RXSTATUS = 111b represents Disparity Error\n"
			"RXSTATUS = 101b represent Overflow Error\n"
			"RXSTATUS = 110b represent Underflow Error\n"
			"RXSTATUS = 001b reports SKP added\n"
			"RXSTATUS = 010b reports SKP removed\n\n"},
	{RX_RECOVERY_RQST, 0x0, ""},
	{N_FT3_TIMEOUT, 0x0, ""},
	{FRAMING_ERROR, 0x0, ""},
	{DESKEW_ERROR, 0x0, ""},
	{BAD_TLP, 0x0, "Its's a correctable error.\nThis error is reported if the received TLP fails LCRC "
			"check or has incorrect sequence number.\nThis can occur as a result of bit errors "
			"on the link or due to receiver errors.\n\n"},
	{LCRC_ERROR, 0x0, "It's a correctable error.\nTLP must have failed LCRC check.\n If the calculated "
				"LCRC value does not equal the received value, "
				"the TLP is discarded and a Nak DLLP is scheduled for transmission.\n\n"},
	{BAD_DLLP, 0x0, "This is a correctable error. This error is reported if the received DLLP fails CRC check.\n"
			"This can also occur as a result of bit errors or RX errors.\n\n"},
	{REPLAY_NUMBER, 0x0, "It's a correctable error. Replay Number Rollover detected.\n "
			     "This error is reported if no ACK or NACK is received from the link partner "
			     "for a particular TLP, before the replay timer expires for three consecutive times.\n"
			     "This can occur if the ACK/NACK DLLP is corrupted due to bit errors on the link, "
			     "and is not detected by the controller.\nIf an analyzer trace is available, check "
			     "if all TLPs are receiving ACK/NACK from the link partner.\n\n"},
	{REPLAY_TIMEOUT, 0x0, "It's a correctable error.\n Replay Timer timed out.\nThis happens if no ACK or "
				"NACK is seen by the PCIe Controller for a transmitted TLP, before the Replay "
				"timer expires.\nFor debug purpose you can try increasing the Replay Timer timeout limit"
				"by using the TIMER_MOD_REPLAY_TIMER field of the TIMER_CTRL_MAX_FUNC_NUM_OFF register.\n\n"},
	{RX_NAK_DLLP, 0x0, ""},
	{TX_NAK_DLLP, 0x0, ""},
	{RETRY_TLP, 0x0, ""},
	{FC_TIMEOUT, 0x0, ""},
	{POISONED_TLP, 0x0, " Link partner sent a TLP with the EP bit set in packet header.\nData poisoning is done at "
				"the transaction layer of a device. For example when requester performs a Memory write "
				"transaction, the data (to be written) fetched from local memory, can have parity error. "
				"For corrupted data, the packet is sent to recipient with “EP” bit set. The recipient will "
				"drop or process the packet, depends on implementation.\n\n"},
	{ECRC_ERROR, 0x0, "ECRC is End to End CRC. ECRC of received TLP did not match the calculated ECRC.\n "
				"This indicates corruption of the TLP header or payload.\n "
				"ECRC in request packet: The completer will drop the packet and no completion "
				"will be returned .That will result in a completion time-out within the "
				"requesting device and the requester will reschedule the same transaction.\n"
				"ECRC in completion packet: The requester will drop the packet and error "
				"reported to the function's device driver via a function-specific interrupt\n\n"},
	{UNSUPPORTED_RQST, 0x0, "Reported for example when a received MEM TLP does not hit any of the enabled "
				"BARs of a device.\nIn that case, check the address of the received TLP is valid.\n\n"},
	{COMPLETOR_ABORT, 0x0, "Reported when a CPL TLP is received with status “Completer Abort”.\n\n"},
	{COMPLETION_TIMEOUT, 0x0, "Reported when an outbound non-posted request does not receive a CPL, "
				"before the CPL timer expires.\nThis can happen, for example, "
				"if the received CPL is malformed, or had an ECRC error and is dropped by "
				"the PCIe Controller.\nIt could also indicate that the CPL is not sent by "
				"the link partner.\nCheck the analyzer trace to find the expected CPL.\n\n" },
};

static struct lane_results lane_debug_res[] = {
	{PIPE_RXPOLARITY, {}, ""},
	{PIPE_DETECT_LANE, {}, ""},
	{PIPE_RXVALID, {}, ""},
	{PIPE_RXELECIDLE, {}, ""},
	{PIPE_TXELECIDLE, {}, ""},
	{DESKEW_POINTER, {}, ""},
};

static struct results debug_res[] = {
	{FRAMING_ERR_PTR, 0x0, ""},
	{FRAMING_ERR, 0x0, ""},
	{PIPE_POWER_DOWN, 0x0, ""},
	{LANE_REVERSAL, 0x0, ""},
	{LTSSM_VARIABLE, 0x0, ""},
	{INTERNAL_PM_MSTATE, 0x0, ""},
	{INTERNAL_PM_SSTATE, 0x0, ""},
	{PME_RESEND_FLAG, 0x0, ""},
	{L1SUB_STATE, 0x0, ""},
	{LATCHED_NFTS, 0x0, ""},
	{TX_TLP_SEQ_NO, 0x0, ""},
	{RX_ACK_SEQ_NO, 0x0, ""},
	{DLCMSM, 0x0, ""},
	{FC_INIT1, 0x0, ""},
	{FC_INIT2, 0x0, ""},
	{CREDIT_SEL_VC, 0x0, ""},
	{CREDIT_SEL_CREDIT_TYPE, 0x0, ""},
	{CREDIT_SEL_TLP_TYPE, 0x0, ""},
	{CREDIT_SEL_HD, 0x0, ""},
	{CREDIT_DATA0, 0x0, ""},
	{CREDIT_DATA1, 0x0, ""},
	{MFTLP_POINTER, 0x0, ""},
	{MFTLP_STATUS, 0x0, ""},
	{EQ_SEQUENCE, 0x0, ""},
	{EQ_CONVERGENCE_INFO, 0x0, ""},
	{EQ_RULEA_VIOLATION, 0x0, ""},
	{EQ_RULEB_VIOLATION, 0x0, ""},
	{EQ_RULEC_VIOLATION, 0x0, ""},
	{EQ_REJECT_EVENT, 0x0, ""},
	{EQ_LOCAL_PRE_CURSOR, 0x0, ""},
	{EQ_LOCAL_CURSOR, 0x0, ""},
	{EQ_LOCAL_POST_CURSOR, 0x0, ""},
	{EQ_LOCAL_RX_HINT, 0x0, ""},
	{EQ_LOCAL_FOM_VALUE, 0x0, ""},
	{EQ_REMOTE_PRE_CURSOR, 0x0, ""},
	{EQ_REMOTE_CURSOR, 0x0, ""},
	{EQ_REMOTE_POST_CURSOR, 0x0, ""},
	{EQ_REMOTE_LF, 0x0, ""},
	{EQ_REMOTE_FS, 0x0, ""},
};

static struct results aer_res[] = {
	{DL_PROTOCOL_ERR, 0x0, "Reported if the sequence number of received TLP is invalid.\n"
				"If this error is reported, check the analyzer trace to see "
				"received TLPs and find out which TLP has invalid sequence number.\n\n"},
	{SURPRISE_DOWN_ERR, 0x0, "When the PCIe device or link goes down without a notice. "
				"Can happen if the link is weak and has RX errors\n\n"},
	{FC_PROTOCOL_ERR, 0x0, "Occurs if no DLLP is received within a 200us window "
				"(Watch Dog Timer expiration limit).\nThis indicates that the link quality "
				"is severely deteriorated.\n\n"},
	{UNEXP_COMPLETION_ERR, 0x0, "Indicates that a CPL TLP is received for which the corresponding "
				"nonposted request is not transmitted or is no longer outstanding.\n"
				"This can happen if the TAG field or other header fields "
				"of the received CPL do not match the corresponding request header fields.\n\n"},
	{REC_OVERFLOW_ERR, 0x0, "Reported when the credit check on a received TLP fails.\n"
				"This means the receive queue buffer does not have enough space to accept "
				"the received TLP.\nThis can happen if the link partner ignores flow control "
				"updates, or, is not receiving correct flow control updates.\n"
				"Use an analyzer trace to check correct exchange of FC Update DLLPs.\n\n"},
	{ACS_VIOLATION, 0x0, "Violation in Access Control Services.\n\n"},
	{INTERNAL_ERR, 0x0, "Reported if your application logic drives the app_err_bus[9] PCIe Controller "
			"input to '1', or the PCIe Controller detected an uncorrectable datapath or RAM "
			"parity/ecc error.\n\n"},
	{ATOMIC_EGRESS_BLOCKED, 0x0, "Error with setting AtomicOp Egress Blocking bit.\n\n"},
	{TLP_PREFIX_BLOCKED, 0x0, "The TLP Prefix mechanism extends the header size by adding DWORDS to "
				"the front of headers that carry additional information.\n"
				"The uncorrected error reflects failure in the process.\n\n"},
	{POISONED_TLP_EGRESS_BLOCKED, 0x0, ""},
	{ADVISORY_NON_FATAL_ERR, 0x0, "This indicates that the severity of the error occurred has been set "
					"to Non-Fatal in the Uncorrectable Error Severity Register.\n\n"},
	{HEADER_LOG_OVERFLOW, 0x0, "This occurs when an error that requires header logging is detected, and either:\n"
				"1) The number of recorded headers supported by the PCIe Controller has been reached, or\n"
				"2) The Multiple Header Recording Enable bit is not Set, and the First "
				"Error Pointer register is valid.\n\n"},
};

/*
 * find_bases: Function to get the RAS, PCIe and AER base address
 *
 * Since all controllers might have different base addresses, we use this
 * function to find all required base addresses
 *
 * Return: RAS base address or negative if error or no RAS base is found
 */
static int find_bases(struct pci_dev *pdev)
{
	int where;
	int id, next, vid;

	ras_des_base = ENOENT;

	where = pci_read_long(pdev, PCI_CAPABILITY_LIST) & 0xFF;
	while (where) {
		id = pci_read_long(pdev, where) & 0xFF;
		next = (pci_read_long(pdev, where) >> 8) & 0xFF;
		if (id == 0xFF) {
			printf("Chain broken\n");
			return EPERM;
		}
		if (id == PCI_CAP_ID_EXP) {
			pcie_base = where;
			break;
		}
		where = next;
	}

	where = 0x100;
	while (where) {
		id = pci_read_long(pdev, where) & 0xFFFF;
		next = (pci_read_long(pdev, where) >> 20) & ~3;
		if (id == 0xFF) {
			printf("Chain broken\n");
			return EPERM;
		}
		if (id == PCI_EXT_CAP_ID_AER)
		       aer_base = where;
		if (id == PCI_EXT_CAP_ID_VNDR) {
			vid = pci_read_long(pdev, where + 4) & 0xFFFF;
			if (vid == 0x2)
				ras_des_base = where;
		}
		where = next;
	}

	return ras_des_base;
}

/*
 * print_link_info: Function to retrieve link status information
 * 		  like max speed/width and negotiated speed/width
 * 		  alongside the LTSSM state.
 *
 * Return: void
 */
static void print_link_info(struct pci_dev *pdev)
{
	unsigned int val;

	printf("LINK INFO:\n==========\n");

	val = pci_read_long(pdev, pcie_base + PCI_EXP_LNKCAP);
	printf("Max Speed: %d\n", val & PCI_EXP_LNKCAP_SPEED);
	printf("Max Width: %d\n", (val & PCI_EXP_LNKCAP_WIDTH) >> 4);

	val = pci_read_word(pdev, pcie_base + PCI_EXP_LNKSTA);
	printf("Negotiated Speed: %d\n", val & PCI_EXP_LNKSTA_SPEED);
	max_lanes = (val & PCI_EXP_LNKSTA_WIDTH) >> 4;
	printf("Negotiated Width: %d\n", (val & PCI_EXP_LNKSTA_WIDTH) >> 4);

	printf("LTSSM State: %x\n", pci_read_long(pdev, DWC_PL0_DEBUG0) & 0x1F);
}

/*
 * receiver_detect_err: Function to identify is receiver has been detected
 * 			properly or not
 * Return: void
 */
static void receiver_detect_err(void)
{
	unsigned int i;
	int flag;

	flag = 0;
	for (i = 0; i < max_lanes; i++) {
		if (!lane_debug_res[PIPE_DETECT_LANE].val[i]) {
			flag++;
			printf("Lane %d not detected\n", i);
		}
	}

	if (flag) {
		printf("1) If the receiver detection feature is not working properly, bypass receiver detection "
			"to see if link training progresses for debug. Application software can set "
			"FORCE_DETECT_LANE_EN field of the SD_CONTROL1_REG[16] register to 1b to instruct the PCIe "
			"Controller to ignore receiver detection from PHY during LTSSM Detect state and use "
			"receiver detection status from FORCE_DETECT_LANE field of SD_CONTROL1_REG[15:0] "
			"register instead. Each bit in SD_CONTROL1_REG[15:0] register corresponds to one lane\n"
			"2) Check for any receiver detection related timeout. If the PHY requires more time "
			"for receiver detection, the application software can hold LTSSM in Detect.Active by "
			"setting the HOLD_LTSSM field of SD_CONTROL2_REG[0] register.\n"
			"3) PIPE: Check if receiver detection is executed in Gen1\n"
			"4) DC single ended impedance:\nCheck if the remote PCIe link partner's receiver DC "
			"single ended impedance (ZRX-DC) is between 40 and 60 Ohm. See the PCIe Base specification.\n"
			"5) Perform receiver detection on a known good receiver that can always be detected "
			"by other PCIe devices.\n"
			"6) Try swapping passing and failing lanes to see if the passing lane still passes.\n");
	}
}

/*
 * broken_lane_err: Function detect errors indicating broken lanes.
 * Return: void
 */
static void broken_lane_err(void)
{
	unsigned int i, flag;

	flag = 0;
	for (i = 0; i < max_lanes; i++) {
		if (!lane_debug_res[PIPE_RXVALID].val[i]) {
			flag++;
			printf("Rx is not Valid for Lane %d.\n", i);
		}
	}

	if (flag) {
		printf("This might indicate broken lanes\n");
		printf("After receiver detection is completed, the LTSSM goes through Polling -> Configuration -> Recovery states,\n");
		printf("before reaching L0 state at Gen1 data rate.\n");
		printf("If some lanes are broken after receiver detection, the link may not reach L0 at the desired link width.\n");
		printf("Possible debug steps are as follows:\n");
		printf("1) In a multi-lane setup, to isolate the broken lane, try to link up at a smaller link width\n");
		printf("2) Try a lane reversal setup if feasible (Connect Lane0 to Lane n-1 of the link partner)\n\n");
	}
}

/*
 * dump_l1ltssm_reg: Function to dump the LTSSM variable.
 * LTSSM variable is SD_STATUS_L1LTSSM_REG[20:16], where:
 * SD_STATUS_L1LTSSM_REG[16]: directed_speed_change
 * SD_STATUS_L1LTSSM_REG[17]: changed_speed_recovery
 * SD_STATUS_L1LTSSM_REG[18]: successful_speed_negotiation
 * SD_STATUS_L1LTSSM_REG[19]: upconfigure_capable
 * SD_STATUS_L1LTSSM_REG[20]: select_deemphasis
 *
 * Return: void
 */
static void dump_l1ltssm_reg(void)
{
	unsigned int val;

	val = debug_res[LTSSM_VARIABLE].val;
	printf("\tdirected_speed_change = %d\n", val & 0x1);
	printf("\tchanged_speed_recovery = %d\n", (val >> 1) & 0x1);
	printf("\tsuccessful_speed_negotiation = %d\n", (val >> 2) & 0x1);
	printf("\tupconfigure_capable = %d\n", (val >> 3) & 0x1);
	printf("\tselect_deemphasis = %d\n", (val >> 4) & 0x1);
}

/*
 * framing_err: To check for framing errors details
 * @value: Framing error type
 * Return: void
 */
static void framing_err(int value)
{
	switch(value) {
	case 0x01:
		printf("Received unexpected Framing Token.\n");
		printf("Non-STP/SDP/IDL Token was received and it was not in TLP/DLLP reception.\n");
		break;
	case 0x02:
		printf("Received unexpected Framing Token.\n");
		printf("Current token was not a valid EDB token and previous token was an EDB. (128/256 bit controller only)\n");
		break;
	case 0x03:
		printf("Received unexpected Framing Token.\n");
		printf("SDP token was received but not expected.(128 bit & (x8 | x16) controller only)\n");
		break;
	case 0x04:
		printf("Received unexpected Framing Token.\n");
		printf("STP token was received but not expected.(128 bit & (x8 | x16) controller only)\n");
		break;
	case 0x05:
		printf("Received unexpected Framing Token.\n");
		printf("EDS token was expected but not received/an EDS token was received but not expected.\n");
		break;
	case 0x06:
		printf("Received unexpected Framing Token.\n");
		printf("Framing error was detected in the deskew block while a packet has been in progress in token_finder.\n");
		break;
	case 0x11:
		printf("Received unexpected STP Token.\n");
		printf("Framing CRC in STP token did not match.\n");
		break;
	case 0x12:
		printf("Received unexpected STP Token.\n");
		printf("Framing Parity in STP token did not match.\n");
		break;
	case 0x13:
		printf("Received unexpected STP Token.\n");
		printf("Framing TLP Length in STP token was smaller than 5 DWORDs.\n");
		break;
	case 0x21:
		printf("Received unexpected Block.\n");
		printf("Received an OS Block following SDS in Datastream state.\n");
		break;
	case 0x22:
		printf("Received unexpected Block.\n");
		printf("Data Block followed by OS Block different from SKP, EI, EIE in Datastream state.\n");
		break;
	case 0x23:
		printf("Received unexpected Block.\n");
		printf("Block with an undefined Block Type in Datastream state.\n");
		break;
	case 0x24:
		printf("Received unexpected Block.\n");
		printf("Data Stream without data over three cycles in Datastream state.\n");
		break;
	case 0x25:
		printf("Received unexpected Block.\n");
		printf("OS Block during Data Stream in Datastream state.\n");
		break;
	case 0x26:
		printf("Received unexpected Block.\n");
		printf("RxStatus Error was detected in Datastream state.\n");
		break;
	}

	printf("Framing error detected.\nTry the following debug steps:\n");
	printf("1) Disable transition to Recovery due to Framing Error:\n");
	printf("For debug purposes you set bit[16] of SD_CONTROL2_REG\n");
	printf("to disable transition to Recovery due to Framing error.\n");
	printf("2) Force transition to Recovery\n");
	printf("For debugging purposes you can set bit[1] of SD_CONTROL2_REG\n");
	printf("to force a transition to Recovery from L0 or L0s.\n");
}

/*
 * mftlp_err: To check for detailed Malformed packet errors
 * @value: Value of the Malformed Packet type
 * Return: void
 */
static void mftlp_err(int value)
{

	printf("Malformed packet detected. The error type is:\n");
	switch(value) {
	case 0x01:
		printf("AtomicOp address alignment.\n");
		break;
	case 0x02:
		printf("AtomicOp operand.\n");
		break;
	case 0x03:
		printf("AtomicOp byte enable.\n");
		break;
	case 0x04:
		printf("TLP length miss match.\n");
		break;
	case 0x05:
		printf("Max payload size.\n");
		break;
	case 0x06:
		printf("Message TLP without TC0.\n");
		break;
	case 0x07:
		printf("Invalid TC.\n");
		break;
	case 0x08:
		printf("Unexpected route bit in Message TLP.\n");
		break;
	case 0x09:
		printf("Unexpected CRS status in Completion TLP.\n");
		break;
	case 0x0A:
		printf("Byte enable.\n");
		break;
	case 0x0B:
		printf("Memory Address 4KB boundary.\n");
		break;
	case 0x0C:
		printf("TLP prefix rules.\n");
		break;
	case 0x0D:
		printf("Translation request rules.\n");
		break;
	case 0x0E:
		printf("Invalid TLP type.\n");
		break;
	case 0x0F:
		printf("Completion rules.\n");
		break;
	case 0x7F:
		printf("Application.\n");
		break;
	default:
		printf("Reserved.\n");
		break;
	}
}

/*
 * print_error_analysis: Function called to identify the errors
 * 		     detected and print debug suggestions.
 * Return: void
 */
static void print_error_analysis(struct pci_dev *pdev)
{
	unsigned int val, i, j;

	val = pci_read_long(pdev, DWC_PL0_DEBUG0) & 0x1F;

	if (val == 0x1 || val == 0x2)
		receiver_detect_err();
	if (val == 0x3 || val == 0x4)
		broken_lane_err();

	for (i = 0; i < sizeof(lane_events) / sizeof(lane_events[0]); i++) {
		for (j = 0; j < max_lanes; j++) {
			if (lane_events_res[i].val[j]) {
				printf("%s detected on lane %d\n", lane_events[i].name, j);
				if (lane_events_res[i].err_msg)
					printf("%s\n", lane_events_res[i].err_msg);
			}
		}
	}

	if (debug_res[MFTLP_STATUS].val)
		mftlp_err(debug_res[MFTLP_POINTER].val);

	if (debug_res[FRAMING_ERR].val)
		framing_err(debug_res[FRAMING_ERR_PTR].val);

	for (i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		if (events_res[i].val) {
			printf("%s detected.\n", events[i].name);
			if (events_res[i].err_msg)
				printf("%s\n", events_res[i].err_msg);
		}
	}

	for (i = 0; i < sizeof(aer) / sizeof(aer[0]); i++) {
		if (aer_res[i].val) {
			printf("%s detected.\n", aer[i].name);
			if (aer_res[i].err_msg)
				printf("%s\n", aer_res[i].err_msg);
		}
	}
}

/*
 * debugpci_capture: Function to enable relevant registers to capture
 * 		     data to be dumped later.
 * Return: void
 */
static void debugpci_capture(struct pci_dev *pdev)
{
	unsigned int i, j, val;

	for (i = 0; i < sizeof(lane_events) / sizeof(lane_events[0]); i++) {
		val = pci_read_long(pdev, ras_des_base + EVENT_OFFSET);
		val &= ~(EVENT_MASK);
		val |= lane_events[i].event_id << EVENT_SHIFT;
		val &= ~(GROUP_MASK);
		val |= lane_events[i].group_id << GROUP_SHIFT;
		val &= ~(EVENT_ENABLE_MASK);
		val |= EVENT_ENABLE << EVENT_ENABLE_SHIFT;
		for (j = 0; j < max_lanes; j++) {
			val &= ~(LANE_SEL_MASK);
			val |= j << LANE_SEL_SHIFT;
			pci_write_long(pdev, ras_des_base + EVENT_OFFSET, val);
		}
	}

	for (i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		val = pci_read_long(pdev, ras_des_base + EVENT_OFFSET);
		val &= ~(EVENT_MASK);
		val |= events[i].event_id << EVENT_SHIFT;
		val &= ~(GROUP_MASK);
		val |= events[i].group_id << GROUP_SHIFT;
		val &= ~(EVENT_ENABLE_MASK);
		val |= EVENT_ENABLE << EVENT_ENABLE_SHIFT;
		pci_write_long(pdev, ras_des_base + EVENT_OFFSET, val);
	}
	printf("Capture enabled\n");
}

/*
 * debugpci_dump: Function to dump captured data and print analysis
 * 		  by calling print_error_analysis().
 * Return: void
 */
static void debugpci_dump(struct pci_dev *pdev)
{
	unsigned int i, j, val;

	printf("Dumping debug data...\n==============================\n");

	print_link_info(pdev);

	for (i = 0; i < sizeof(lane_events) / sizeof(lane_events[0]); i++) {
		val = pci_read_long(pdev, ras_des_base + EVENT_OFFSET);
		val &= ~(EVENT_MASK);
		val |= lane_events[i].event_id << EVENT_SHIFT;
		val &= ~(GROUP_MASK);
		val |= lane_events[i].group_id << GROUP_SHIFT;
		val &= ~(EVENT_ENABLE_MASK);
		val |= 0x0 << EVENT_ENABLE_SHIFT;
		printf("%s:\n", lane_events[i].name);
		for (j = 0; j < max_lanes; j++) {
			val &= ~(LANE_SEL_MASK);
			val |= j << LANE_SEL_SHIFT;
			pci_write_long(pdev, ras_des_base + EVENT_OFFSET, val);
			lane_events_res[i].val[j] = pci_read_long(pdev, ras_des_base + EVENT_DATA_OFFSET);
			printf("\tLane %d:\t\t%d\n", j, lane_events_res[i].val[j]);
		}
	}

	for (i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		val = pci_read_long(pdev, ras_des_base + EVENT_OFFSET);
		val &= ~(EVENT_MASK);
		val |= events[i].event_id << EVENT_SHIFT;
		val &= ~(GROUP_MASK);
		val |= events[i].group_id << GROUP_SHIFT;
		val &= ~(EVENT_ENABLE_MASK);
		val |= 0x0 << EVENT_ENABLE_SHIFT;
		pci_write_long(pdev, ras_des_base + EVENT_OFFSET, val);
		events_res[i].val = pci_read_long(pdev, ras_des_base + EVENT_DATA_OFFSET);
		printf("%s:\t\t%d\n", events[i].name, events_res[i].val);
	}

	for (i = 0; i < sizeof(lane_debug) / sizeof(lane_debug[0]); i++) {
		printf("%s:\n", lane_debug[i].name);
		for (j = 0; j < max_lanes; j++) {
			val = pci_read_long(pdev, ras_des_base + lane_debug[i].offset);
			val &= ~(DWC_SD_STATUS_LANE_SEL_MASK);
			val |= j;
			pci_write_long(pdev, ras_des_base + lane_debug[i].offset, val);
			lane_debug_res[i].val[j] = (pci_read_long(pdev, ras_des_base +
						lane_debug[i].offset) >>
						lane_debug[i].shift) &
						lane_debug[i].mask;
			printf("\tLane %d:\t\t%d\n", j, lane_debug_res[i].val[j]);
		}
	}

	for (i = 0; i < sizeof(debug) / sizeof(debug[0]); i++) {
		debug_res[i].val = (pci_read_long(pdev, ras_des_base + debug[i].offset) >> debug[i].shift) & debug[i].mask;
		printf("%s:\t\t%d\n", debug[i].name, debug_res[i].val);
		if (i == LTSSM_VARIABLE)
			dump_l1ltssm_reg();
	}

	for (i = 0; i < sizeof(aer) / sizeof(aer[0]); i++) {
		aer_res[i].val = (pci_read_long(pdev, aer_base + aer[i].offset) >> aer[i].shift) & aer[i].mask;
		printf("%s:\t\t%d\n", aer[i].name, aer_res[i].val);
	}

	printf("\n\nAnalysis\n====================\n");
	print_error_analysis(pdev);
}

static void NONRET
usage(void)
{
	fprintf(stderr,
"Usage: dwc_debugpci <slot> [<options>]\n"
"General options:\n"
"c:\t\tEnable event counters capture in HW\n"
"d:\t\tDump all the debug data and present initial analysis\n\n"
"<slot>:\t\t[[[<domain>]:][<bus>]:][<slot>][.[<func>]]\n\n"
"example:\t\tdwc_debugpci 0000:00:00.0 c\n\n");
	exit(0);
}

int main(int argc, char **argv)
{
	struct pci_access *pacc;
	struct pci_dev *pdev;
	char *d;
	int ret;

	ret = 0;

	if (argc == 2) {
		if (!strcmp(argv[1], "--version")) {
			printf("Current Version: %s\n", DEBUGPCI_VERSION);
			exit(0);
		} else if (!strcmp(argv[1], "--help")) {
			usage();
		} else {
			fprintf(stderr, ".\nTry `dwc_debugpci --help' for more information.\n");
			exit(EINVAL);
		}
	}

	if (argc != 3) {
		fprintf(stderr, ".\nTry `dwc_debugpci --help' for more information.\n");
		exit(EINVAL);
	}

	/* Get the pci_access structure */
	pacc = pci_alloc();
	pci_filter_init(pacc, &filter);
	if (d = pci_filter_parse_slot(&filter, argv[1])) {
		printf("Unable to parse filter for device\n");
		fprintf(stderr, ".\nTry `dwc_debugpci --help' for more information.\n");
		ret = ENXIO;
		goto cleanup;
	}
	/* Initialize the PCI library */
	pci_init(pacc);
	/* We want to get the list of devices */
	pci_scan_bus(pacc);
	/* Iterate over all devices */
	for (pdev=pacc->devices; pdev; pdev=pdev->next)	{
		if (pci_filter_match(&filter, pdev))
			break;
	}

	ret = find_bases(pdev);
	if (ret < 0) {
		printf("Device does not support DWC debug registers\n");
		goto cleanup;
	}

	if (strcmp(argv[2], "c") == 0)
		debugpci_capture(pdev);
	else if (strcmp(argv[2], "d") == 0)
		debugpci_dump(pdev);
	else {
		printf("Wrong option\n");
		fprintf(stderr, ".\nTry `dwc_debugpci --help' for more information.\n");
		ret = EINVAL;
	}

cleanup:
	/* Close everything */
	pci_cleanup(pacc);
	exit(ret);
}

