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

enum mode { MARGIN, FULL, SCAN };

static const char usage_msg[]
  = "Usage:\n"
    "pcilmr [--margin] [<margining options>] <downstream component> ...\n"
    "pcilmr --full [<margining options>]\n"
    "pcilmr --scan\n\n"
    "Device Specifier:\n"
    "<device/component>:\t[<domain>:]<bus>:<dev>.<func>\n\n"
    "Modes:\n"
    "--margin\t\tMargin selected Links\n"
    "--full\t\t\tMargin all ready for testing Links in the system (one by one)\n"
    "--scan\t\t\tScan for Links available for margining\n\n"
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

static void
scan_links(struct pci_access *pacc, bool only_ready)
{
  if (only_ready)
    printf("Links ready for margining:\n");
  else
    printf("Links with Lane Margining at the Receiver capabilities:\n");
  bool flag = true;
  for (struct pci_dev *up = pacc->devices; up; up = up->next)
    {
      if (pci_find_cap(up, PCI_EXT_CAP_ID_LMR, PCI_CAP_EXTENDED))
        {
          struct pci_dev *down = find_down_port_for_up(pacc, up);

          if (down && margin_verify_link(down, up))
            {
              margin_log_bdfs(down, up);
              if (!only_ready && (margin_check_ready_bit(down) || margin_check_ready_bit(up)))
                printf(" - Ready");
              printf("\n");
              flag = false;
            }
        }
    }
  if (flag)
    printf("Links not found or you don't have enough privileges.\n");
  pci_cleanup(pacc);
  exit(0);
}

static u8
find_ready_links(struct pci_access *pacc, struct pci_dev **down_ports, struct pci_dev **up_ports,
                 bool cnt_only)
{
  u8 cnt = 0;
  for (struct pci_dev *up = pacc->devices; up; up = up->next)
    {
      if (pci_find_cap(up, PCI_EXT_CAP_ID_LMR, PCI_CAP_EXTENDED))
        {
          struct pci_dev *down = find_down_port_for_up(pacc, up);

          if (down && margin_verify_link(down, up)
              && (margin_check_ready_bit(down) || margin_check_ready_bit(up)))
            {
              if (!cnt_only)
                {
                  up_ports[cnt] = up;
                  down_ports[cnt] = down;
                }
              cnt++;
            }
        }
    }
  return cnt;
}

