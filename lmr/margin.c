/*
 *	The PCI Utilities -- Obtain the margin information of the Link
 *
 *	Copyright (c) 2023-2024 KNS Group LLC (YADRO)
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "lmr.h"

#ifdef PCI_OS_DJGPP
#include <unistd.h>
#endif

/* Macro helpers for Margining command parsing */

typedef u16 margin_cmd;

/* Margining command parsing */

#define LMR_CMD_RECVN   MASK(2, 0)
#define LMR_CMD_TYPE    MASK(5, 3)
#define LMR_CMD_PAYLOAD MASK(15, 8)

// Payload parsing

// Report Capabilities
#define LMR_PLD_VOLT_SUPPORT         BIT(8)
#define LMR_PLD_IND_U_D_VOLT         BIT(9)
#define LMR_PLD_IND_L_R_TIM          BIT(10)
#define LMR_PLD_SAMPLE_REPORT_METHOD BIT(11)
#define LMR_PLD_IND_ERR_SAMPLER      BIT(12)

#define LMR_PLD_MAX_T_STEPS MASK(13, 8)
#define LMR_PLD_MAX_V_STEPS MASK(14, 8)
#define LMR_PLD_MAX_OFFSET  MASK(14, 8)
#define LMR_PLD_MAX_LANES   MASK(12, 8)
#define LMR_PLD_SAMPLE_RATE MASK(13, 8)

// Timing Step
#define LMR_PLD_MARGIN_T_STEPS MASK(13, 8)
#define LMR_PLD_T_GO_LEFT      BIT(14)

// Voltage Timing
#define LMR_PLD_MARGIN_V_STEPS MASK(14, 8)
#define LMR_PLD_V_GO_DOWN      BIT(15)

// Step Response
#define LMR_PLD_ERR_CNT    MASK(13, 8)
#define LMR_PLD_MARGIN_STS MASK(15, 14)

/* Address calc macro for Lanes Margining registers */

#define LMR_LANE_CTRL(lmr_cap_addr, lane)   ((lmr_cap_addr) + 8 + 4 * (lane))
#define LMR_LANE_STATUS(lmr_cap_addr, lane) ((lmr_cap_addr) + 10 + 4 * (lane))

/* Margining Commands */

#define MARG_TIM(go_left, step, recvn)  margin_make_cmd(((go_left) << 6) | (step), 3, recvn)
#define MARG_VOLT(go_down, step, recvn) margin_make_cmd(((go_down) << 7) | (step), 4, recvn)

// Report commands
#define REPORT_CAPS(recvn)         margin_make_cmd(0x88, 1, recvn)
#define REPORT_VOL_STEPS(recvn)    margin_make_cmd(0x89, 1, recvn)
#define REPORT_TIM_STEPS(recvn)    margin_make_cmd(0x8A, 1, recvn)
#define REPORT_TIM_OFFSET(recvn)   margin_make_cmd(0x8B, 1, recvn)
#define REPORT_VOL_OFFSET(recvn)   margin_make_cmd(0x8C, 1, recvn)
#define REPORT_SAMPL_RATE_V(recvn) margin_make_cmd(0x8D, 1, recvn)
#define REPORT_SAMPL_RATE_T(recvn) margin_make_cmd(0x8E, 1, recvn)
#define REPORT_SAMPLE_CNT(recvn)   margin_make_cmd(0x8F, 1, recvn)
#define REPORT_MAX_LANES(recvn)    margin_make_cmd(0x90, 1, recvn)

// Set commands
#define NO_COMMAND                          margin_make_cmd(0x9C, 7, 0)
#define CLEAR_ERROR_LOG(recvn)              margin_make_cmd(0x55, 2, recvn)
#define GO_TO_NORMAL_SETTINGS(recvn)        margin_make_cmd(0xF, 2, recvn)
#define SET_ERROR_LIMIT(error_limit, recvn) margin_make_cmd(0xC0 | (error_limit), 2, recvn)

static int
msleep(long msec)
{
#if defined(PCI_OS_WINDOWS)
  Sleep(msec);
  return 0;
#elif defined(PCI_OS_DJGPP)
  if (msec * 1000 < 11264)
    usleep(11264);
  else
    usleep(msec * 1000);
  return 0;
#else
  struct timespec ts;
  int res;

  if (msec < 0)
    {
      errno = EINVAL;
      return -1;
    }

  ts.tv_sec = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;

  do
    {
      res = nanosleep(&ts, &ts);
  } while (res && errno == EINTR);

  return res;
#endif
}

