/*
 *	The PCI Utilities -- Log margining process
 *
 *	Copyright (c) 2023 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdarg.h>
#include <stdio.h>

#include "lmr.h"

bool margin_global_logging = false;
bool margin_print_domain = true;

void
margin_log(char *format, ...)
{
  va_list arg;
  va_start(arg, format);
  if (margin_global_logging)
    vprintf(format, arg);
  va_end(arg);
}

void
margin_log_bdfs(struct pci_dev *down, struct pci_dev *up)
{
  if (margin_print_domain)
    margin_log("%x:%x:%x.%x -> %x:%x:%x.%x", down->domain, down->bus, down->dev, down->func,
               up->domain, up->bus, up->dev, up->func);
  else
    margin_log("%x:%x.%x -> %x:%x.%x", down->bus, down->dev, down->func, up->bus, up->dev,
               up->func);
}

void
margin_log_link(struct margin_link *link)
{
  margin_log("Link ");
  margin_log_bdfs(link->down_port.dev, link->up_port.dev);
  margin_log("\nNegotiated Link Width: %d\n", link->down_port.width);
  margin_log("Link Speed: %d.0 GT/s = Gen %d\n", (link->down_port.link_speed - 3) * 16,
             link->down_port.link_speed);
  margin_log("Available receivers: ");
  int receivers_n = 2 + 2 * link->down_port.retimers_n;
  for (int i = 1; i < receivers_n; i++)
    margin_log("Rx(%X) - %d, ", 10 + i - 1, i);
  margin_log("Rx(F) - 6\n");
}

void
margin_log_params(struct margin_params *params)
{
  margin_log("Independent Error Sampler: %d\n", params->ind_error_sampler);
  margin_log("Sample Reporting Method: %d\n", params->sample_report_method);
  margin_log("Independent Left and Right Timing Margining: %d\n", params->ind_left_right_tim);
  margin_log("Voltage Margining Supported: %d\n", params->volt_support);
  margin_log("Independent Up and Down Voltage Margining: %d\n", params->ind_up_down_volt);
  margin_log("Number of Timing Steps: %d\n", params->timing_steps);
  margin_log("Number of Voltage Steps: %d\n", params->volt_steps);
  margin_log("Max Timing Offset: %d\n", params->timing_offset);
  margin_log("Max Voltage Offset: %d\n", params->volt_offset);
  margin_log("Max Lanes: %d\n", params->max_lanes);
}

void
margin_log_recvn(struct margin_recv *recv)
{
  margin_log("\nReceiver = Rx(%X)\n", 10 + recv->recvn - 1);
}

void
margin_log_receiver(struct margin_recv *recv)
{
  margin_log("\nError Count Limit = %d\n", recv->error_limit);
  margin_log("Parallel Lanes: %d\n\n", recv->parallel_lanes);

  margin_log_params(recv->params);

  if (recv->lane_reversal)
    {
      margin_log("\nWarning: device uses Lane Reversal.\n");
      margin_log("However, utility uses logical lane numbers in arguments and for logging.\n");
    }
}

void
margin_log_margining(struct margin_lanes_data arg)
{
  char *ind_dirs[] = { "Up", "Down", "Left", "Right" };
  char *non_ind_dirs[] = { "Voltage", "", "Timing" };

  if (arg.verbosity > 0)
    {
      margin_log("\033[2K\rMargining - ");
      if (arg.ind)
        margin_log("%s", ind_dirs[arg.dir]);
      else
        margin_log("%s", non_ind_dirs[arg.dir]);

      u8 lanes_counter = 0;
      margin_log(" - Lanes ");
      margin_log("[%d", arg.lanes_numbers[0]);
      for (int i = 1; i < arg.lanes_n; i++)
        {
          if (arg.lanes_numbers[i] - 1 == arg.lanes_numbers[i - 1])
            {
              lanes_counter++;
              if (lanes_counter == 1)
                margin_log("-");
              if (i + 1 == arg.lanes_n)
                margin_log("%d", arg.lanes_numbers[i]);
            }
          else
            {
              if (lanes_counter > 0)
                margin_log("%d", arg.lanes_numbers[i - 1]);
              margin_log(",%d", arg.lanes_numbers[i]);
              lanes_counter = 0;
            }
        }
      margin_log("]");

      u64 lane_eta_s = (arg.steps_lane_total - arg.steps_lane_done) * MARGIN_STEP_MS / 1000;
      u64 total_eta_s = *arg.steps_utility * MARGIN_STEP_MS / 1000 + lane_eta_s;
      margin_log(" - ETA: %3ds Steps: %3d Total ETA: %3dm %2ds", lane_eta_s, arg.steps_lane_done,
                 total_eta_s / 60, total_eta_s % 60);

      fflush(stdout);
    }
}

void
margin_log_hw_quirks(struct margin_recv *recv)
{
  switch (recv->dev->hw)
    {
      case MARGIN_ICE_LAKE_RC:
        if (recv->recvn == 1)
          margin_log("\nRx(A) is Intel Ice Lake RC port.\n"
                     "Applying next quirks for margining process:\n"
                     "  - Set MaxVoltageOffset to 12 (120 mV).\n");
        break;
      default:
        break;
    }
}