int
main(int argc, char **argv)
{
  struct pci_access *pacc;

  struct pci_dev **up_ports;
  struct pci_dev **down_ports;
  u8 ports_n = 0;

  struct margin_link *links;
  bool *checks_status_ports;

  bool status = true;
  enum mode mode;

  /* each link has several receivers -> several results */
  struct margin_results **results;
  u8 *results_n;

  struct margin_args *args;

  u8 steps_t_arg = 0;
  u8 steps_v_arg = 0;
  u8 parallel_lanes_arg = 1;
  u8 error_limit = 4;
  u8 lanes_arg[32];
  u8 recvs_arg[6];

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

  struct option long_options[]
    = { { .name = "margin", .has_arg = no_argument, .flag = NULL, .val = 0 },
        { .name = "scan", .has_arg = no_argument, .flag = NULL, .val = 1 },
        { .name = "full", .has_arg = no_argument, .flag = NULL, .val = 2 },
        { 0, 0, 0, 0 } };

  int c;
  c = getopt_long(argc, argv, ":", long_options, NULL);

  switch (c)
    {
      case -1: /* no options (strings like component are possible) */
        /* FALLTHROUGH */
      case 0:
        mode = MARGIN;
        break;
      case 1:
        mode = SCAN;
        if (optind == argc)
          scan_links(pacc, false);
        optind--;
        break;
      case 2:
        mode = FULL;
        break;
      default: /* unknown option symbol */
        mode = MARGIN;
        optind--;
        break;
    }

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
            lanes_n = parse_csv_arg(optarg, lanes_arg);
            break;
          case 'e':
            error_limit = atoi(optarg);
            break;
          case 'r':
            recvs_n = parse_csv_arg(optarg, recvs_arg);
            break;
          default:
            die("Invalid arguments\n\n%s", usage_msg);
        }
    }

  if (mode == FULL && optind != argc)
    status = false;
  if (mode == MARGIN && optind == argc)
    status = false;
  if (!status && argc > 1)
    die("Invalid arguments\n\n%s", usage_msg);
  if (!status)
    {
      printf("%s", usage_msg);
      exit(0);
    }

  if (mode == FULL)
    {
      ports_n = find_ready_links(pacc, NULL, NULL, true);
      if (ports_n == 0)
        {
          die("Links not found or you don't have enough privileges.\n");
        }
      else
        {
          up_ports = xmalloc(ports_n * sizeof(*up_ports));
          down_ports = xmalloc(ports_n * sizeof(*down_ports));
          find_ready_links(pacc, down_ports, up_ports, false);
        }
    }
  else if (mode == MARGIN)
    {
      ports_n = argc - optind;
      up_ports = xmalloc(ports_n * sizeof(*up_ports));
      down_ports = xmalloc(ports_n * sizeof(*down_ports));

      u8 cnt = 0;
      while (optind != argc)
        {
          up_ports[cnt] = dev_for_filter(pacc, argv[optind]);
          down_ports[cnt] = find_down_port_for_up(pacc, up_ports[cnt]);
          if (!down_ports[cnt])
            die("Cannot find Upstream Component for the specified device: %s\n", argv[optind]);
          cnt++;
          optind++;
        }
    }
  else
    die("Bug in the args parsing!\n");

  if (!pci_find_cap(up_ports[0], PCI_CAP_ID_EXP, PCI_CAP_NORMAL))
    die("Looks like you don't have enough privileges to access "
        "Device Configuration Space.\nTry to run utility as root.\n");

  results = xmalloc(ports_n * sizeof(*results));
  results_n = xmalloc(ports_n * sizeof(*results_n));
  links = xmalloc(ports_n * sizeof(*links));
  checks_status_ports = xmalloc(ports_n * sizeof(*checks_status_ports));
  args = xmalloc(ports_n * sizeof(*args));

  for (int i = 0; i < ports_n; i++)
    {
      args[i].error_limit = error_limit;
      args[i].parallel_lanes = parallel_lanes_arg;
      args[i].run_margin = run_margin;
      args[i].verbosity = 1;
      args[i].steps_t = steps_t_arg;
      args[i].steps_v = steps_v_arg;
      for (int j = 0; j < recvs_n; j++)
        args[i].recvs[j] = recvs_arg[j];
      args[i].recvs_n = recvs_n;
      for (int j = 0; j < lanes_n; j++)
        args[i].lanes[j] = lanes_arg[j];
      args[i].lanes_n = lanes_n;
      args[i].steps_utility = &total_steps;

      enum margin_test_status args_status;

      if (!margin_fill_link(down_ports[i], up_ports[i], &links[i]))
        {
          checks_status_ports[i] = false;
          results[i] = xmalloc(sizeof(*results[i]));
          results[i]->test_status = MARGIN_TEST_PREREQS;
          continue;
        }

      if ((args_status = margin_process_args(&links[i].down_port, &args[i])) != MARGIN_TEST_OK)
        {
          checks_status_ports[i] = false;
          results[i] = xmalloc(sizeof(*results[i]));
          results[i]->test_status = args_status;
          continue;
        }

      checks_status_ports[i] = true;
      struct margin_params params;

      for (int j = 0; j < args[i].recvs_n; j++)
        {
          if (margin_read_params(pacc, args[i].recvs[j] == 6 ? up_ports[i] : down_ports[i],
                                 args[i].recvs[j], &params))
            {
              u8 steps_t = steps_t_arg ? steps_t_arg : params.timing_steps;
              u8 steps_v = steps_v_arg ? steps_v_arg : params.volt_steps;
              u8 parallel_recv = parallel_lanes_arg > params.max_lanes + 1 ? params.max_lanes + 1 :
                                                                             parallel_lanes_arg;

              u8 step_multiplier
                = args[i].lanes_n / parallel_recv + ((args[i].lanes_n % parallel_recv) > 0);

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
    }

  for (int i = 0; i < ports_n; i++)
    {
      if (checks_status_ports[i])
        results[i] = margin_test_link(&links[i], &args[i], &results_n[i]);
      else
        {
          results_n[i] = 1;
          if (results[i]->test_status == MARGIN_TEST_PREREQS)
            {
              printf("Link ");
              margin_log_bdfs(down_ports[i], up_ports[i]);
              printf(" is not ready for margining.\n"
                     "Link data rate must be 16 GT/s or 32 GT/s.\n"
                     "Downstream Component must be at D0 PM state.\n");
            }
          else if (results[i]->test_status == MARGIN_TEST_ARGS_RECVS)
            {
              margin_log_link(&links[i]);
              printf("\nInvalid RecNums specified.\n");
            }
          else if (results[i]->test_status == MARGIN_TEST_ARGS_LANES)
            {
              margin_log_link(&links[i]);
              printf("\nInvalid lanes specified.\n");
            }
        }
      printf("\n----\n\n");
    }

  if (run_margin)
    {
      printf("Results:\n");
      printf("\nPass/fail criteria:\nTiming:\n");
      printf("Minimum Offset (spec): %d %% UI\nRecommended Offset: %d %% UI\n", MARGIN_TIM_MIN,
             MARGIN_TIM_RECOMMEND);
      printf("\nVoltage:\nMinimum Offset (spec): %d mV\n\n", MARGIN_VOLT_MIN);
      printf(
        "Margining statuses:\nLIM -\tErrorCount exceeded Error Count Limit (found device limit)\n");
      printf("NAK -\tDevice didn't execute last command, \n\tso result may be less reliable\n");
      printf("THR -\tThe set (using the utility options) \n\tstep threshold has been reached\n\n");
      printf("Notations:\nst - steps\n\n");

      for (int i = 0; i < ports_n; i++)
        {
          printf("Link ");
          margin_log_bdfs(down_ports[i], up_ports[i]);
          printf(":\n\n");
          margin_results_print_brief(results[i], results_n[i]);
          printf("\n");
        }
    }

  for (int i = 0; i < ports_n; i++)
    margin_free_results(results[i], results_n[i]);
  free(results_n);
  free(results);
  free(up_ports);
  free(down_ports);
  free(links);
  free(checks_status_ports);
  free(args);

  pci_cleanup(pacc);
  return 0;
}
