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

#endif