static margin_cmd
margin_make_cmd(u8 payload, u8 type, u8 recvn)
{
  return SET_REG_MASK(0, LMR_CMD_PAYLOAD, payload) | SET_REG_MASK(0, LMR_CMD_TYPE, type)
         | SET_REG_MASK(0, LMR_CMD_RECVN, recvn);
}

static bool
margin_set_cmd(struct margin_dev *dev, u8 lane, margin_cmd cmd)
{
  pci_write_word(dev->dev, LMR_LANE_CTRL(dev->lmr_cap_addr, lane), cmd);
  msleep(10);
  return pci_read_word(dev->dev, LMR_LANE_STATUS(dev->lmr_cap_addr, lane)) == cmd;
}

static bool
margin_report_cmd(struct margin_dev *dev, u8 lane, margin_cmd cmd, margin_cmd *result)
{
  pci_write_word(dev->dev, LMR_LANE_CTRL(dev->lmr_cap_addr, lane), cmd);
  msleep(10);
  *result = pci_read_word(dev->dev, LMR_LANE_STATUS(dev->lmr_cap_addr, lane));
  return GET_REG_MASK(*result, LMR_CMD_TYPE) == GET_REG_MASK(cmd, LMR_CMD_TYPE)
         && GET_REG_MASK(*result, LMR_CMD_RECVN) == GET_REG_MASK(cmd, LMR_CMD_RECVN)
         && margin_set_cmd(dev, lane, NO_COMMAND);
}

static void
margin_apply_hw_quirks(struct margin_recv *recv, struct margin_link_args *args)
{
  switch (recv->dev->hw)
    {
      case MARGIN_ICE_LAKE_RC:
        if (recv->recvn == 1)
          {
            recv->params->volt_offset = 12;
            args->recv_args[recv->recvn - 1].t.one_side_is_whole = true;
            args->recv_args[recv->recvn - 1].t.valid = true;
          }
        break;
      default:
        break;
    }
}

static bool
read_params_internal(struct margin_dev *dev, u8 recvn, bool lane_reversal,
                     struct margin_params *params)
{
  margin_cmd resp;
  u8 lane = lane_reversal ? dev->width - 1 : 0;
  margin_set_cmd(dev, lane, NO_COMMAND);
  bool status = margin_report_cmd(dev, lane, REPORT_CAPS(recvn), &resp);
  if (status)
    {
      params->volt_support = GET_REG_MASK(resp, LMR_PLD_VOLT_SUPPORT);
      params->ind_up_down_volt = GET_REG_MASK(resp, LMR_PLD_IND_U_D_VOLT);
      params->ind_left_right_tim = GET_REG_MASK(resp, LMR_PLD_IND_L_R_TIM);
      params->sample_report_method = GET_REG_MASK(resp, LMR_PLD_SAMPLE_REPORT_METHOD);
      params->ind_error_sampler = GET_REG_MASK(resp, LMR_PLD_IND_ERR_SAMPLER);
      status = margin_report_cmd(dev, lane, REPORT_VOL_STEPS(recvn), &resp);
    }
  if (status)
    {
      params->volt_steps = GET_REG_MASK(resp, LMR_PLD_MAX_V_STEPS);
      status = margin_report_cmd(dev, lane, REPORT_TIM_STEPS(recvn), &resp);
    }
  if (status)
    {
      params->timing_steps = GET_REG_MASK(resp, LMR_PLD_MAX_T_STEPS);
      status = margin_report_cmd(dev, lane, REPORT_TIM_OFFSET(recvn), &resp);
    }
  if (status)
    {
      params->timing_offset = GET_REG_MASK(resp, LMR_PLD_MAX_OFFSET);
      status = margin_report_cmd(dev, lane, REPORT_VOL_OFFSET(recvn), &resp);
    }
  if (status)
    {
      params->volt_offset = GET_REG_MASK(resp, LMR_PLD_MAX_OFFSET);
      status = margin_report_cmd(dev, lane, REPORT_SAMPL_RATE_V(recvn), &resp);
    }
  if (status)
    {
      params->sample_rate_v = GET_REG_MASK(resp, LMR_PLD_SAMPLE_RATE);
      status = margin_report_cmd(dev, lane, REPORT_SAMPL_RATE_T(recvn), &resp);
    }
  if (status)
    {
      params->sample_rate_t = GET_REG_MASK(resp, LMR_PLD_SAMPLE_RATE);
      status = margin_report_cmd(dev, lane, REPORT_MAX_LANES(recvn), &resp);
    }
  if (status)
    params->max_lanes = GET_REG_MASK(resp, LMR_PLD_MAX_LANES);
  return status;
}

