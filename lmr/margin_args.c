/*
 *	The PCI Utilities -- Parse pcilmr utility arguments
 *
 *	Copyright (c) 2024 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lmr.h"

const char* usage
  = "! Utility requires preliminary preparation of the system. Refer to the pcilmr man page !\n\n"
    "Usage:\n"
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
    "capabilities as test limit.\n\n"
    "Margining Log settings:\n"
    "-o <directory>\t\tSave margining results in csv form into the\n"
    "\t\t\tspecified directory. Utility will generate file with the\n"
    "\t\t\tname in form of 'lmr_<downstream component>_Rx#_<timestamp>.csv'\n"
    "\t\t\tfor each successfully tested receiver.\n";

static struct pci_dev *
dev_for_filter(struct pci_access *pacc, char *filter)
{
  struct pci_filter pci_filter;
  pci_filter_init(pacc, &pci_filter);
  if (pci_filter_parse_slot(&pci_filter, filter))
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

static u8
find_ready_links(struct pci_access *pacc, struct margin_link *links, bool cnt_only)
{
  u8 cnt = 0;
  for (struct pci_dev *p = pacc->devices; p; p = p->next)
    {
      if (pci_find_cap(p, PCI_EXT_CAP_ID_LMR, PCI_CAP_EXTENDED) && margin_port_is_down(p))
        {
          struct pci_dev *down = NULL;
          struct pci_dev *up = NULL;
          margin_find_pair(pacc, p, &down, &up);

          if (down && margin_verify_link(down, up)
              && (margin_check_ready_bit(down) || margin_check_ready_bit(up)))
            {
              if (!cnt_only)
                margin_fill_link(down, up, &(links[cnt]));
              cnt++;
            }
        }
    }
  return cnt;
}

static void
init_link_args(struct margin_link_args *link_args, struct margin_com_args *com_args)
{
  memset(link_args, 0, sizeof(*link_args));
  link_args->common = com_args;
  link_args->parallel_lanes = 1;
}

static void
parse_dev_args(int argc, char **argv, struct margin_link_args *args, u8 link_speed)
{
  if (argc == optind)
    return;
  int c;
  while ((c = getopt(argc, argv, "+r:l:p:t:v:VT")) != -1)
    {
      switch (c)
        {
          case 't':
            args->steps_t = atoi(optarg);
            break;
          case 'T':
            args->steps_t = 63;
            break;
          case 'v':
            args->steps_v = atoi(optarg);
            break;
          case 'V':
            args->steps_v = 127;
            break;
          case 'p':
            args->parallel_lanes = atoi(optarg);
            break;
          case 'l':
            args->lanes_n = parse_csv_arg(optarg, args->lanes);
            break;
          case 'r':
            args->recvs_n = parse_csv_arg(optarg, args->recvs);
            break;
          case '?':
            die("Invalid arguments\n\n%s", usage);
            break;
          default:
            return;
        }
    }
}

struct margin_link *
margin_parse_util_args(struct pci_access *pacc, int argc, char **argv, enum margin_mode mode,
                       u8 *links_n)
{
  struct margin_com_args *com_args = xmalloc(sizeof(*com_args));
  com_args->error_limit = 4;
  com_args->run_margin = true;
  com_args->verbosity = 1;
  com_args->steps_utility = 0;
  com_args->dir_for_csv = NULL;
  com_args->save_csv = false;

  int c;
  while ((c = getopt(argc, argv, "+e:co:")) != -1)
    {
      switch (c)
        {
          case 'c':
            com_args->run_margin = false;
            break;
          case 'e':
            com_args->error_limit = atoi(optarg);
            break;
          case 'o':
            com_args->dir_for_csv = optarg;
            com_args->save_csv = true;
            break;
          default:
            die("Invalid arguments\n\n%s", usage);
        }
    }

  bool status = true;
  if (mode == FULL && optind != argc)
    status = false;
  if (mode == MARGIN && optind == argc)
    status = false;
  if (!status && argc > 1)
    die("Invalid arguments\n\n%s", usage);
  if (!status)
    {
      printf("%s", usage);
      exit(0);
    }

  u8 ports_n = 0;
  struct margin_link *links = NULL;
  char err[128];

  if (mode == FULL)
    {
      ports_n = find_ready_links(pacc, NULL, true);
      if (ports_n == 0)
        die("Links not found or you don't have enough privileges.\n");
      else
        {
          links = xmalloc(ports_n * sizeof(*links));
          find_ready_links(pacc, links, false);
          for (int i = 0; i < ports_n; i++)
            init_link_args(&(links[i].args), com_args);
        }
    }
  else if (mode == MARGIN)
    {
      while (optind != argc)
        {
          struct pci_dev *dev = dev_for_filter(pacc, argv[optind]);
          optind++;
          links = xrealloc(links, (ports_n + 1) * sizeof(*links));
          struct pci_dev *down;
          struct pci_dev *up;
          if (!margin_find_pair(pacc, dev, &down, &up))
            die("Cannot find pair for the specified device: %s\n", argv[optind]);
          struct pci_cap *cap = pci_find_cap(down, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
          if (!cap)
            die("Looks like you don't have enough privileges to access "
                "Device Configuration Space.\nTry to run utility as root.\n");
          if (!margin_fill_link(down, up, &(links[ports_n])))
            {
              margin_gen_bdfs(down, up, err, sizeof(err));
              die("Link %s is not ready for margining.\n"
                  "Link data rate must be 16 GT/s or 32 GT/s.\n"
                  "Downstream Component must be at D0 PM state.\n",
                  err);
            }
          init_link_args(&(links[ports_n].args), com_args);
          parse_dev_args(argc, argv, &(links[ports_n].args),
                         links[ports_n].down_port.link_speed - 4);
          ports_n++;
        }
    }
  else
    die("Bug in the args parsing!\n");

  *links_n = ports_n;
  return links;
}
