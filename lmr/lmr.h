/*
 *	The PCI Utilities -- Margining utility main header
 *
 *	Copyright (c) 2023 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _LMR_H
#define _LMR_H

#include <stdbool.h>

#include "pciutils.h"

#define MARGIN_STEP_MS 1000

#define MARGIN_TIM_MIN       20
#define MARGIN_TIM_RECOMMEND 30
#define MARGIN_VOLT_MIN      50

/* PCI Device wrapper for margining functions */
struct margin_dev {
  struct pci_dev *dev;
  int lmr_cap_addr;
  u8 width;
  u8 retimers_n;
  u8 link_speed;

  /* Saved Device settings to restore after margining */
  u8 aspm;
  bool hasd; // Hardware Autonomous Speed Disable
  bool hawd; // Hardware Autonomous Width Disable
};

struct margin_link {
  struct margin_dev down_port;
  struct margin_dev up_port;
};

/* Specification Revision 5.0 Table 8-11 */
struct margin_params {
  bool ind_error_sampler;
  bool sample_report_method;
  bool ind_left_right_tim;
  bool ind_up_down_volt;
  bool volt_support;

  u8 max_lanes;

  u8 timing_steps;
  u8 timing_offset;

  u8 volt_steps;
  u8 volt_offset;

  u8 sample_rate_v;
  u8 sample_rate_t;
};

/* Step Margin Execution Status - Step command response */
enum margin_step_exec_sts {
  MARGIN_NAK = 0, // NAK/Set up for margin
  MARGIN_LIM,     // Too many errors (device limit)
  MARGIN_THR      // Test threshold has been reached
};

enum margin_dir { VOLT_UP = 0, VOLT_DOWN, TIM_LEFT, TIM_RIGHT };

/* Margining results of one lane of the receiver */
struct margin_res_lane {
  u8 lane;
  u8 steps[4];
  enum margin_step_exec_sts statuses[4];
};

/* Reason not to run margining test on the Link/Receiver */
enum margin_test_status {
  MARGIN_TEST_OK = 0,
  MARGIN_TEST_READY_BIT,
  MARGIN_TEST_CAPS,

  // Couldn't run test
  MARGIN_TEST_PREREQS,
  MARGIN_TEST_ARGS_LANES,
  MARGIN_TEST_ARGS_RECVS,
  MARGIN_TEST_ASPM
};

/* All lanes Receiver results */
struct margin_results {
  u8 recvn; // Receiver Number
  struct margin_params params;
  bool lane_reversal;
  u8 link_speed;

  enum margin_test_status test_status;

  /* Used to convert steps to physical quantity.
     Calculated from MaxOffset and NumSteps     */
  double tim_coef;
  double volt_coef;

  u8 lanes_n;
  struct margin_res_lane *lanes;
};

/* pcilmr arguments */
struct margin_args {
  u8 steps_t;        // 0 == use NumTimingSteps
  u8 steps_v;        // 0 == use NumVoltageSteps
  u8 parallel_lanes; // [1; MaxLanes + 1]
  u8 error_limit;    // [0; 63]
  u8 recvs[6];       // Receivers Numbers
  u8 recvs_n;        // 0 == margin all available receivers
  u8 lanes[32];      // Lanes to Margin
  u8 lanes_n;        // 0 == margin all available lanes
  bool run_margin;   // Or print params only
  u8 verbosity;      // 0 - basic;
                     // 1 - add info about remaining time and lanes in progress during margining

  u64 *steps_utility; // For ETA logging
};

/* Receiver structure */
struct margin_recv {
  struct margin_dev *dev;
  u8 recvn; // Receiver Number
  bool lane_reversal;
  struct margin_params *params;

  u8 parallel_lanes;
  u8 error_limit;
};

struct margin_lanes_data {
  struct margin_recv *recv;

  struct margin_res_lane *results;
  u8 *lanes_numbers;
  u8 lanes_n;

  bool ind;
  enum margin_dir dir;

  u8 steps_lane_done;
  u8 steps_lane_total;
  u64 *steps_utility;

  u8 verbosity;
};

/* margin_hw */

/* Verify that devices form the link with 16 GT/s or 32 GT/s data rate */
bool margin_verify_link(struct pci_dev *down_port, struct pci_dev *up_port);

/* Check Margining Ready bit from Margining Port Status Register */
bool margin_check_ready_bit(struct pci_dev *dev);

/* Verify link and fill wrappers */
bool margin_fill_link(struct pci_dev *down_port, struct pci_dev *up_port,
                      struct margin_link *wrappers);

/* Disable ASPM, set Hardware Autonomous Speed/Width Disable bits */
bool margin_prep_link(struct margin_link *link);

/* Restore ASPM, Hardware Autonomous Speed/Width settings */
void margin_restore_link(struct margin_link *link);

/* margin */

/* Fill margin_params without calling other functions */
bool margin_read_params(struct pci_access *pacc, struct pci_dev *dev, u8 recvn,
                        struct margin_params *params);

enum margin_test_status margin_process_args(struct margin_dev *dev, struct margin_args *args);

/* Awaits that args are prepared through process_args.
   Returns number of margined Receivers through recvs_n */
struct margin_results *margin_test_link(struct margin_link *link, struct margin_args *args,
                                        u8 *recvs_n);

void margin_free_results(struct margin_results *results, u8 results_n);

/* margin_log */

extern bool margin_global_logging;
extern bool margin_print_domain;

void margin_log(char *format, ...);

/* b:d.f -> b:d.f */
void margin_log_bdfs(struct pci_dev *down_port, struct pci_dev *up_port);

/* Print Link header (bdfs, width, speed) */
void margin_log_link(struct margin_link *link);

void margin_log_params(struct margin_params *params);

/* Print receiver number */
void margin_log_recvn(struct margin_recv *recv);

/* Print full info from Receiver struct */
void margin_log_receiver(struct margin_recv *recv);

/* Margining in progress log */
void margin_log_margining(struct margin_lanes_data arg);

/* margin_results */

void margin_results_print_brief(struct margin_results *results, u8 recvs_n);

#endif