/* Margin all lanes_n lanes simultaneously */
static void
margin_test_lanes(struct margin_lanes_data arg)
{
  u8 steps_done = 0;
  margin_cmd lane_status;
  u8 marg_type;
  margin_cmd step_cmd;
  bool timing = (arg.dir == TIM_LEFT || arg.dir == TIM_RIGHT);

  if (timing)
    {
      marg_type = 3;
      step_cmd = MARG_TIM(arg.dir == TIM_LEFT, steps_done, arg.recv->recvn);
    }
  else
    {
      marg_type = 4;
      step_cmd = MARG_VOLT(arg.dir == VOLT_DOWN, steps_done, arg.recv->recvn);
    }

  bool failed_lanes[32] = { 0 };
  u8 alive_lanes = arg.lanes_n;

  for (int i = 0; i < arg.lanes_n; i++)
    {
      margin_set_cmd(arg.recv->dev, arg.results[i].lane, NO_COMMAND);
      margin_set_cmd(arg.recv->dev, arg.results[i].lane,
                     SET_ERROR_LIMIT(arg.recv->error_limit, arg.recv->recvn));
      margin_set_cmd(arg.recv->dev, arg.results[i].lane, NO_COMMAND);
      arg.results[i].steps[arg.dir] = arg.steps_lane_total;
      arg.results[i].statuses[arg.dir] = MARGIN_THR;
    }

  while (alive_lanes > 0 && steps_done < arg.steps_lane_total)
    {
      alive_lanes = 0;
      steps_done++;
      if (timing)
        step_cmd = SET_REG_MASK(step_cmd, LMR_PLD_MARGIN_T_STEPS, steps_done);
      else
        step_cmd = SET_REG_MASK(step_cmd, LMR_PLD_MARGIN_V_STEPS, steps_done);

      for (int i = 0; i < arg.lanes_n; i++)
        {
          if (!failed_lanes[i])
            {
              alive_lanes++;
              int ctrl_addr = LMR_LANE_CTRL(arg.recv->dev->lmr_cap_addr, arg.results[i].lane);
              pci_write_word(arg.recv->dev->dev, ctrl_addr, step_cmd);
            }
        }
      msleep(arg.recv->dwell_time * 1000);

      for (int i = 0; i < arg.lanes_n; i++)
        {
          if (!failed_lanes[i])
            {
              int status_addr = LMR_LANE_STATUS(arg.recv->dev->lmr_cap_addr, arg.results[i].lane);
              lane_status = pci_read_word(arg.recv->dev->dev, status_addr);
              u8 step_status = GET_REG_MASK(lane_status, LMR_PLD_MARGIN_STS);
              if (!(GET_REG_MASK(lane_status, LMR_CMD_TYPE) == marg_type
                    && GET_REG_MASK(lane_status, LMR_CMD_RECVN) == arg.recv->recvn
                    && step_status == 2
                    && GET_REG_MASK(lane_status, LMR_PLD_ERR_CNT) <= arg.recv->error_limit
                    && margin_set_cmd(arg.recv->dev, arg.results[i].lane, NO_COMMAND)))
                {
                  alive_lanes--;
                  failed_lanes[i] = true;
                  arg.results[i].steps[arg.dir] = steps_done - 1;
                  arg.results[i].statuses[arg.dir]
                    = (step_status == 3 || step_status == 1 ? MARGIN_NAK : MARGIN_LIM);
                }
            }
        }

      arg.steps_lane_done = steps_done;
      margin_log_margining(arg);
    }

  for (int i = 0; i < arg.lanes_n; i++)
    {
      margin_set_cmd(arg.recv->dev, arg.results[i].lane, NO_COMMAND);
      margin_set_cmd(arg.recv->dev, arg.results[i].lane, CLEAR_ERROR_LOG(arg.recv->recvn));
      margin_set_cmd(arg.recv->dev, arg.results[i].lane, NO_COMMAND);
      margin_set_cmd(arg.recv->dev, arg.results[i].lane, GO_TO_NORMAL_SETTINGS(arg.recv->recvn));
      margin_set_cmd(arg.recv->dev, arg.results[i].lane, NO_COMMAND);
    }
}

