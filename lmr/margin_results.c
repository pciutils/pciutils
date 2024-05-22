/*
 *	The PCI Utilities -- Display/save margining results
 *
 *	Copyright (c) 2023-2024 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lmr.h"

enum lane_rating {
  FAIL = 0,
  PASS,
  PERFECT,
  INIT,
};

static char *const grades[] = { "Fail", "Pass", "Perfect" };
static char *const sts_strings[] = { "NAK", "LIM", "THR" };

static enum lane_rating
rate_lane(double value, double min, double recommended, enum lane_rating cur_rate)
{
  enum lane_rating res = PERFECT;
  if (value < recommended)
    res = PASS;
  if (value < min)
    res = FAIL;
  if (cur_rate == INIT)
    return res;
  if (res < cur_rate)
    return res;
  else
    return cur_rate;
}

void
margin_results_print_brief(struct margin_results *results, u8 recvs_n,
                           struct margin_link_args *args)
{
  struct margin_res_lane *lane;
  struct margin_results *res;
  struct margin_params params;

  enum lane_rating lane_rating;

  u8 link_speed;

  struct margin_recv_args grade_args;
  bool spec_ref_only;

  double ew_min;
  double ew_rec;
  double eh_min;
  double eh_rec;

  char *no_test_msgs[] = { "",
                           "Margining Ready bit is Clear",
                           "Error during caps reading",
                           "Margining prerequisites are not satisfied (16/32 GT/s, D0)",
                           "Invalid lanes specified with arguments",
                           "Invalid receivers specified with arguments",
                           "Couldn't disable ASPM" };

  for (int i = 0; i < recvs_n; i++)
    {
      res = &(results[i]);
      params = res->params;
      link_speed = res->link_speed - 4;

      if (res->test_status != MARGIN_TEST_OK)
        {
          if (res->test_status < MARGIN_TEST_PREREQS)
            printf("Rx(%X) -", 10 + res->recvn - 1);
          printf(" Couldn't run test (%s)\n\n", no_test_msgs[res->test_status]);
          continue;
        }

      spec_ref_only = true;
      grade_args = args->recv_args[res->recvn - 1];
      if (grade_args.t.criteria != 0)
        {
          spec_ref_only = false;
          ew_min = grade_args.t.criteria;
          ew_rec = grade_args.t.criteria;
        }
      else
        {
          ew_min = margin_ew_min[link_speed];
          ew_rec = margin_ew_rec[link_speed];
        }

      if (grade_args.v.criteria != 0)
        {
          spec_ref_only = false;
          eh_min = grade_args.v.criteria;
          eh_rec = grade_args.v.criteria;
        }
      else
        {
          eh_min = margin_eh_min[link_speed];
          eh_rec = margin_eh_rec[link_speed];
        }

      printf("Rx(%X) - Grading criteria:\n", 10 + res->recvn - 1);
      if (spec_ref_only)
        {
          printf("\tUsing spec only:\n");
          printf("\tEW: minimum - %.2f ps; recommended - %.2f ps\n", ew_min, ew_rec);
          printf("\tEH: minimum - %.2f mV; recommended - %.2f mV\n\n", eh_min, eh_rec);
        }
      else
        {
          printf("\tEW: pass - %.2f ps\n", ew_min);
          printf("\tEH: pass - %.2f mV\n\n", eh_min);
        }

      if (!params.ind_left_right_tim)
        {
          printf("Rx(%X) - EW: independent left/right timing margin is not supported:\n",
                 10 + res->recvn - 1);
          if (grade_args.t.one_side_is_whole)
            printf("\tmanual setting - the entire margin across the eye "
                   "is what is reported by one side margining\n\n");
          else
            printf("\tdefault - calculating EW as double one side result\n\n");
        }

      if (params.volt_support && !params.ind_up_down_volt)
        {
          printf("Rx(%X) - EH: independent up and down voltage margining is not supported:\n",
                 10 + res->recvn - 1);
          if (grade_args.v.one_side_is_whole)
            printf("\tmanual setting - the entire margin across the eye "
                   "is what is reported by one side margining\n\n");
          else
            printf("\tdefault - calculating EH as double one side result\n\n");
        }

      if (res->lane_reversal)
        printf("Rx(%X) - Lane Reversal\n", 10 + res->recvn - 1);

      if (!res->tim_off_reported)
        printf("Rx(%X) - Attention: Vendor chose not to report the Max Timing Offset.\n"
               "Utility used its max possible value (50%% UI) for calculations of %% UI and ps.\n"
               "Keep in mind that for timing results of this receiver only steps values are "
               "reliable.\n\n",
               10 + res->recvn - 1);
      if (params.volt_support && !res->volt_off_reported)
        printf("Rx(%X) - Attention: Vendor chose not to report the Max Voltage Offset.\n"
               "Utility used its max possible value (500 mV) for calculations of mV.\n"
               "Keep in mind that for voltage results of this receiver only steps values are "
               "reliable.\n\n",
               10 + res->recvn - 1);

      for (int j = 0; j < res->lanes_n; j++)
        {
          if (spec_ref_only)
            lane_rating = INIT;
          else
            lane_rating = PASS;

          lane = &(res->lanes[j]);
          double left_ps = lane->steps[TIM_LEFT] * res->tim_coef / 100.0 * margin_ui[link_speed];
          double right_ps = lane->steps[TIM_RIGHT] * res->tim_coef / 100.0 * margin_ui[link_speed];
          double up_volt = lane->steps[VOLT_UP] * res->volt_coef;
          double down_volt = lane->steps[VOLT_DOWN] * res->volt_coef;

          double ew = left_ps;
          if (params.ind_left_right_tim)
            ew += right_ps;
          else if (!grade_args.t.one_side_is_whole)
            ew *= 2.0;

          double eh = 0.0;
          if (params.volt_support)
            {
              eh += up_volt;
              if (params.ind_up_down_volt)
                eh += down_volt;
              else if (!grade_args.v.one_side_is_whole)
                eh *= 2.0;
            }

          lane_rating = rate_lane(ew, ew_min, ew_rec, lane_rating);
          if (params.volt_support)
            lane_rating = rate_lane(eh, eh_min, eh_rec, lane_rating);

          printf("Rx(%X) Lane %2d: %s\t (W %4.1f%% UI - %5.2fps", 10 + res->recvn - 1, lane->lane,
                 grades[lane_rating], ew / margin_ui[link_speed] * 100.0, ew);
          if (params.volt_support)
            printf(", H %5.1f mV", eh);
          if (params.ind_left_right_tim)
            printf(")  (L %4.1f%% UI - %5.2fps - %2dst %s)  (R %4.1f%% UI - %5.2fps - %2dst %s)",
                   left_ps / margin_ui[link_speed] * 100.0, left_ps, lane->steps[TIM_LEFT],
                   sts_strings[lane->statuses[TIM_LEFT]], right_ps / margin_ui[link_speed] * 100.0,
                   right_ps, lane->steps[TIM_RIGHT], sts_strings[lane->statuses[TIM_RIGHT]]);
          else
            printf(")  (T %4.1f%% UI - %5.2fps - %2dst %s)",
                   left_ps / margin_ui[link_speed] * 100.0, left_ps, lane->steps[TIM_LEFT],
                   sts_strings[lane->statuses[TIM_LEFT]]);
          if (params.volt_support)
            {
              if (params.ind_up_down_volt)
                printf("  (U %5.1f mV - %3dst %s)  (D %5.1f mV - %3dst %s)", up_volt,
                       lane->steps[VOLT_UP], sts_strings[lane->statuses[VOLT_UP]], down_volt,
                       lane->steps[VOLT_DOWN], sts_strings[lane->statuses[VOLT_DOWN]]);
              else
                printf("  (V %5.1f mV - %3dst %s)", up_volt, lane->steps[VOLT_UP],
                       sts_strings[lane->statuses[VOLT_UP]]);
            }
          printf("\n");
        }
      printf("\n");
    }
}

void
margin_results_save_csv(struct margin_results *results, u8 recvs_n, struct margin_link *link)
{
  char timestamp[64];
  time_t tim = time(NULL);
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", gmtime(&tim));

  char *dir = link->args.common->dir_for_csv;
  size_t pathlen = strlen(dir) + 128;
  char *path = xmalloc(pathlen);
  FILE *csv;

  struct margin_res_lane *lane;
  struct margin_results *res;
  struct margin_params params;

  enum lane_rating lane_rating;
  u8 link_speed;

  struct margin_recv_args grade_args;
  bool spec_ref_only;

  double ew_min;
  double ew_rec;
  double eh_min;
  double eh_rec;

  struct pci_dev *port;

  for (int i = 0; i < recvs_n; i++)
    {
      res = &(results[i]);
      params = res->params;
      link_speed = res->link_speed - 4;

      if (res->test_status != MARGIN_TEST_OK)
        continue;

      port = res->recvn == 6 ? link->up_port.dev : link->down_port.dev;
      snprintf(path, pathlen, "%s/lmr_%0*x.%02x.%02x.%x_Rx%X_%s.csv", dir,
               port->domain_16 == 0xffff ? 8 : 4, port->domain, port->bus, port->dev, port->func,
               10 + res->recvn - 1, timestamp);
      csv = fopen(path, "w");
      if (!csv)
        die("Error while saving %s\n", path);

      fprintf(csv, "Lane,EW Min,EW Rec,EW,EH Min,EH Rec,EH,Lane Status,Left %% UI,Left "
                   "ps,Left Steps,Left Status,Right %% UI,Right ps,Right Steps,Right Status,Up "
                   "mV,Up Steps,Up Status,Down mV,Down Steps,Down Status\n");

      spec_ref_only = true;
      grade_args = link->args.recv_args[res->recvn - 1];
      if (grade_args.t.criteria != 0)
        {
          spec_ref_only = false;
          ew_min = grade_args.t.criteria;
          ew_rec = grade_args.t.criteria;
        }
      else
        {
          ew_min = margin_ew_min[link_speed];
          ew_rec = margin_ew_rec[link_speed];
        }
      if (grade_args.v.criteria != 0)
        {
          spec_ref_only = false;
          eh_min = grade_args.v.criteria;
          eh_rec = grade_args.v.criteria;
        }
      else
        {
          eh_min = margin_eh_min[link_speed];
          eh_rec = margin_eh_rec[link_speed];
        }

      for (int j = 0; j < res->lanes_n; j++)
        {
          if (spec_ref_only)
            lane_rating = INIT;
          else
            lane_rating = PASS;

          lane = &(res->lanes[j]);
          double left_ps = lane->steps[TIM_LEFT] * res->tim_coef / 100.0 * margin_ui[link_speed];
          double right_ps = lane->steps[TIM_RIGHT] * res->tim_coef / 100.0 * margin_ui[link_speed];
          double up_volt = lane->steps[VOLT_UP] * res->volt_coef;
          double down_volt = lane->steps[VOLT_DOWN] * res->volt_coef;

          double ew = left_ps;
          if (params.ind_left_right_tim)
            ew += right_ps;
          else if (!grade_args.t.one_side_is_whole)
            ew *= 2.0;

          double eh = 0.0;
          if (params.volt_support)
            {
              eh += up_volt;
              if (params.ind_up_down_volt)
                eh += down_volt;
              else if (!grade_args.v.one_side_is_whole)
                eh *= 2.0;
            }

          lane_rating = rate_lane(ew, ew_min, ew_rec, lane_rating);
          if (params.volt_support)
            lane_rating = rate_lane(eh, eh_min, eh_rec, lane_rating);

          fprintf(csv, "%d,%f,", lane->lane, ew_min);
          if (spec_ref_only)
            fprintf(csv, "%f,", ew_rec);
          else
            fprintf(csv, "NA,");
          fprintf(csv, "%f,", ew);
          if (params.volt_support)
            {
              fprintf(csv, "%f,", eh_min);
              if (spec_ref_only)
                fprintf(csv, "%f,", eh_rec);
              else
                fprintf(csv, "NA,");
              fprintf(csv, "%f,", eh);
            }
          else
            fprintf(csv, "NA,NA,NA,");
          fprintf(csv, "%s,", grades[lane_rating]);

          fprintf(csv, "%f,%f,%d,%s,", left_ps * 100.0 / margin_ui[link_speed], left_ps,
                  lane->steps[TIM_LEFT], sts_strings[lane->statuses[TIM_LEFT]]);

          if (params.ind_left_right_tim)
            fprintf(csv, "%f,%f,%d,%s,", right_ps * 100.0 / margin_ui[link_speed], right_ps,
                    lane->steps[TIM_RIGHT], sts_strings[lane->statuses[TIM_RIGHT]]);
          else
            {
              for (int k = 0; k < 4; k++)
                fprintf(csv, "NA,");
            }
          if (params.volt_support)
            {
              fprintf(csv, "%f,%d,%s,", up_volt, lane->steps[VOLT_UP],
                      sts_strings[lane->statuses[VOLT_UP]]);
              if (params.ind_up_down_volt)
                fprintf(csv, "%f,%d,%s\n", down_volt, lane->steps[VOLT_DOWN],
                        sts_strings[lane->statuses[VOLT_DOWN]]);
              else
                fprintf(csv, "NA,NA,NA\n");
            }
          else
            {
              for (int k = 0; k < 5; k++)
                fprintf(csv, "NA,");
              fprintf(csv, "NA\n");
            }
        }
      fclose(csv);
    }
  free(path);
}
