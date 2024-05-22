/*
 *	The PCI Utilities -- Margining utility main function
 *
 *	Copyright (c) 2023-2024 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include "lmr/lmr.h"

const char program_name[] = "pcilmr";

static void
scan_links(struct pci_access *pacc, bool only_ready)
{
  if (only_ready)
    printf("Links ready for margining:\n");
  else
    printf("Links with Lane Margining at the Receiver capabilities:\n");
  bool flag = true;
  for (struct pci_dev *p = pacc->devices; p; p = p->next)
    {
      if (pci_find_cap(p, PCI_EXT_CAP_ID_LMR, PCI_CAP_EXTENDED) && margin_port_is_down(p))
        {
          struct pci_dev *down = NULL;
          struct pci_dev *up = NULL;
          margin_find_pair(pacc, p, &down, &up);

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

int
main(int argc, char **argv)
{
  struct pci_access *pacc;

  u8 links_n = 0;
  struct margin_link *links;
  bool *checks_status_ports;

  enum margin_mode mode;

  /* each link has several receivers -> several results */
  struct margin_results **results;
  u8 *results_n;

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

  opterr = 0;
  int c;
  c = getopt_long(argc, argv, "+", long_options, NULL);

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
        else
          die("Invalid arguments\n\n%s", usage);
        break;
      case 2:
        mode = FULL;
        break;
      default: /* unknown option symbol */
        mode = MARGIN;
        optind--;
        break;
    }

  opterr = 1;

  links = margin_parse_util_args(pacc, argc, argv, mode, &links_n);
  struct margin_com_args *com_args = links[0].args.common;

  results = xmalloc(links_n * sizeof(*results));
  results_n = xmalloc(links_n * sizeof(*results_n));
  checks_status_ports = xmalloc(links_n * sizeof(*checks_status_ports));

  for (int i = 0; i < links_n; i++)
    {
      enum margin_test_status args_status;

      if ((args_status = margin_process_args(&links[i])) != MARGIN_TEST_OK)
        {
          checks_status_ports[i] = false;
          results[i] = xmalloc(sizeof(*results[i]));
          results[i]->test_status = args_status;
          continue;
        }

      checks_status_ports[i] = true;
      struct margin_params params;
      struct margin_link_args *link_args = &links[i].args;

      for (int j = 0; j < link_args->recvs_n; j++)
        {
          if (margin_read_params(
                pacc, link_args->recvs[j] == 6 ? links[i].up_port.dev : links[i].down_port.dev,
                link_args->recvs[j], &params))
            {
              u8 steps_t = link_args->steps_t ? link_args->steps_t : params.timing_steps;
              u8 steps_v = link_args->steps_v ? link_args->steps_v : params.volt_steps;
              u8 parallel_recv = link_args->parallel_lanes > params.max_lanes + 1 ?
                                   params.max_lanes + 1 :
                                   link_args->parallel_lanes;

              u8 step_multiplier
                = link_args->lanes_n / parallel_recv + ((link_args->lanes_n % parallel_recv) > 0);

              com_args->steps_utility += steps_t * step_multiplier;
              if (params.ind_left_right_tim)
                com_args->steps_utility += steps_t * step_multiplier;
              if (params.volt_support)
                {
                  com_args->steps_utility += steps_v * step_multiplier;
                  if (params.ind_up_down_volt)
                    com_args->steps_utility += steps_v * step_multiplier;
                }
            }
        }
    }

  for (int i = 0; i < links_n; i++)
    {
      if (checks_status_ports[i])
        results[i] = margin_test_link(&links[i], &results_n[i]);
      else
        {
          results_n[i] = 1;
          if (results[i]->test_status == MARGIN_TEST_ARGS_RECVS)
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

  if (com_args->run_margin)
    {
      printf("Results:\n");
      printf(
        "Margining statuses:\nLIM -\tErrorCount exceeded Error Count Limit (found device limit)\n");
      printf("NAK -\tDevice didn't execute last command, \n\tso result may be less reliable\n");
      printf("THR -\tThe set (using the utility options) \n\tstep threshold has been reached\n\n");
      printf("Notations:\nst - steps\n\n");

      for (int i = 0; i < links_n; i++)
        {
          printf("Link ");
          margin_log_bdfs(links[i].down_port.dev, links[i].up_port.dev);
          printf(":\n\n");
          margin_results_print_brief(results[i], results_n[i], &links[i].args);
          if (com_args->save_csv)
            margin_results_save_csv(results[i], results_n[i], &links[i]);
          printf("\n");
        }
    }

  for (int i = 0; i < links_n; i++)
    margin_free_results(results[i], results_n[i]);
  free(results_n);
  free(results);
  free(com_args);
  free(links);
  free(checks_status_ports);

  pci_cleanup(pacc);
  return 0;
}
