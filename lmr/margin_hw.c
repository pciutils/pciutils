/*
 *	The PCI Utilities -- Verify and prepare devices before margining
 *
 *	Copyright (c) 2023 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "lmr.h"

static u16 special_hw[][4] =
  // Vendor ID, Device ID, Revision ID, margin_hw
  { { 0x8086, 0x347A, 0x4, MARGIN_ICE_LAKE_RC }, 
    { 0xFFFF, 0, 0, MARGIN_HW_DEFAULT } 
  };

static enum margin_hw
detect_unique_hw(struct pci_dev *dev)
{
  u16 vendor = pci_read_word(dev, PCI_VENDOR_ID);
  u16 device = pci_read_word(dev, PCI_DEVICE_ID);
  u8 revision = pci_read_byte(dev, PCI_REVISION_ID);

  for (int i = 0; special_hw[i][0] != 0xFFFF; i++)
    {
      if (vendor == special_hw[i][0] && device == special_hw[i][1] && revision == special_hw[i][2])
        return special_hw[i][3];
    }
  return MARGIN_HW_DEFAULT;
}

bool
margin_verify_link(struct pci_dev *down_port, struct pci_dev *up_port)
{
  struct pci_cap *cap = pci_find_cap(down_port, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
  if (!cap)
    return false;
  if ((pci_read_word(down_port, cap->addr + PCI_EXP_LNKSTA) & PCI_EXP_LNKSTA_SPEED) < 4)
    return false;
  if ((pci_read_word(down_port, cap->addr + PCI_EXP_LNKSTA) & PCI_EXP_LNKSTA_SPEED) > 5)
    return false;

  u8 down_type = pci_read_byte(down_port, PCI_HEADER_TYPE) & 0x7F;
  u8 down_sec = pci_read_byte(down_port, PCI_SECONDARY_BUS);
  u8 down_dir
    = GET_REG_MASK(pci_read_word(down_port, cap->addr + PCI_EXP_FLAGS), PCI_EXP_FLAGS_TYPE);

  // Verify that devices are linked, down_port is Root Port or Downstream Port of Switch,
  // up_port is Function 0 of a Device
  if (!(down_sec == up_port->bus && down_type == PCI_HEADER_TYPE_BRIDGE
        && (down_dir == PCI_EXP_TYPE_ROOT_PORT || down_dir == PCI_EXP_TYPE_DOWNSTREAM)
        && up_port->func == 0))
    return false;

  struct pci_cap *pm = pci_find_cap(up_port, PCI_CAP_ID_PM, PCI_CAP_NORMAL);
  return pm && !(pci_read_word(up_port, pm->addr + PCI_PM_CTRL) & PCI_PM_CTRL_STATE_MASK); // D0
}

bool
margin_check_ready_bit(struct pci_dev *dev)
{
  struct pci_cap *lmr = pci_find_cap(dev, PCI_EXT_CAP_ID_LMR, PCI_CAP_EXTENDED);
  return lmr && (pci_read_word(dev, lmr->addr + PCI_LMR_PORT_STS) & PCI_LMR_PORT_STS_READY);
}

/* Awaits device at 16 GT/s or higher */
static struct margin_dev
fill_dev_wrapper(struct pci_dev *dev)
{
  struct pci_cap *cap = pci_find_cap(dev, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
  struct margin_dev res
    = { .dev = dev,
        .lmr_cap_addr = pci_find_cap(dev, PCI_EXT_CAP_ID_LMR, PCI_CAP_EXTENDED)->addr,
        .width = GET_REG_MASK(pci_read_word(dev, cap->addr + PCI_EXP_LNKSTA), PCI_EXP_LNKSTA_WIDTH),
        .retimers_n
        = (!!(pci_read_word(dev, cap->addr + PCI_EXP_LNKSTA2) & PCI_EXP_LINKSTA2_RETIMER))
          + (!!(pci_read_word(dev, cap->addr + PCI_EXP_LNKSTA2) & PCI_EXP_LINKSTA2_2RETIMERS)),
        .link_speed = (pci_read_word(dev, cap->addr + PCI_EXP_LNKSTA) & PCI_EXP_LNKSTA_SPEED),
        .hw = detect_unique_hw(dev) };
  return res;
}

bool
margin_fill_link(struct pci_dev *down_port, struct pci_dev *up_port, struct margin_link *wrappers)
{
  if (!margin_verify_link(down_port, up_port))
    return false;
  wrappers->down_port = fill_dev_wrapper(down_port);
  wrappers->up_port = fill_dev_wrapper(up_port);
  return true;
}

/* Disable ASPM, set Hardware Autonomous Speed/Width Disable bits */
static bool
margin_prep_dev(struct margin_dev *dev)
{
  struct pci_cap *pcie = pci_find_cap(dev->dev, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
  if (!pcie)
    return false;

  u16 lnk_ctl = pci_read_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL);
  dev->aspm = lnk_ctl & PCI_EXP_LNKCTL_ASPM;
  dev->hawd = !!(lnk_ctl & PCI_EXP_LNKCTL_HWAUTWD);
  lnk_ctl &= ~PCI_EXP_LNKCTL_ASPM;
  pci_write_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL, lnk_ctl);
  if (pci_read_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL) & PCI_EXP_LNKCTL_ASPM)
    return false;

  lnk_ctl |= PCI_EXP_LNKCTL_HWAUTWD;
  pci_write_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL, lnk_ctl);

  u16 lnk_ctl2 = pci_read_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL2);
  dev->hasd = !!(lnk_ctl2 & PCI_EXP_LNKCTL2_SPEED_DIS);
  lnk_ctl2 |= PCI_EXP_LNKCTL2_SPEED_DIS;
  pci_write_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL2, lnk_ctl2);

  return true;
}

/* Restore Device ASPM, Hardware Autonomous Speed/Width settings */
static void
margin_restore_dev(struct margin_dev *dev)
{
  struct pci_cap *pcie = pci_find_cap(dev->dev, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
  if (!pcie)
    return;

  u16 lnk_ctl = pci_read_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL);
  lnk_ctl = SET_REG_MASK(lnk_ctl, PCI_EXP_LNKCAP_ASPM, dev->aspm);
  lnk_ctl = SET_REG_MASK(lnk_ctl, PCI_EXP_LNKCTL_HWAUTWD, dev->hawd);
  pci_write_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL, lnk_ctl);

  u16 lnk_ctl2 = pci_read_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL2);
  lnk_ctl2 = SET_REG_MASK(lnk_ctl2, PCI_EXP_LNKCTL2_SPEED_DIS, dev->hasd);
  pci_write_word(dev->dev, pcie->addr + PCI_EXP_LNKCTL2, lnk_ctl2);
}

bool
margin_prep_link(struct margin_link *link)
{
  if (!link)
    return false;
  if (!margin_prep_dev(&link->down_port))
    return false;
  if (!margin_prep_dev(&link->up_port))
    {
      margin_restore_dev(&link->down_port);
      return false;
    }
  return true;
}

void
margin_restore_link(struct margin_link *link)
{
  margin_restore_dev(&link->down_port);
  margin_restore_dev(&link->up_port);
}
