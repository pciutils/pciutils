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
  BAD = 0,
  OKAY,
  PERFECT,
  WEIRD,
  INIT,
};

static char *const grades[] = { "Bad", "Okay", "Perfect", "Weird" };
static char *const sts_strings[] = { "NAK", "LIM", "THR" };
static const double ui[] = { 62.5 / 100, 31.25 / 100 };

static enum lane_rating
rate_lane(double value, double min, double recommended, enum lane_rating cur_rate)
{
  enum lane_rating res = PERFECT;
  if (value < recommended)
    res = OKAY;
  if (value < min)
    res = BAD;
  if (cur_rate == INIT)
    return res;
  if (res < cur_rate)
    return res;
  else
    return cur_rate;
}

static bool
check_recv_weird(struct margin_results *results, double tim_min, double volt_min)
{
  bool result = true;

  struct margin_res_lane *lane;
  for (int i = 0; i < results->lanes_n && result; i++)
    {
      lane = &(results->lanes[i]);
      if (lane->steps[TIM_LEFT] * results->tim_coef != tim_min)
        result = false;
      if (results->params.ind_left_right_tim
          && lane->steps[TIM_RIGHT] * results->tim_coef != tim_min)
        result = false;
      if (results->params.volt_support)
        {
          if (lane->steps[VOLT_UP] * results->volt_coef != volt_min)
            result = false;
          if (results->params.ind_up_down_volt
              && lane->steps[VOLT_DOWN] * results->volt_coef != volt_min)
            result = false;
        }
    }
  return result;
}

