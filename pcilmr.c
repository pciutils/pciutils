/*
 *	The PCI Utilities -- Margining utility main function
 *
 *	Copyright (c) 2023 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <getopt.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include "lmr/lmr.h"

const char program_name[] = "pcilmr";

static const char usage_msg[]
  = "Usage:\n"
    "pcilmr [<margining options>] <downstream component>\n\n"
    "Device Specifier:\n"
    "<device/component>:\t[<domain>:]<bus>:<dev>.<func>\n\n"
    "Margining options:\n\n"
    "Margining Test settings:\n"
    "-c\t\t\tPrint Device Lane Margining Capabilities only. Do not run margining.\n"
    "-l <lane>[,<lane>...]\tSpecify lanes for margining. Default: all link lanes.\n"
    "\t\t\tRemember that Device may use Lane Reversal for Lane numbering.\n"
    "\t\t\tHowever, utility uses logical lane numbers in arguments and for logging.\n"
    "\t\t\tUtility will automatically determine Lane Reversal and tune its calls.\n"
    "-e <errors>\t\tSpecify Error Count Limit for margining. Default: 4.\n"
    "-r <recvn>[,<recvn>...]\tSpecify Receivers to select margining targets.\n"
    "\t\t\tDefault: all available Receivers (including Retimers).\n"
    "-p <parallel_lanes>\tSpecify number of lanes to margin simultaneously.\n"
    "\t\t\tDefault: 1.\n"
    "\t\t\tAccording to spec it's possible for Receiver to margin up\n"
    "\t\t\tto MaxLanes + 1 lanes simultaneously, but usually this works\n"
    "\t\t\tbad, so this option is for experiments mostly.\n"
    "-T\t\t\tTime Margining will continue until the Error Count is no more\n"
    "\t\t\tthan an Error Count Limit. Use this option to find Link limit.\n"
    "-V\t\t\tSame as -T option, but for Voltage.\n"
    "-t <steps>\t\tSpecify maximum number of steps for Time Margining.\n"
    "-v <steps>\t\tSpecify maximum number of steps for Voltage Margining.\n"
    "Use only one of -T/-t options at the same time (same for -V/-v).\n"
    "Without these options utility will use MaxSteps from Device\n"
    "capabilities as test limit.\n\n";

static struct pci_dev *
dev_for_filter(struct pci_access *pacc, char *filter)
{
  struct pci_filter pci_filter;
  char dev[17] = { 0 };
  strncpy(dev, filter, sizeof(dev) - 1);
  pci_filter_init(pacc, &pci_filter);
  if (pci_filter_parse_slot(&pci_filter, dev))
    die("Invalid device ID: %s\n", filter);

  if (pci_filter.bus == -1 || pci_filter.slot == -1 || pci_filter.func == -1)
    die("Invalid device ID: %s\n", filter);

  if (pci_filter.domain == -1)
    pci_filter.domain = 0;

  for (struct pci_dev *p = pacc->devices; p; p = p->next)
    {
      if (pci_filter_match(&pci_filter, p))
        return p;
    }

  die("No such PCI device: %s or you don't have enough privileges.\n", filter);
}

static struct pci_dev *
find_down_port_for_up(struct pci_access *pacc, struct pci_dev *up)
{
  struct pci_dev *down = NULL;
  for (struct pci_dev *p = pacc->devices; p; p = p->next)
    {
      if (pci_read_byte(p, PCI_SECONDARY_BUS) == up->bus && up->domain == p->domain)
        {
          down = p;
          break;
        }
    }
  return down;
}

static u8
parse_csv_arg(char *arg, u8 *vals)
{
  u8 cnt = 0;
  char *token = strtok(arg, ",");
  while (token)
    {
      vals[cnt] = atoi(token);
      cnt++;
      token = strtok(NULL, ",");
    }
  return cnt;
}

int
main(int argc, char **argv)
{
  struct pci_access *pacc;

  struct pci_dev *up_port;
  struct pci_dev *down_port;

  struct margin_link link;

  bool status = true;

  struct margin_results *results;
  u8 results_n;

  struct margin_args args;

  u8 steps_t_arg = 0;
  u8 steps_v_arg = 0;
  u8 parallel_lanes_arg = 1;
  u8 error_limit = 4;

  u8 lanes_n = 0;
  u8 recvs_n = 0;

  bool run_margin = true;

  u64 total_steps = 0;

  pacc = pci_alloc();
  pci_init(pacc);
  pci_scan_bus(pacc);

  margin_print_domain = false;
  for (struct pci_dev *dev = pacc->devices; dev; dev = dev->next)
    {
      if (dev->domain != 0)
        {
          margin_print_domain = true;
          break;
        }
    }

  margin_global_logging = true;

  int c;

  while ((c = getopt(argc, argv, ":r:e:l:cp:t:v:VT")) != -1)
    {
      switch (c)
        {
          case 't':
            steps_t_arg = atoi(optarg);
            break;
          case 'T':
            steps_t_arg = 63;
            break;
          case 'v':
            steps_v_arg = atoi(optarg);
            break;
          case 'V':
            steps_v_arg = 127;
            break;
          case 'p':
            parallel_lanes_arg = atoi(optarg);
            break;
          case 'c':
            run_margin = false;
            break;
          case 'l':
            lanes_n = parse_csv_arg(optarg, args.lanes);
            break;
          case 'e':
            error_limit = atoi(optarg);
            break;
          case 'r':
            recvs_n = parse_csv_arg(optarg, args.recvs);
            break;
          default:
            die("Invalid arguments\n\n%s", usage_msg);
        }
    }

  if (optind != argc - 1)
    status = false;
  if (!status && argc > 1)
    die("Invalid arguments\n\n%s", usage_msg);
  if (!status)
    {
      printf("%s", usage_msg);
      exit(0);
    }

  up_port = dev_for_filter(pacc, argv[argc - 1]);

  down_port = find_down_port_for_up(pacc, up_port);
  if (!down_port)
    die("Cannot find Upstream Component for the specified device: %s\n", argv[argc - 1]);

  if (!pci_find_cap(up_port, PCI_CAP_ID_EXP, PCI_CAP_NORMAL))
    die("Looks like you don't have enough privileges to access "
        "Device Configuration Space.\nTry to run utility as root.\n");

  if (!margin_fill_link(down_port, up_port, &link))
    {
      printf("Link ");
      margin_log_bdfs(down_port, up_port);
      printf(" is not ready for margining.\n"
             "Link data rate must be 16 GT/s or 32 GT/s.\n"
             "Downstream Component must be at D0 PM state.\n");
      status = false;
    }

  if (status)
    {
      args.error_limit = error_limit;
      args.lanes_n = lanes_n;
      args.recvs_n = recvs_n;
      args.steps_t = steps_t_arg;
      args.steps_v = steps_v_arg;
      args.parallel_lanes = parallel_lanes_arg;
      args.run_margin = run_margin;
      args.verbosity = 1;
      args.steps_utility = &total_steps;

      enum margin_test_status args_status;

      if ((args_status = margin_process_args(&link.down_port, &args)) != MARGIN_TEST_OK)
        {
          status = false;
          margin_log_link(&link);
          if (args_status == MARGIN_TEST_ARGS_RECVS)
            margin_log("\nInvalid RecNums specified.\n");
          else if (args_status == MARGIN_TEST_ARGS_LANES)
            margin_log("\nInvalid lanes specified.\n");
        }
    }

  if (status)
    {
      struct margin_params params;

      for (int i = 0; i < args.recvs_n; i++)
        {
          if (margin_read_params(pacc, args.recvs[i] == 6 ? up_port : down_port, args.recvs[i],
                                 &params))
            {
              u8 steps_t = steps_t_arg ? steps_t_arg : params.timing_steps;
              u8 steps_v = steps_v_arg ? steps_v_arg : params.volt_steps;
              u8 parallel_recv = parallel_lanes_arg > params.max_lanes + 1 ? params.max_lanes + 1 :
                                                                             parallel_lanes_arg;

              u8 step_multiplier
                = args.lanes_n / parallel_recv + ((args.lanes_n % parallel_recv) > 0);

              total_steps += steps_t * step_multiplier;
              if (params.ind_left_right_tim)
                total_steps += steps_t * step_multiplier;
              if (params.volt_support)
                {
                  total_steps += steps_v * step_multiplier;
                  if (params.ind_up_down_volt)
                    total_steps += steps_v * step_multiplier;
                }
            }
        }

      results = margin_test_link(&link, &args, &results_n);
    }

  if (status && run_margin)
    {
      printf("\nResults:\n");
      printf("\nPass/fail criteria:\nTiming:\n");
      printf("Minimum Offset (spec): %d %% UI\nRecommended Offset: %d %% UI\n", MARGIN_TIM_MIN,
             MARGIN_TIM_RECOMMEND);
      printf("\nVoltage:\nMinimum Offset (spec): %d mV\n\n", MARGIN_VOLT_MIN);
      printf(
        "Margining statuses:\nLIM -\tErrorCount exceeded Error Count Limit (found device limit)\n");
      printf("NAK -\tDevice didn't execute last command, \n\tso result may be less reliable\n");
      printf("THR -\tThe set (using the utility options) \n\tstep threshold has been reached\n\n");
      printf("Notations:\nst - steps\n\n");

      margin_results_print_brief(results, results_n);
    }

  if (status)
    margin_free_results(results, results_n);

  pci_cleanup(pacc);
  return 0;
}