/* Awaits that Receiver is prepared through prep_dev function */
static bool
margin_test_receiver(struct margin_dev *dev, u8 recvn, struct margin_link_args *args,
                     struct margin_results *results)
{
  u8 *lanes_to_margin = args->lanes;
  u8 lanes_n = args->lanes_n;

  struct margin_params params;
  struct margin_recv recv = { .dev = dev,
                              .recvn = recvn,
                              .lane_reversal = false,
                              .params = &params,
                              .parallel_lanes = args->parallel_lanes ? args->parallel_lanes : 1,
                              .error_limit = args->common->error_limit,
                              .dwell_time = args->common->dwell_time };

  results->recvn = recvn;
  results->lanes_n = lanes_n;
  margin_log_recvn(&recv);

  if (!margin_check_ready_bit(dev->dev))
    {
      margin_log("\nMargining Ready bit is Clear.\n");
      results->test_status = MARGIN_TEST_READY_BIT;
      return false;
    }

  if (!read_params_internal(dev, recvn, recv.lane_reversal, &params))
    {
      recv.lane_reversal = true;
      if (!read_params_internal(dev, recvn, recv.lane_reversal, &params))
        {
          margin_log("\nError during caps reading.\n");
          results->test_status = MARGIN_TEST_CAPS;
          return false;
        }
    }

  results->params = params;

  if (recv.parallel_lanes > params.max_lanes + 1)
    recv.parallel_lanes = params.max_lanes + 1;
  margin_apply_hw_quirks(&recv, args);
  margin_log_hw_quirks(&recv);

  results->tim_off_reported = params.timing_offset != 0;
  results->volt_off_reported = params.volt_offset != 0;
  double tim_offset = results->tim_off_reported ? (double)params.timing_offset : 50.0;
  double volt_offset = results->volt_off_reported ? (double)params.volt_offset : 50.0;

  results->tim_coef = tim_offset / (double)params.timing_steps;
  results->volt_coef = volt_offset / (double)params.volt_steps * 10.0;

  results->lane_reversal = recv.lane_reversal;
  results->link_speed = dev->link_speed;
  results->test_status = MARGIN_TEST_OK;

  margin_log_receiver(&recv);

  results->lanes = xmalloc(sizeof(struct margin_res_lane) * lanes_n);
  for (int i = 0; i < lanes_n; i++)
    {
      results->lanes[i].lane
        = recv.lane_reversal ? dev->width - lanes_to_margin[i] - 1 : lanes_to_margin[i];
    }

  if (args->common->run_margin)
    {
      if (args->common->verbosity > 0)
        margin_log("\n");
      struct margin_lanes_data lanes_data = { .recv = &recv,
                                              .verbosity = args->common->verbosity,
                                              .steps_utility = &args->common->steps_utility };

      enum margin_dir dir[] = { TIM_LEFT, TIM_RIGHT, VOLT_UP, VOLT_DOWN };

      u8 lanes_done = 0;
      u8 use_lanes = 0;
      u8 steps_t = args->steps_t ? args->steps_t : params.timing_steps;
      u8 steps_v = args->steps_v ? args->steps_v : params.volt_steps;

      while (lanes_done != lanes_n)
        {
          use_lanes = (lanes_done + recv.parallel_lanes > lanes_n) ? lanes_n - lanes_done :
                                                                     recv.parallel_lanes;
          lanes_data.lanes_numbers = lanes_to_margin + lanes_done;
          lanes_data.lanes_n = use_lanes;
          lanes_data.results = results->lanes + lanes_done;

          for (int i = 0; i < 4; i++)
            {
              bool timing = dir[i] == TIM_LEFT || dir[i] == TIM_RIGHT;
              if (!timing && !params.volt_support)
                continue;
              if (dir[i] == TIM_RIGHT && !params.ind_left_right_tim)
                continue;
              if (dir[i] == VOLT_DOWN && !params.ind_up_down_volt)
                continue;

              lanes_data.ind = timing ? params.ind_left_right_tim : params.ind_up_down_volt;
              lanes_data.dir = dir[i];
              lanes_data.steps_lane_total = timing ? steps_t : steps_v;
              if (args->common->steps_utility >= lanes_data.steps_lane_total)
                args->common->steps_utility -= lanes_data.steps_lane_total;
              else
                args->common->steps_utility = 0;
              margin_test_lanes(lanes_data);
            }
          lanes_done += use_lanes;
        }
      if (args->common->verbosity > 0)
        margin_log("\n");
      if (recv.lane_reversal)
        {
          for (int i = 0; i < lanes_n; i++)
            results->lanes[i].lane = lanes_to_margin[i];
        }
    }