void
margin_results_print_brief(struct margin_results *results, u8 recvs_n)
{
  struct margin_res_lane *lane;
  struct margin_results *res;
  struct margin_params params;

  enum lane_rating lane_rating;

  u8 link_speed;

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

      if (check_recv_weird(res, MARGIN_TIM_MIN, MARGIN_VOLT_MIN))
        lane_rating = WEIRD;
      else
        lane_rating = INIT;

      for (u8 j = 0; j < res->lanes_n; j++)
        {
          lane = &(res->lanes[j]);
          double left_ui = lane->steps[TIM_LEFT] * res->tim_coef;
          double right_ui = lane->steps[TIM_RIGHT] * res->tim_coef;
          double up_volt = lane->steps[VOLT_UP] * res->volt_coef;
          double down_volt = lane->steps[VOLT_DOWN] * res->volt_coef;

          if (lane_rating != WEIRD)
            {
              lane_rating = rate_lane(left_ui, MARGIN_TIM_MIN, MARGIN_TIM_RECOMMEND, INIT);
              if (params.ind_left_right_tim)
                lane_rating
                  = rate_lane(right_ui, MARGIN_TIM_MIN, MARGIN_TIM_RECOMMEND, lane_rating);
              if (params.volt_support)
                {
                  lane_rating = rate_lane(up_volt, MARGIN_VOLT_MIN, MARGIN_VOLT_MIN, lane_rating);
                  if (params.ind_up_down_volt)
                    lane_rating
                      = rate_lane(down_volt, MARGIN_VOLT_MIN, MARGIN_VOLT_MIN, lane_rating);
                }
            }

          printf("Rx(%X) Lane %2d - %s\t", 10 + res->recvn - 1, lane->lane, grades[lane_rating]);
          if (params.ind_left_right_tim)
            printf("L %4.1f%% UI - %5.2fps - %2dst %s, R %4.1f%% UI - %5.2fps - %2dst %s", left_ui,
                   left_ui * ui[link_speed], lane->steps[TIM_LEFT],
                   sts_strings[lane->statuses[TIM_LEFT]], right_ui, right_ui * ui[link_speed],
                   lane->steps[TIM_RIGHT], sts_strings[lane->statuses[TIM_RIGHT]]);
          else
            printf("T %4.1f%% UI - %5.2fps - %2dst %s", left_ui, left_ui * ui[link_speed],
                   lane->steps[TIM_LEFT], sts_strings[lane->statuses[TIM_LEFT]]);
          if (params.volt_support)
            {
              if (params.ind_up_down_volt)
                printf(", U %5.1f mV - %3dst %s, D %5.1f mV - %3dst %s", up_volt,
                       lane->steps[VOLT_UP], sts_strings[lane->statuses[VOLT_UP]], down_volt,
                       lane->steps[VOLT_DOWN], sts_strings[lane->statuses[VOLT_DOWN]]);
              else
                printf(", V %5.1f mV - %3dst %s", up_volt, lane->steps[VOLT_UP],
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
               port->domain_16 == 0xffff ? 8 : 4, port->domain, port->bus, port->dev,
               port->func, 10 + res->recvn - 1, timestamp);
      csv = fopen(path, "w");
      if (!csv)
        die("Error while saving %s\n", path);

      fprintf(csv, "Lane,Lane Status,Left %% UI,Left ps,Left Steps,Left Status,"
                   "Right %% UI,Right ps,Right Steps,Right Status,"
                   "Time %% UI,Time ps,Time Steps,Time Status,"
                   "Up mV,Up Steps,Up Status,Down mV,Down Steps,Down Status,"
                   "Voltage mV,Voltage Steps,Voltage Status\n");

      if (check_recv_weird(res, MARGIN_TIM_MIN, MARGIN_VOLT_MIN))
        lane_rating = WEIRD;
      else
        lane_rating = INIT;

      for (int j = 0; j < res->lanes_n; j++)
        {
          lane = &(res->lanes[j]);
          double left_ui = lane->steps[TIM_LEFT] * res->tim_coef;
          double right_ui = lane->steps[TIM_RIGHT] * res->tim_coef;
          double up_volt = lane->steps[VOLT_UP] * res->volt_coef;
          double down_volt = lane->steps[VOLT_DOWN] * res->volt_coef;

          if (lane_rating != WEIRD)
            {
              lane_rating = rate_lane(left_ui, MARGIN_TIM_MIN, MARGIN_TIM_RECOMMEND, INIT);
              if (params.ind_left_right_tim)
                lane_rating
                  = rate_lane(right_ui, MARGIN_TIM_MIN, MARGIN_TIM_RECOMMEND, lane_rating);
              if (params.volt_support)
                {
                  lane_rating = rate_lane(up_volt, MARGIN_VOLT_MIN, MARGIN_VOLT_MIN, lane_rating);
                  if (params.ind_up_down_volt)
                    lane_rating
                      = rate_lane(down_volt, MARGIN_VOLT_MIN, MARGIN_VOLT_MIN, lane_rating);
                }
            }

          fprintf(csv, "%d,%s,", lane->lane, grades[lane_rating]);
          if (params.ind_left_right_tim)
            {
              fprintf(csv, "%f,%f,%d,%s,%f,%f,%d,%s,NA,NA,NA,NA,", left_ui,
                      left_ui * ui[link_speed], lane->steps[TIM_LEFT],
                      sts_strings[lane->statuses[TIM_LEFT]], right_ui, right_ui * ui[link_speed],
                      lane->steps[TIM_RIGHT], sts_strings[lane->statuses[TIM_RIGHT]]);
            }
          else
            {
              for (int k = 0; k < 8; k++)
                fprintf(csv, "NA,");
              fprintf(csv, "%f,%f,%d,%s,", left_ui, left_ui * ui[link_speed], lane->steps[TIM_LEFT],
                      sts_strings[lane->statuses[TIM_LEFT]]);
            }
          if (params.volt_support)
            {
              if (params.ind_up_down_volt)
                {
                  fprintf(csv, "%f,%d,%s,%f,%d,%s,NA,NA,NA\n", up_volt, lane->steps[VOLT_UP],
                          sts_strings[lane->statuses[VOLT_UP]], down_volt, lane->steps[VOLT_DOWN],
                          sts_strings[lane->statuses[VOLT_DOWN]]);
                }
              else
                {
                  for (int k = 0; k < 6; k++)
                    fprintf(csv, "NA,");
                  fprintf(csv, "%f,%d,%s\n", up_volt, lane->steps[VOLT_UP],
                          sts_strings[lane->statuses[VOLT_UP]]);
                }
            }
          else
            {
              for (int k = 0; k < 8; k++)
                fprintf(csv, "NA,");
              fprintf(csv, "NA\n");
            }
        }
      fclose(csv);
    }
  free(path);
}