  return true;
}

bool
margin_read_params(struct pci_access *pacc, struct pci_dev *dev, u8 recvn,
                   struct margin_params *params)
{
  struct pci_cap *cap = pci_find_cap(dev, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
  if (!cap)
    return false;

  bool dev_down = margin_port_is_down(dev);

  if (recvn == 0)
    {
      if (dev_down)
        recvn = 1;
      else
        recvn = 6;
    }

  if (recvn > 6)
    return false;
  if (dev_down && recvn == 6)
    return false;
  if (!dev_down && recvn != 6)
    return false;

  struct pci_dev *down = NULL;
  struct pci_dev *up = NULL;
  struct margin_link link;

  if (!margin_find_pair(pacc, dev, &down, &up))
    return false;

  if (!margin_fill_link(down, up, &link))
    return false;

  struct margin_dev *dut = (dev_down ? &link.down_port : &link.up_port);
  if (!margin_check_ready_bit(dut->dev))
    return false;

  if (!margin_prep_link(&link))
    return false;

  bool status;
  bool lane_reversal = false;
  status = read_params_internal(dut, recvn, lane_reversal, params);
  if (!status)
    {
      lane_reversal = true;
      status = read_params_internal(dut, recvn, lane_reversal, params);
    }

  margin_restore_link(&link);

  return status;
}

enum margin_test_status
margin_process_args(struct margin_link *link)
{
  struct margin_dev *dev = &link->down_port;
  struct margin_link_args *args = &link->args;

  u8 receivers_n = 2 + 2 * dev->retimers_n;

  if (!args->recvs_n)
    {
      for (int i = 1; i < receivers_n; i++)
        args->recvs[i - 1] = i;
      args->recvs[receivers_n - 1] = 6;
      args->recvs_n = receivers_n;
    }
  else
    {
      for (int i = 0; i < args->recvs_n; i++)
        {
          u8 recvn = args->recvs[i];
          if (recvn < 1 || recvn > 6 || (recvn != 6 && recvn > receivers_n - 1))
            {
              return MARGIN_TEST_ARGS_RECVS;
            }
        }
    }

  if (!args->lanes_n)
    {
      args->lanes_n = dev->width;
      for (int i = 0; i < args->lanes_n; i++)
        args->lanes[i] = i;
    }
  else
    {
      for (int i = 0; i < args->lanes_n; i++)
        {
          if (args->lanes[i] >= dev->width)
            {
              return MARGIN_TEST_ARGS_LANES;
            }
        }
    }

  return MARGIN_TEST_OK;
}

struct margin_results *
margin_test_link(struct margin_link *link, u8 *recvs_n)
{
  struct margin_link_args *args = &link->args;

  bool status = margin_prep_link(link);

  u8 receivers_n = status ? args->recvs_n : 1;
  u8 *receivers = args->recvs;

  margin_log_link(link);

  struct margin_results *results = xmalloc(sizeof(*results) * receivers_n);

  if (!status)
    {
      results[0].test_status = MARGIN_TEST_ASPM;
      margin_log("\nCouldn't disable ASPM on the given Link.\n");
    }

  if (status)
    {
      struct margin_dev *dut;
      for (int i = 0; i < receivers_n; i++)
        {
          dut = receivers[i] == 6 ? &link->up_port : &link->down_port;
          margin_test_receiver(dut, receivers[i], args, &results[i]);
        }

      margin_restore_link(link);
    }

  *recvs_n = receivers_n;
  return results;
}

void
margin_free_results(struct margin_results *results, u8 results_n)
{
  for (int i = 0; i < results_n; i++)
    {
      if (results[i].test_status == MARGIN_TEST_OK)
        free(results[i].lanes);
    }
  free(results);
}
