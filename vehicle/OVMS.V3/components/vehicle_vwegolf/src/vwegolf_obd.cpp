/**
 * Project:      Open Vehicle Monitor System
 * Module:       VW e-Golf via OBD Port
 *
 * (c) 2020  Soko
 * (c) 2021  Michael Balzer <dexter@dexters-web.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "ovms_log.h"
static const char *TAG = "v-vwegolf";

#include <stdio.h>
#include <string>
#include <iomanip>
#include <algorithm>
#include <cmath>

#include "pcp.h"
#include "ovms_metrics.h"
#include "ovms_events.h"
#include "ovms_config.h"
#include "ovms_command.h"
#include "metrics_standard.h"
#include "ovms_notify.h"
#include "ovms_utils.h"

#include "vehicle_vwegolf.h"
#include "vwegolf_obd.h"

 
//
// General PIDs for all model years
//
const OvmsVehicle::poll_pid_t vwegolf_polls[] = {
  // Note: poller ticker cycles at 3600 seconds = max period
  // { ecu, type, pid, {_OFF,_AWAKE,_CHARGING,_ON}, bus, protocol }

  {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_CCS_STATUS,       {  0,  0,  3,  0}, 1, ISOTP_STD},

  {VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_U,                {  0, 10,  3,  1}, 1, ISOTP_STD},
  {VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_I,                {  0, 10,  3,  1}, 1, ISOTP_STD},
  // Same tick & order important of above 2: VWGOLF_BAT_MGMT_I calculates the power
  // Also, the charging interval should match the charge input polls (CCS & AC), and they
  // should be polled close to each other to get a correct charge loss/efficiency calculation.

  {VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_SOC_NORM,         {  0,  0,  0, 20}, 1, ISOTP_STD},
  {VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_SOC_ABS,          {  0,  0,  0, 20}, 1, ISOTP_STD},
  {VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_SOC_ABS,          {  0, 20, 20, 20}, 1, ISOTP_STD},
  {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_SOC_NORM,         {  0, 20, 20,  0}, 1, ISOTP_STD},
  {VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_ENERGY_COUNTERS,  {  0, 20, 20, 20}, 1, ISOTP_STD},
  // Energy counters need to be polled directly after the SOCs and at the same interval

  {VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_CELL_MAX,         {  0, 20, 20, 20}, 1, ISOTP_STD},
  {VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_CELL_MIN,         {  0, 20, 20, 20}, 1, ISOTP_STD},
  // Same tick & order important of above 2: VWGOLF_BAT_MGMT_CELL_MIN calculates the delta

  {VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_TEMP,             {  0, 20, 20, 20}, 1, ISOTP_STD},

  {VWGOLF_CHG,      UDS_READ, VWGOLF_CHG_POWER_EFF,             {  0,  0, 10,  0}, 1, ISOTP_STD},
  {VWGOLF_CHG,      UDS_READ, VWGOLF_CHG_POWER_LOSS,            {  0,  0, 10,  0}, 1, ISOTP_STD},

  {VWGOLF_MFD,      UDS_READ, VWGOLF_MFD_ODOMETER,              {  0,  0,  0, 60}, 1, ISOTP_STD},
  {VWGOLF_MFD,      UDS_READ, VWGOLF_MFD_RANGE_CAP,             {  0,  0,  0, 60}, 1, ISOTP_STD},

  {VWGOLF_MFD,      UDS_READ, VWGOLF_MFD_SERV_RANGE,            {  0,  0,  0, 60}, 1, ISOTP_STD},
  {VWGOLF_MFD,      UDS_READ, VWGOLF_MFD_SERV_TIME,             {  0,  0,  0, 60}, 1, ISOTP_STD},

  {VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_TEMP_DCDC,        {  0,  0,  0, 20}, 1, ISOTP_STD},
  {VWGOLF_ELD,      UDS_READ, VWGOLF_ELD_DCDC_U,                {  0, 10, 10,  5}, 1, ISOTP_STD},
  {VWGOLF_ELD,      UDS_READ, VWGOLF_ELD_DCDC_I,                {  0, 10, 10,  5}, 1, ISOTP_STD},
  {VWGOLF_ELD,      UDS_READ, VWGOLF_ELD_TEMP_MOT,              {  0, 60, 60, 20}, 1, ISOTP_STD},
  {VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_TEMP_PEM,         {  0,  0,  0, 20}, 1, ISOTP_STD},
  {VWGOLF_CHG,      UDS_READ, VWGOLF_CHG_TEMP_COOLER,           {  0, 20, 20, 20}, 1, ISOTP_STD},
//{VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_TEMP_MAX,         {  0,  0,  0, 20}, 1, ISOTP_STD},
//{VWGOLF_BAT_MGMT, UDS_READ, VWGOLF_BAT_MGMT_TEMP_MIN,         {  0,  0,  0, 20}, 1, ISOTP_STD},

  {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_SOC_LIMITS,       {  0, 30, 30, 30}, 1, ISOTP_STD},
  {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_TIMER_DEF,        {  0, 30, 30, 30}, 1, ISOTP_STD},
  {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_REM,              {  0,  0, 30,  0}, 1, ISOTP_STD},

  {VWGOLF_BRK,      UDS_SESSION, VWGOLF_EXTDIAG_START,          {  0,  0,  0, 30}, 1, ISOTP_STD},
  {VWGOLF_BRK,      UDS_READ, VWGOLF_BRK_TPMS,                  {  0,  0,  0, 30}, 1, ISOTP_STD},
};

//
// Specific PIDs for gen1 model (before year 2017)
//
const OvmsVehicle::poll_pid_t vwegolf_gen1_polls[] = {
  {VWGOLF_CHG,      UDS_READ, VWGOLF1_CHG_AC_U,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  {VWGOLF_CHG,      UDS_READ, VWGOLF1_CHG_AC_I,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  // Same tick & order important of above 2: VWGOLF_CHG_AC_I calculates the AC power
  {VWGOLF_CHG,      UDS_READ, VWGOLF1_CHG_DC_U,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  {VWGOLF_CHG,      UDS_READ, VWGOLF1_CHG_DC_I,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  // Same tick & order important of above 2: VWGOLF_CHG_DC_I calculates the DC power
  // Same tick & order important of above 4: VWGOLF_CHG_DC_I calculates the power loss & efficiency
};

//
// Specific PIDs for gen2 model (from year 2017)
//
const OvmsVehicle::poll_pid_t vwegolf_gen2_polls[] = {
  {VWGOLF_CHG,      UDS_READ, VWGOLF2_CHG_AC_U,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  {VWGOLF_CHG,      UDS_READ, VWGOLF2_CHG_AC_I,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  // Same tick & order important of above 2: VWGOLF_CHG_AC_I calculates the AC power
  {VWGOLF_CHG,      UDS_READ, VWGOLF2_CHG_DC_U,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  {VWGOLF_CHG,      UDS_READ, VWGOLF2_CHG_DC_I,                 {  0,  0,  3,  0}, 1, ISOTP_STD},
  // Same tick & order important of above 2: VWGOLF_CHG_DC_I calculates the DC power
  // Same tick & order important of above 4: VWGOLF_CHG_DC_I calculates the power loss & efficiency
};


void OvmsVehicleVWeGolf::OBDInit()
{
  ESP_LOGI(TAG, "Starting connection: OBDII");

  //
  // First time initialization
  //

  if (m_obd_state == OBDS_Init)
  {
    // Init metrics:
    m_lv_pwrstate = MyMetrics.InitInt("xvg.e.lv.pwrstate", 30, 0, Other, true);
    m_hv_chgmode  = MyMetrics.InitInt("xvg.e.hv.chgmode", 30, 0, Other, true);
    m_lv_autochg  = MyMetrics.InitInt("xvg.e.lv.autochg", 30, 0);

    bool timermode = StdMetrics.ms_v_charge_timermode->AsBool();
    int soclim = StdMetrics.ms_v_charge_limit_soc->AsInt();
    m_chg_timer_socmin = MyMetrics.InitInt("xvg.c.limit.soc.min", SM_STALE_NONE, soclim, Percentage);
    m_chg_timer_socmax = MyMetrics.InitInt("xvg.c.limit.soc.max", SM_STALE_NONE, soclim, Percentage);
    m_chg_timer_def = MyMetrics.InitBool("xvg.c.timermode.def", SM_STALE_NONE, timermode);

    BatMgmtSoCAbs = MyMetrics.InitFloat("xvg.b.soc.abs", 100, 0, Percentage);
    MotElecSoCAbs = MyMetrics.InitFloat("xvg.m.soc.abs", 100, 0, Percentage);
    MotElecSoCNorm = MyMetrics.InitFloat("xvg.m.soc.norm", 100, 0, Percentage);
    ChgMgmtSoCNorm = MyMetrics.InitFloat("xvg.c.soc.norm", 100, 0, Percentage);
    BatMgmtCellDelta = MyMetrics.InitFloat("xvg.b.cell.delta", SM_STALE_NONE, 0, Volts);

    ChargerPowerEffEcu = MyMetrics.InitFloat("xvg.c.eff.ecu", 100, 0, Percentage);
    ChargerPowerLossEcu = MyMetrics.InitFloat("xvg.c.loss.ecu", SM_STALE_NONE, 0, kW);
    ChargerPowerEffCalc = MyMetrics.InitFloat("xvg.c.eff.calc", 100, 0, Percentage);
    ChargerPowerLossCalc = MyMetrics.InitFloat("xvg.c.loss.calc", SM_STALE_NONE, 0, kW);
    ChargerACPower = MyMetrics.InitFloat("xvg.c.ac.p", SM_STALE_NONE, 0, kW);
    ChargerAC1U = MyMetrics.InitFloat("xvg.c.ac.u1", SM_STALE_NONE, 0, Volts);
    ChargerAC2U = MyMetrics.InitFloat("xvg.c.ac.u2", SM_STALE_NONE, 0, Volts);
    ChargerAC1I = MyMetrics.InitFloat("xvg.c.ac.i1", SM_STALE_NONE, 0, Amps);
    ChargerAC2I = MyMetrics.InitFloat("xvg.c.ac.i2", SM_STALE_NONE, 0, Amps);
    ChargerDC1U = MyMetrics.InitFloat("xvg.c.dc.u1", SM_STALE_NONE, 0, Volts);
    ChargerDC2U = MyMetrics.InitFloat("xvg.c.dc.u2", SM_STALE_NONE, 0, Volts);
    ChargerDC1I = MyMetrics.InitFloat("xvg.c.dc.i1", SM_STALE_NONE, 0, Amps);
    ChargerDC2I = MyMetrics.InitFloat("xvg.c.dc.i2", SM_STALE_NONE, 0, Amps);
    ChargerDCPower = MyMetrics.InitFloat("xvg.c.dc.p", SM_STALE_NONE, 0, kW);

    m_chg_ccs_voltage = MyMetrics.InitFloat("xvg.c.ccs.u", SM_STALE_MIN, 0, Volts);
    m_chg_ccs_current = MyMetrics.InitFloat("xvg.c.ccs.i", SM_STALE_MIN, 0, Amps);
    m_chg_ccs_power   = MyMetrics.InitFloat("xvg.c.ccs.p", SM_STALE_MIN, 0, kW);

    ServiceDays =  MyMetrics.InitInt("xvg.e.serv.days", SM_STALE_NONE, 0);
    TPMSDiffusion = MyMetrics.InitVector<float>("xvg.v.t.diff", SM_STALE_NONE, 0);
    TPMSEmergency = MyMetrics.InitVector<float>("xvg.v.t.emgcy", SM_STALE_NONE, 0);

    // Battery energy according to MFD range estimation:
    m_bat_energy_range  = MyMetrics.InitFloat("xvg.b.energy.range", SM_STALE_MAX, 0, kWh);
    m_bat_cap_kwh_range = MyMetrics.InitFloat("xvg.b.cap.kwh.range", SM_STALE_MAX, 0, kWh);

    // Battery capacity calculations from charge SOC & coulomb/energy delta:
    m_bat_cap_ah_abs    = MyMetrics.InitFloat("xvg.b.cap.ah.abs", SM_STALE_MAX, 0, AmpHours, true);
    m_bat_cap_ah_norm   = MyMetrics.InitFloat("xvg.b.cap.ah.norm", SM_STALE_MAX, 0, AmpHours, true);
    m_bat_cap_kwh_abs   = MyMetrics.InitFloat("xvg.b.cap.kwh.abs", SM_STALE_MAX, 0, kWh, true);
    m_bat_cap_kwh_norm  = MyMetrics.InitFloat("xvg.b.cap.kwh.norm", SM_STALE_MAX, 0, kWh, true);

    // Init smoothing:
    m_smooth_cap_ah_abs  .Init(15, m_bat_cap_ah_abs->AsFloat(),   m_bat_cap_ah_abs->AsFloat() != 0);
    m_smooth_cap_ah_norm .Init(15, m_bat_cap_ah_norm->AsFloat(),  m_bat_cap_ah_norm->AsFloat() != 0);
    m_smooth_cap_kwh_abs .Init(15, m_bat_cap_kwh_abs->AsFloat(),  m_bat_cap_kwh_abs->AsFloat() != 0);
    m_smooth_cap_kwh_norm.Init(15, m_bat_cap_kwh_norm->AsFloat(), m_bat_cap_kwh_norm->AsFloat() != 0);

    // Start can1:
    RegisterCanBus(1, CAN_MODE_ACTIVE, CAN_SPEED_500KBPS);
  }

  //
  // Init/reconfigure poller
  //

  m_obd_state = OBDS_Config;

  PollSetPidList(m_can1, NULL);
  PollSetThrottling(0);
  PollSetResponseSeparationTime(1);

  if (StandardMetrics.ms_v_charge_inprogress->AsBool())
    PollSetState(VWEGOLF_CHARGING);
  else if (StandardMetrics.ms_v_env_on->AsBool())
    PollSetState(VWEGOLF_ON);
  else
    PollSetState(VWEGOLF_OFF);

  m_poll_vector.clear();

  // Add vehicle state detection PIDs:
  for (auto p : poll_list_t {
      {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_LV_PWRSTATE, {  1,  1,  1,  1}, 1, ISOTP_STD},
      {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_HV_CHGMODE,  {  0,  1,  1,  1}, 1, ISOTP_STD},
      {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_LV_AUTOCHG,  {  0,  1,  1,  1}, 1, ISOTP_STD},
    }) {
    if (HasT26()) {
      // We can get the car state from T26, adjust poll intervals:
      p.polltime[VWEGOLF_OFF]       = 0;
      p.polltime[VWEGOLF_AWAKE]     = 5;
      p.polltime[VWEGOLF_CHARGING]  = 5;
      p.polltime[VWEGOLF_ON]        = 5;
    }
    m_poll_vector.push_back(p);
  }

  // Add high priority PIDs only necessary without T26:
  if (HasNoT26()) {
    m_poll_vector.insert(m_poll_vector.end(), {
      {VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_SPEED,    {  0,  0,  0,  1}, 1, ISOTP_STD},
      // … speed interval = VWGOLF_BAT_MGMT_U & _I to get a consistent consumption calculation
    });
  }

  // Add high priority PIDs:
  m_poll_vector.insert(m_poll_vector.end(), {
    {VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_POWER_MOT,  {  0,  0,  0,  1}, 1, ISOTP_STD},
  });

  // Add model year specific AC charger PIDs:
  // Note: these end with the charge metrics to fetch them directly before the battery metrics
  if (vwegolf_modelyear < 2017) {
    m_poll_vector.insert(m_poll_vector.end(), vwegolf_gen1_polls, endof_array(vwegolf_gen1_polls));
  }
  else {
    m_poll_vector.insert(m_poll_vector.end(), vwegolf_gen2_polls, endof_array(vwegolf_gen2_polls));
  }

  // Add general / common PIDs:
  // Note: these begin with the battery metrics to fetch them directly after the charge metrics
  m_poll_vector.insert(m_poll_vector.end(), vwegolf_polls, endof_array(vwegolf_polls));

  // Add test/log PIDs for DC fast charging:
  if (m_cfg_dc_interval) {
    for (auto p : poll_list_t {
        {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_TEST_1DD7, {  0,  0,  0,  0}, 1, ISOTP_STD},
        {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_TEST_1DDA, {  0,  0,  0,  0}, 1, ISOTP_STD},
        {VWGOLF_CHG_MGMT, UDS_READ, VWGOLF_CHG_MGMT_TEST_1DE6, {  0,  0,  0,  0}, 1, ISOTP_STD},
      }) {
      p.polltime[VWEGOLF_AWAKE]     = m_cfg_dc_interval;
      p.polltime[VWEGOLF_CHARGING]  = m_cfg_dc_interval;
      m_poll_vector.push_back(p);
    }
  }

  // Add low priority PIDs only necessary without T26:
  if (HasNoT26()) {
    m_poll_vector.insert(m_poll_vector.end(), {
      {VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_TEMP_AMB, {  0,  0,  0, 30}, 1, ISOTP_STD},
      {VWGOLF_MFD,      UDS_READ, VWGOLF_MFD_RANGE_DSP,     {  0,  0,  0, 30}, 1, ISOTP_STD},
    });
  }

  // Add BMS cell PIDs if enabled:
  if (m_cfg_cell_interval_drv || m_cfg_cell_interval_chg || m_cfg_cell_interval_awk)
  {
    // Battery pack layout:
    //  Gen2 (2017): 2P84S in 14 modules
    //  Gen1 (2014): 2P102S in 16+1 modules
    int volts = (vwegolf_modelyear > 2016) ? 84 : 102;
    int cmods = (vwegolf_modelyear > 2016) ? 14 : 17;

    // Add PIDs to poll list:
    OvmsVehicle::poll_pid_t p = { VWGOLF_BAT_MGMT, UDS_READ, 0, {0,0,0,0}, 1, ISOTP_STD };
    p.polltime[VWEGOLF_ON]        = m_cfg_cell_interval_drv;
    p.polltime[VWEGOLF_CHARGING]  = m_cfg_cell_interval_chg;
    p.polltime[VWEGOLF_AWAKE]     = m_cfg_cell_interval_awk;

    // From voltages & deviation under load, it seems voltages are numbered across the
    // cell modules, with the first group of values corresponding to the cell packs
    // at the outer edges of the modules (tend to show lower values & negative deviation).
    // As the gradient analysis relies on cell index matching read index, we need
    // to rearrange the voltage poll sequence:
    for (int vi = 0; vi < volts; vi++) {
      int pi = (vi % 6) * cmods + (vi / 6);
      p.pid = VWGOLF_BAT_MGMT_CELL_VBASE + pi;
      m_poll_vector.push_back(p);
    }

    // Add temperature polls (one per cell module):
    for (int ti = 0; ti < cmods; ti++) {
      p.pid = (ti < 16) ? VWGOLF_BAT_MGMT_CELL_TBASE + ti : VWGOLF_BAT_MGMT_CELL_T17;
      m_poll_vector.push_back(p);
    }

    // Init processing:
    m_cell_last_vi = 0;
    m_cell_last_ti = 0;
    BmsRestartCellVoltages();
    BmsRestartCellTemperatures();
  }

  // Terminate poll list:
  m_poll_vector.push_back(POLL_LIST_END);
  ESP_LOGD(TAG, "Poll vector: size=%d cap=%d", m_poll_vector.size(), m_poll_vector.capacity());
  PollSetPidList(m_can1, m_poll_vector.data());

  m_obd_state = OBDS_Run;
}


void OvmsVehicleVWeGolf::OBDDeInit()
{
  m_obd_state = OBDS_DeInit;
  ESP_LOGI(TAG, "Stopping connection: OBDII");
  PollSetPidList(m_can1, NULL);
}


/**
 * PollSetState: set the polling state, log the change/call
 */
void OvmsVehicleVWeGolf::PollSetState(uint8_t state)
{
  const char *statename[] = { "OFF", "AWAKE", "CHARGING", "ON" };
  ESP_LOGI(TAG, "PollSetState: %s -> %s", statename[m_poll_state], statename[state]);
  OvmsVehicle::PollSetState(state);
}


/**
 * PollerStateTicker: check for state changes
 *  This is called by VehicleTicker1() just before the next PollerSend().
 */
void OvmsVehicleVWeGolf::PollerStateTicker()
{
  // T26 state management has precedence if available:
  if (HasT26() || m_obd_state != OBDS_Run)
    return;

  bool car_online = (m_can1->GetErrorState() < CAN_errorstate_passive && !m_lv_pwrstate->IsStale());
  int lv_pwrstate = m_lv_pwrstate->AsInt();
  int hv_chgmode = m_hv_chgmode->AsInt();
  float dcdc_voltage = StdMetrics.ms_v_charge_12v_voltage->AsFloat();

  // Determine and prioritize the new polling state:
  //  - if lv_pwrstate didn't get an update for 3 seconds, the car is off
  //  - if the HV charger is active, we're in charge mode
  //      (Note: the car may still also be switched on by the user while charging,
  //        but we consider that to require concentrating on the charge metric polling)
  //  - if the LV system is fully online, the car is switched on
  //  - else it's awake

  int poll_state = 0;
  if (!car_online)
    poll_state = VWEGOLF_OFF;
  else if (hv_chgmode)
    poll_state = VWEGOLF_CHARGING;
  else if (lv_pwrstate > 12)
    poll_state = VWEGOLF_ON;
  else
    poll_state = VWEGOLF_AWAKE;

  //
  // Determine independent state flags
  //

  // - base system is awake if we've got a fresh lv_pwrstate:
  StdMetrics.ms_v_env_aux12v->SetValue(car_online);

  // - charging / trickle charging 12V battery is active when lv_pwrstate is not zero:
  StdMetrics.ms_v_env_charging12v->SetValue(car_online && lv_pwrstate > 0);

  // - v_env_awake = car has been switched on by the user (yeah, confusing name, may be changed itf)
  StdMetrics.ms_v_env_awake->SetValue(car_online && lv_pwrstate > 12);

  // - v_env_on = "ignition" / drivable mode: this is currently still a heuristical guess;
  //      set when ON & DC converter puts out more than 14V, reset when not ON
  //      (TODO: find proper PID)
  if (poll_state != VWEGOLF_ON) {
    StdMetrics.ms_v_env_on->SetValue(false);
  }
  else if (StdMetrics.ms_v_env_on->AsBool() == false && dcdc_voltage > 14) {
    // TODO: get real charge port state
    // For now, we assume the port has been closed when the car is started:
    StdMetrics.ms_v_charge_duration_full->SetValue(0);
    StdMetrics.ms_v_door_chargeport->SetValue(false);
    StdMetrics.ms_v_charge_substate->SetValue("");
    StdMetrics.ms_v_charge_state->SetValue("");
    StdMetrics.ms_v_env_on->SetValue(true);
  }

  //
  // Handle polling state change
  //

  if (poll_state != m_poll_state) {
    ESP_LOGD(TAG,
      "PollerStateTicker: [%s] LVPwrState=%d HVChgMode=%d LVAutoChg=%d "
      "12V=%.1f DCDC_U=%.1f DCDC_I=%.1f ChgEff=%.1f BatI=%.1f BatIAge=%u => PollState %d->%d",
      car_online ? "online" : "offline", lv_pwrstate, hv_chgmode, m_lv_autochg->AsInt(),
      StdMetrics.ms_v_bat_12v_voltage->AsFloat(),
      dcdc_voltage, StdMetrics.ms_v_charge_12v_current->AsFloat(),
      ChargerPowerEffEcu->AsFloat(),
      StdMetrics.ms_v_bat_current->AsFloat(), StdMetrics.ms_v_bat_current->Age(),
      m_poll_state, poll_state);
  }

  if (poll_state == VWEGOLF_CHARGING) {
    if (!IsCharging()) {
      ESP_LOGI(TAG, "PollerStateTicker: Setting car state to CHARGING");

      // Start new charge:
      SetUsePhase(UP_Charging);
      ResetChargeCounters();

      // TODO: get real port & pilot states, fake for now:
      StdMetrics.ms_v_door_chargeport->SetValue(true);
      StdMetrics.ms_v_charge_pilot->SetValue(true);

      UpdateChargeParams();
      SetChargeState(true);

      PollSetState(VWEGOLF_CHARGING);
    }
    return;
  }
  else if (IsCharging()) {
    ESP_LOGI(TAG, "PollerStateTicker: Charge stopped/done");

    // TODO: get real charge pilot states, fake for now:
    StdMetrics.ms_v_charge_pilot->SetValue(false);

    SetChargeState(false);
  }

  if (poll_state == VWEGOLF_ON) {
    if (!IsOn()) {
      ESP_LOGI(TAG, "PollerStateTicker: Setting car state to ON");

      // Start new trip:
      SetUsePhase(UP_Driving);
      ResetTripCounters();

      // Fetch VIN once:
      if (!StdMetrics.ms_v_vin->IsDefined()) {
        std::string vin;
        if (PollSingleRequest(m_can1, VWGOLF_MOT_ELEC, UDS_READ, VWGOLF_MOT_ELEC_VIN, vin) == 0) {
          StdMetrics.ms_v_vin->SetValue(vin.substr(1));
        }
      }

      // Start regular polling:
      PollSetState(VWEGOLF_ON);
    }
    return;
  }

  if (poll_state == VWEGOLF_AWAKE) {
    if (!IsAwake()) {
      ESP_LOGI(TAG, "PollerStateTicker: Setting car state to AWAKE");
      PollSetState(VWEGOLF_AWAKE);
    }
    return;
  }

  if (poll_state == VWEGOLF_OFF) {
    if (!IsOff()) {
      ESP_LOGI(TAG, "PollerStateTicker: Setting car state to OFF");
      PollSetState(VWEGOLF_OFF);

      // Clear powers & currents in case we missed the zero reading:
      StdMetrics.ms_v_bat_current->SetValue(0);
      StdMetrics.ms_v_bat_power->SetValue(0);
      StdMetrics.ms_v_bat_12v_current->SetValue(0);
      StdMetrics.ms_v_charge_12v_current->SetValue(0);
      StdMetrics.ms_v_charge_12v_power->SetValue(0);
      StdMetrics.ms_v_charge_12v_voltage->SetValue(0);
    }
    return;
  }
}


void OvmsVehicleVWeGolf::IncomingGPollReply(canbus *bus, uint16_t type, uint16_t pid, uint8_t *data, uint8_t length, uint16_t mlremain)
{
  if (m_obd_state != OBDS_Run)
    return;

  // If not all data is here: wait for the next call
  if (!GPollReply.AddNewData(pid, data, length, mlremain)) {
    return;
  }

  float value;
  int ivalue;

  //
  // Handle reply for diagnostic session
  //

  if (type == UDS_SESSION)
    return;

  //
  // Handle BMS cell voltage & temperatures
  //

  if (pid >= VWGOLF_BAT_MGMT_CELL_VBASE && pid <= VWGOLF_BAT_MGMT_CELL_VLAST)
  {
    uint16_t pi = pid - VWGOLF_BAT_MGMT_CELL_VBASE;

    // get cell index from poll index:
    uint16_t cmods = (vwegolf_modelyear > 2016) ? 14 : 17;
    uint16_t vi = (pi % cmods) * 6 + (pi / cmods);

    if (vi < m_cell_last_vi) {
      BmsRestartCellVoltages();
    }
    if (GPollReply.FromUint16("VWGOLF_BAT_MGMT_CELL_VOLT", value)) {
      BmsSetCellVoltage(vi, value / 4096);
    }
    m_cell_last_vi = vi;
    return;
  }

  if ((pid >= VWGOLF_BAT_MGMT_CELL_TBASE && pid <= VWGOLF_BAT_MGMT_CELL_TLAST) ||
      (pid == VWGOLF_BAT_MGMT_CELL_T17))
  {
    uint16_t ti = (pid == VWGOLF_BAT_MGMT_CELL_T17) ? 16 : pid - VWGOLF_BAT_MGMT_CELL_TBASE;
    if (ti < m_cell_last_ti) {
      BmsRestartCellTemperatures();
    }
    if (GPollReply.FromUint16("VWGOLF_BAT_MGMT_CELL_TEMP", value)) {
      BmsSetCellTemperature(ti, value / 64);
    }
    m_cell_last_ti = ti;
    return;
  }

  //
  // Handle regular PIDs
  //

  switch (pid) {

    case VWGOLF_CHG_MGMT_LV_PWRSTATE:
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_LV_PWRSTATE", ivalue)) {
        m_lv_pwrstate->SetValue(ivalue);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_LV_PWRSTATE=%d", ivalue);
      }
      break;

    case VWGOLF_CHG_MGMT_LV_AUTOCHG:
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_LV_AUTOCHG", ivalue)) {
        m_lv_autochg->SetValue(ivalue);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_LV_AUTOCHG=%d", ivalue);
      }
      break;

    case VWGOLF_CHG_MGMT_HV_CHGMODE:
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_HV_CHGMODE", ivalue)) {
        m_hv_chgmode->SetValue(ivalue);
        if (ivalue >= 4)
          SetChargeType(CHGTYPE_DC);
        else if (ivalue >= 1)
          SetChargeType(CHGTYPE_AC);
        else
          SetChargeType(CHGTYPE_None);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_HV_CHGMODE=%d", ivalue);
      }
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_TIMERMODE", ivalue, 1)) {
        bool timermode = (ivalue != 0);
        bool modified = StdMetrics.ms_v_charge_timermode->SetValue(timermode);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_TIMERMODE=%d", ivalue);
        if (modified)
          UpdateChargeParams();
      }
      break;

    case VWGOLF_CHG_MGMT_TIMER_DEF:
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_TIMER_DEF", ivalue)) {
        bool timerdef = (ivalue != 0);
        m_chg_timer_def->SetValue(timerdef);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_TIMER_DEF=%d", ivalue);
      }
      break;

    case VWGOLF_CHG_MGMT_SOC_LIMITS: {
      int socmin, socmax;
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_SOC_LIMIT_MAX", socmax, 1)) {
        GPollReply.FromUint8("VWGOLF_CHG_MGMT_SOC_LIMIT_MIN", socmin);
        bool modified =
          m_chg_timer_socmin->SetValue(socmin) |
          m_chg_timer_socmax->SetValue(socmax);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_SOC_LIMITS MIN=%d%% MAX=%d%%", socmin, socmax);
        if (modified)
          UpdateChargeParams();
      }
      break;
    }

    case VWGOLF_BAT_MGMT_U:
      if (GPollReply.FromUint16("VWGOLF_BAT_MGMT_U", value)) {
        StdMetrics.ms_v_bat_voltage->SetValue(value / 4.0f);
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_U=%f => %f", value, StdMetrics.ms_v_bat_voltage->AsFloat());
      }
      break;
    case VWGOLF_BAT_MGMT_I:
      if (GPollReply.FromUint16("VWGOLF_BAT_MGMT_I", value)) {
        // ECU delivers negative current when it goes out of the battery. OVMS wants positive when the battery outputs current.
        StdMetrics.ms_v_bat_current->SetValue(((value - 2044.0f) / 4.0f) * -1.0f);
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_I=%f => %f", value, StdMetrics.ms_v_bat_current->AsFloat());

        value = StdMetrics.ms_v_bat_voltage->AsFloat() * StdMetrics.ms_v_bat_current->AsFloat() / 1000.0f;
        StdMetrics.ms_v_bat_power->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_POWER=%f => %f", value, StdMetrics.ms_v_bat_power->AsFloat());
      }
      break;

    case VWGOLF_MOT_ELEC_SOC_NORM:
      // Gets updates while driving
      if (GPollReply.FromUint16("VWGOLF_MOT_ELEC_SOC_NORM", value)) {
        StdMetrics.ms_v_bat_soc->SetValue(value / 100.0f);
        MotElecSoCNorm->SetValue(value / 100.0f);
        VALUE_LOG(TAG, "VWGOLF_MOT_ELEC_SOC_NORM=%f => %f", value, StdMetrics.ms_v_bat_soc->AsFloat());
        // Update range:
        StandardMetrics.ms_v_bat_range_ideal->SetValue(
          StdMetrics.ms_v_bat_range_full->AsFloat() * (StdMetrics.ms_v_bat_soc->AsFloat() / 100));
      }
      break;

    case VWGOLF_CHG_MGMT_SOC_NORM:
      // Gets updates while charging
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_SOC_NORM", value)) {
        float soc = value / 2.0f;
        StdMetrics.ms_v_bat_soc->SetValue(soc);
        ChgMgmtSoCNorm->SetValue(soc);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_SOC_NORM=%f => %f", value, soc);
        // Update range:
        StdMetrics.ms_v_bat_range_ideal->SetValue(
          StdMetrics.ms_v_bat_range_full->AsFloat() * (soc / 100));
        if (HasNoT26()) {
          // Calculate estimated range from last known factor:
          StdMetrics.ms_v_bat_range_est->SetValue(soc * m_range_est_factor);
        }
      }
      break;

    case VWGOLF_MFD_RANGE_DSP:
      // Gets updates while driving
      if (GPollReply.FromUint16("VWGOLF_MFD_RANGE_DSP", value)) {
        StdMetrics.ms_v_bat_range_est->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_MFD_RANGE_DSP=%f", value);
        // Update range factor for calculation during charge:
        float soc = StdMetrics.ms_v_bat_soc->AsFloat();
        if (value > 10 && soc > 10) {
          float range_factor = value / soc;
          if (range_factor > 0.1) {
            m_range_est_factor = range_factor;
          }
        }
      }
      break;

    case VWGOLF_MFD_RANGE_CAP:
      if (GPollReply.FromUint16("VWGOLF_MFD_RANGE_CAP", value) && value != 511) {
        // Usable battery energy [kWh] from range estimation:
        float energy_avail = value / 10;
        m_bat_energy_range->SetValue(energy_avail);
        VALUE_LOG(TAG, "VWGOLF_MFD_RANGE_ENERGY=%g => %.1fkWh", value, energy_avail);
        // Stable capacity derivation only possible for SOC >= 30%:
        float soc = StdMetrics.ms_v_bat_soc->AsFloat();
        if (soc >= 30) {
          float capacity = energy_avail / soc * 100;
          m_bat_cap_kwh_range->SetValue(capacity);
          VALUE_LOG(TAG, "VWGOLF_MFD_RANGE_CAP=%.1fkWh", capacity);
        }
      }
      break;

    case VWGOLF_MOT_ELEC_SOC_ABS:
      // Gets updates only while driving
      if (GPollReply.FromUint8("VWGOLF_MOT_ELEC_SOC_ABS", value)) {
        MotElecSoCAbs->SetValue(value / 2.55f);
        VALUE_LOG(TAG, "VWGOLF_MOT_ELEC_SOC_ABS=%f => %f", value, MotElecSoCAbs->AsFloat());
      }
      break;

    case VWGOLF_BAT_MGMT_SOC_ABS:
      if (GPollReply.FromUint8("VWGOLF_BAT_MGMT_SOC_ABS", value)) {
        BatMgmtSoCAbs->SetValue(value / 2.5f);
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_SOC_ABS=%f => %f", value, BatMgmtSoCAbs->AsFloat());
      }
      break;

    case VWGOLF_BAT_MGMT_ENERGY_COUNTERS: {
      const float coulomb_factor = 0.0018204444;
      const float energy_factor  =  0.0001165084;
      bool charge_inprogress = StdMetrics.ms_v_charge_inprogress->AsBool();
      if (GPollReply.FromInt32("VWGOLF_BAT_MGMT_COULOMB_COUNTERS_RECD", value, 0)) {
        float coulomb_recd_total = value * coulomb_factor;
        StdMetrics.ms_v_bat_coulomb_recd_total->SetValue(coulomb_recd_total);
        // Get trip difference:
        if (!charge_inprogress) {
          if (m_coulomb_recd_start <= 0)
            m_coulomb_recd_start = coulomb_recd_total;
          StdMetrics.ms_v_bat_coulomb_recd->SetValue(coulomb_recd_total - m_coulomb_recd_start);
        }
        else {
          if (m_coulomb_charged_start <= 0)
            m_coulomb_charged_start = coulomb_recd_total;
        }
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_COULOMB_COUNTERS_RECD=%f => %f", value, coulomb_recd_total);
      }
      if (GPollReply.FromInt32("VWGOLF_BAT_MGMT_COULOMB_COUNTERS_USED", value, 4)) {
        // Used is negative here, standard metric is positive
        float coulomb_used_total = -value * coulomb_factor;
        StdMetrics.ms_v_bat_coulomb_used_total->SetValue(coulomb_used_total);
        // Get trip difference:
        if (!charge_inprogress) {
          if (m_coulomb_used_start <= 0)
            m_coulomb_used_start = coulomb_used_total;
          StdMetrics.ms_v_bat_coulomb_used->SetValue(coulomb_used_total - m_coulomb_used_start);
        }
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_COULOMB_COUNTERS_USED=%f => %f", value, coulomb_used_total);
      }
      if (GPollReply.FromInt32("VWGOLF_BAT_MGMT_ENERGY_COUNTERS_RECD", value, 8)) {
        float energy_recd_total = value * energy_factor;
        StdMetrics.ms_v_bat_energy_recd_total->SetValue(energy_recd_total);
        // Get charge/trip difference:
        if (!charge_inprogress) {
          if (m_energy_recd_start <= 0)
            m_energy_recd_start = energy_recd_total;
          StdMetrics.ms_v_bat_energy_recd->SetValue(energy_recd_total - m_energy_recd_start);
        }
        else {
          if (m_energy_charged_start <= 0)
            m_energy_charged_start = energy_recd_total;
          StdMetrics.ms_v_charge_kwh->SetValue(energy_recd_total - m_energy_charged_start);
        }
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_ENERGY_COUNTERS_RECD=%f => %f", value, energy_recd_total);
      }
      if (GPollReply.FromInt32("VWGOLF_BAT_MGMT_ENERGY_COUNTERS_USED", value, 12)) {
        // Used is negative here, standard metric is positive
        float energy_used_total = -value * energy_factor;
        StdMetrics.ms_v_bat_energy_used_total->SetValue(energy_used_total);
        // Get trip difference:
        if (!charge_inprogress) {
          if (m_energy_used_start <= 0)
            m_energy_used_start = energy_used_total;
          StdMetrics.ms_v_bat_energy_used->SetValue(energy_used_total - m_energy_used_start);
        }
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_ENERGY_COUNTERS_USED=%f => %f", value, energy_used_total);
      }

      // After receiving the new SOCs & coulomb counts, we can update our capacities:
      UpdateChargeCap(charge_inprogress);
      break;
    }

    case VWGOLF_BAT_MGMT_CELL_MAX:
      if (GPollReply.FromUint16("VWGOLF_BAT_MGMT_CELL_MAX", value)) {
        BatMgmtCellMax = value / 4096.0f;
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_CELL_MAX=%f => %f", value, BatMgmtCellMax);
        StdMetrics.ms_v_bat_pack_vmax->SetValue(BatMgmtCellMax);
      }
      break;

    case VWGOLF_BAT_MGMT_CELL_MIN:
      if (GPollReply.FromUint16("VWGOLF_BAT_MGMT_CELL_MIN", value)) {
        BatMgmtCellMin = value / 4096.0f;
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_CELL_MIN=%f => %f", value, BatMgmtCellMin);
        StdMetrics.ms_v_bat_pack_vmin->SetValue(BatMgmtCellMin);

        value = BatMgmtCellMax - BatMgmtCellMin;
        BatMgmtCellDelta->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_CELL_DELTA=%f => %f", value, BatMgmtCellDelta->AsFloat());
      }
      break;

    case VWGOLF_BAT_MGMT_TEMP:
      if (GPollReply.FromInt16("VWGOLF_BAT_MGMT_TEMP", value)) {
        StdMetrics.ms_v_bat_temp->SetValue(value / 64.0f);
        VALUE_LOG(TAG, "VWGOLF_BAT_MGMT_TEMP=%f => %f", value, StdMetrics.ms_v_bat_temp->AsFloat());
      }
      break;

    case VWGOLF1_CHG_AC_U:
      if (GPollReply.FromUint16("VWGOLF_CHG_AC1_U", value) && value != 511) {
        if (IsChargeModeAC()) {
          StdMetrics.ms_v_charge_voltage->SetValue(value);
        }
        VALUE_LOG(TAG, "VWGOLF_CHG_AC1_U=%f", value);
      }
      break;

    case VWGOLF1_CHG_AC_I:
      if (GPollReply.FromUint8("VWGOLF_CHG_AC1_I", value) && value != 255) {
        float current = value / 10;
        VALUE_LOG(TAG, "VWGOLF_CHG_AC1_I=%f => %f", value, current);

        if (IsChargeModeAC()) {
          float power = (StdMetrics.ms_v_charge_voltage->AsFloat() * current) / 1000.0f;
          ChargerACPower->SetValue(power);
          VALUE_LOG(TAG, "VWGOLF_CHG_AC_P=%.1f", power);

          StdMetrics.ms_v_charge_current->SetValue(current);
          UpdateChargePower(power);
        }
      }
      break;

    case VWGOLF2_CHG_AC_U: {
      int phasecnt = 0;
      float voltagesum = 0;

      if (GPollReply.FromUint16("VWGOLF_CHG_AC1_U", value) && value != 511) {
        ChargerAC1U->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_AC1_U=%f => %f", value, ChargerAC1U->AsFloat());
        if (value > 90) {
          phasecnt++;
          voltagesum += value;
        }
      }
      if (GPollReply.FromUint16("VWGOLF_CHG_AC2_U", value, 2) && value != 511) {
        ChargerAC2U->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_AC2_U=%f => %f", value, ChargerAC2U->AsFloat());
        if (value > 90) {
          phasecnt++;
          voltagesum += value;
        }
      }
      if (phasecnt > 1) {
        voltagesum /= phasecnt;
      }
      if (IsChargeModeAC()) {
        StdMetrics.ms_v_charge_voltage->SetValue(voltagesum);
      }
      break;
    }

    case VWGOLF2_CHG_AC_I:
      if (GPollReply.FromUint8("VWGOLF_CHG_AC1_I", value) && value != 255) {
        ChargerAC1I->SetValue(value / 10.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_AC1_I=%f => %f", value, ChargerAC1I->AsFloat());
      }
      if (GPollReply.FromUint8("VWGOLF_CHG_AC2_I", value, 1) && value != 255) {
        ChargerAC2I->SetValue(value / 10.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_AC2_I=%f => %f", value, ChargerAC2I->AsFloat());

        if (IsChargeModeAC()) {
          float power = (ChargerAC1U->AsFloat() * ChargerAC1I->AsFloat() +
                         ChargerAC2U->AsFloat() * ChargerAC2I->AsFloat()) / 1000.0f;
          ChargerACPower->SetValue(power);
          VALUE_LOG(TAG, "VWGOLF_CHG_AC_P=%.1f", power);

          StdMetrics.ms_v_charge_current->SetValue(ChargerAC1I->AsFloat() + ChargerAC2I->AsFloat());
          UpdateChargePower(power);
        }
      }
      break;

    case VWGOLF1_CHG_DC_U:
      if (GPollReply.FromUint16("VWGOLF_CHG_DC_U", value)) {
        ChargerDC1U->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC_U=%f => %f", value, ChargerDC1U->AsFloat());
      }
      break;

    case VWGOLF1_CHG_DC_I:
      if (GPollReply.FromUint16("VWGOLF_CHG_DC_I", value)) {
        ChargerDC1I->SetValue((value - 510.0f) / 5.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC_I=%f => %f", value, ChargerDC1I->AsFloat());

        value = (ChargerDC1U->AsFloat() * ChargerDC1I->AsFloat()) / 1000.0f;
        ChargerDCPower->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC_P=%f => %f", value, ChargerDCPower->AsFloat());

        value = ChargerACPower->AsFloat() - ChargerDCPower->AsFloat();
        ChargerPowerLossCalc->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_LOSS_CALC=%f => %f", value, ChargerPowerLossCalc->AsFloat());

        value = ChargerACPower->AsFloat() > 0
                ? ChargerDCPower->AsFloat() / ChargerACPower->AsFloat() * 100.0f
                : 0.0f;
        ChargerPowerEffCalc->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_EFF_CALC=%f => %f", value, ChargerPowerEffCalc->AsFloat());
      }
      break;

    case VWGOLF2_CHG_DC_U:
      if (GPollReply.FromUint16("VWGOLF_CHG_DC1_U", value)) {
        ChargerDC1U->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC1_U=%f => %f", value, ChargerDC1U->AsFloat());
      }
      if (GPollReply.FromUint16("VWGOLF_CHG_DC2_U", value, 2)) {
        ChargerDC2U->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC2_U=%f => %f", value, ChargerDC2U->AsFloat());
      }
      break;

    case VWGOLF2_CHG_DC_I:
      if (GPollReply.FromUint16("VWGOLF_CHG_DC1_I", value) && value != 1023) {
        ChargerDC1I->SetValue((value - 510.0f) / 5.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC1_I=%f => %f", value, ChargerDC1I->AsFloat());
      }
      if (GPollReply.FromUint16("VWGOLF_CHG_DC2_I", value, 2) && value != 1023) {
        ChargerDC2I->SetValue((value - 510.0f) / 5.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC2_I=%f => %f", value, ChargerDC2I->AsFloat());

        value = (ChargerDC1U->AsFloat() * ChargerDC1I->AsFloat() +
                 ChargerDC2U->AsFloat() * ChargerDC2I->AsFloat()) / 1000.0f;
        ChargerDCPower->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_DC_P=%f => %f", value, ChargerDCPower->AsFloat());

        value = ChargerACPower->AsFloat() - ChargerDCPower->AsFloat();
        ChargerPowerLossCalc->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_LOSS_CALC=%f => %f", value, ChargerPowerLossCalc->AsFloat());

        value = ChargerACPower->AsFloat() > 0
                ? ChargerDCPower->AsFloat() / ChargerACPower->AsFloat() * 100.0f
                : 0.0f;
        ChargerPowerEffCalc->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_CHG_EFF_CALC=%f => %f", value, ChargerPowerEffCalc->AsFloat());
      }
      break;

    case VWGOLF_CHG_MGMT_CCS_STATUS:
      // CCS charge status
      // not fully decoded yet, log for analysis:
      VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_CCS_STATUS: %s", GPollReply.GetHexString().c_str());
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_CCS_LOCK", ivalue, 13) && ivalue != 0) {
        // CCS locked:
        GPollReply.FromUint16("VWGOLF_CHG_MGMT_CCS_U", value, 4);
        float voltage = value / 10;
        m_chg_ccs_voltage->SetValue(voltage);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_CCS_U=%g => %.1f", value, voltage);

        GPollReply.FromUint16("VWGOLF_CHG_MGMT_CCS_I", value, 10);
        float current = value / 10;
        m_chg_ccs_current->SetValue(current);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_CCS_I=%g => %.1f", value, current);

        float power = voltage * current / 1000;
        m_chg_ccs_power->SetValue(power);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_CCS_P => %.1f", power);

        if (IsChargeModeDC()) {
          // CCS_U and CCS_I are provided by the CCS charger with varying resolution and
          //  precision / accuracy depending on the charger type.
          // CCS_U is allowed to be off by +/- 5% by IEC 61851 and has shown to be
          //  very unreliable, being normally some volts below the measured battery voltage.
          // CCS_I is allowed to be off by +/- 3% but seems to be reliable normally.
          // To get a better power estimation we use the battery voltage instead of CCS_U:
          voltage = StdMetrics.ms_v_bat_voltage->AsFloat();
          power = voltage * current / 1000;
          // Notes:
          //  This power normally is still a bit below the power displayed by the charger (if any).
          //  The power displayed by the charger is substantially above CCS_U x CCS_I, so
          //  the charger possibly displays it's input power, but there doesn't seem to
          //  be a way to retrieve that or the charger efficiency (none known yet).
          StdMetrics.ms_v_charge_voltage->SetValue(voltage);
          StdMetrics.ms_v_charge_current->SetValue(current);
          UpdateChargePower(power);
        }
      }
      break;

    case VWGOLF_CHG_POWER_EFF:
      // Value is offset from 750d%. So a value > 250 would be (more) than 100% efficiency!
      // This means no charging is happening at the moment (standardvalue replied for no charging is 0xFE)
      if (GPollReply.FromUint8("VWGOLF_CHG_POWER_EFF", value)) {
        ChargerPowerEffEcu->SetValue(value <= 250.0f ? value / 10.0f + 75.0f : 0.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_POWER_EFF=%f => %f", value, ChargerPowerEffEcu->AsFloat());
      }
      break;

    case VWGOLF_CHG_POWER_LOSS:
      if (GPollReply.FromUint8("VWGOLF_CHG_POWER_LOSS", value)) {
        ChargerPowerLossEcu->SetValue((value * 20.0f) / 1000.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_POWER_LOSS=%f => %f", value, ChargerPowerLossEcu->AsFloat());
      }
      break;

    case VWGOLF_MOT_ELEC_SPEED:
      if (GPollReply.FromUint8("VWGOLF_MOT_ELEC_SPEED", value)) {
        StdMetrics.ms_v_pos_speed->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_MOT_ELEC_SPEED=%f", value);
      }
      break;
    case VWGOLF_MOT_ELEC_POWER_MOT:
      if (GPollReply.FromInt16("VWGOLF_MOT_ELEC_POWER_MOT", value)) {
        StdMetrics.ms_v_inv_power->SetValue(value / 250);
        VALUE_LOG(TAG, "VWGOLF_MOT_ELEC_POWER_MOT=%f => %f", value, StdMetrics.ms_v_inv_power->AsFloat());
      }
      break;
    case VWGOLF_MFD_ODOMETER:
      if (GPollReply.FromUint16("VWGOLF_MFD_ODOMETER", value)) {
        float odo = value * 10.0f;
        StdMetrics.ms_v_pos_odometer->SetValue(odo);
        // Set trip reference / difference:
        if (m_odo_start <= 0)
          m_odo_start = odo;
        StdMetrics.ms_v_pos_trip->SetValue(odo - m_odo_start);
        VALUE_LOG(TAG, "VWGOLF_MFD_ODOMETER=%f => %f", value, odo);
      }
      break;

    case VWGOLF_MFD_SERV_RANGE:
      if (GPollReply.FromUint16("VWGOLF_MFD_SERV_RANGE", value) && value > 0) { // excluding value of 0 seems to be necessary for now
        // Send notification?
        int threshold = MyConfig.GetParamValueInt("xvg", "serv_warn_range", 5000);
        int old_value = StdMetrics.ms_v_env_service_range->AsInt();
        if (old_value > threshold && value <= threshold) { 
          MyNotify.NotifyStringf("info", "serv.range", "Service range left: %d km!", value);
        }         
        StdMetrics.ms_v_env_service_range->SetValue(value);
        VALUE_LOG(TAG, "VWGOLF_MFD_SERV_RANGE=%f => %f", value, StdMetrics.ms_v_env_service_range->AsFloat());
      }
      break;
    case VWGOLF_MFD_SERV_TIME:
      if (GPollReply.FromUint16("VWGOLF_MFD_SERV_TIME", value) && value > 0) { // excluding value of 0 seems to be necessary for now
        // Send notification?
        int now = StdMetrics.ms_m_timeutc->AsInt();
        int threshold = MyConfig.GetParamValueInt("xvg", "serv_warn_days", 30);
        int old_value = ROUNDPREC((StdMetrics.ms_v_env_service_time->AsInt() - now) / 86400.0f, 0);
        if (old_value > threshold && value <= threshold) {
          MyNotify.NotifyStringf("info", "serv.time", "Service time left: %d days!", value);
        }         
        ServiceDays -> SetValue(value);
        StdMetrics.ms_v_env_service_time->SetValue(StdMetrics.ms_m_timeutc->AsInt() + value * 86400);
        VALUE_LOG(TAG, "VWGOLF_MFD_SERV_TIME=%f => %f", value, StdMetrics.ms_v_env_service_time->AsFloat());
      }
      break;

    case VWGOLF_MOT_ELEC_TEMP_DCDC:
      if (GPollReply.FromUint16("VWGOLF_MOT_ELEC_TEMP_DCDC", value)) {
        StdMetrics.ms_v_charge_12v_temp->SetValue(value / 10.0f - 273.1f);
        VALUE_LOG(TAG, "VWGOLF_MOT_ELEC_TEMP_DCDC=%f => %f", value, StdMetrics.ms_v_charge_12v_temp->AsFloat());
      }
      break;
    case VWGOLF_ELD_DCDC_U:
      if (GPollReply.FromUint16("VWGOLF_ELD_DCDC_U", value)) {
        StdMetrics.ms_v_charge_12v_voltage->SetValue(value / 512.0f);
        VALUE_LOG(TAG, "VWGOLF_ELD_DCDC_U=%f => %f", value, StdMetrics.ms_v_charge_12v_voltage->AsFloat());
      }
      break;
    case VWGOLF_ELD_DCDC_I:
      if (GPollReply.FromUint16("VWGOLF_ELD_DCDC_I", value)) {
        StdMetrics.ms_v_charge_12v_current->SetValue(value / 16.0f);
        StdMetrics.ms_v_bat_12v_current->SetValue(value / 16.0f); // until we find a separate reading
        VALUE_LOG(TAG, "VWGOLF_ELD_DCDC_I=%f => %f", value, StdMetrics.ms_v_charge_12v_current->AsFloat());
        StdMetrics.ms_v_charge_12v_power->SetValue(
          StdMetrics.ms_v_charge_12v_voltage->AsFloat() * StdMetrics.ms_v_charge_12v_current->AsFloat());
        VALUE_LOG(TAG, "VWGOLF_ELD_DCDC_P=%f => %f",
          StdMetrics.ms_v_charge_12v_power->AsFloat(), StdMetrics.ms_v_charge_12v_power->AsFloat());
      }
      break;

    case VWGOLF_ELD_TEMP_MOT:
      if (GPollReply.FromInt16("VWGOLF_ELD_TEMP_MOT", value)) {
        StdMetrics.ms_v_mot_temp->SetValue(value / 64.0f);
        VALUE_LOG(TAG, "VWGOLF_ELD_TEMP_MOT=%f => %f", value, StdMetrics.ms_v_mot_temp->AsFloat());
      }
      break;
    case VWGOLF_MOT_ELEC_TEMP_PEM:
      if (GPollReply.FromUint16("VWGOLF_MOT_ELEC_TEMP_PEM", value)) {
        StdMetrics.ms_v_inv_temp->SetValue(value / 10.0f - 273.1);
        VALUE_LOG(TAG, "VWGOLF_MOT_ELEC_TEMP_PEM=%f => %f", value, StdMetrics.ms_v_inv_temp->AsFloat());
      }
      break;
    case VWGOLF_CHG_TEMP_COOLER:
      if (GPollReply.FromUint8("VWGOLF_CHG_TEMP_COOLER", value)) {
        StdMetrics.ms_v_charge_temp->SetValue(value - 40.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_TEMP_COOLER=%f => %f", value, StdMetrics.ms_v_charge_temp->AsFloat());
      }
      break;
    case VWGOLF_MOT_ELEC_TEMP_AMB:
      if (GPollReply.FromUint8("VWGOLF_MOT_ELEC_TEMP_AMB", value) && value > 0 && value < 255) {
        StdMetrics.ms_v_env_temp->SetValue(value - 40.0f);
        VALUE_LOG(TAG, "VWGOLF_MOT_ELEC_TEMP_AMB=%f => %f", value, StdMetrics.ms_v_env_temp->AsFloat());
      }
      break;

    case VWGOLF_CHG_MGMT_REM:
      // Ignore charge shutdown value of 127 to keep last estimation:
      if (GPollReply.FromUint8("VWGOLF_CHG_MGMT_REM", value) && value != 127) {
        StdMetrics.ms_v_charge_duration_full->SetValue(value * 5.0f);
        VALUE_LOG(TAG, "VWGOLF_CHG_MGMT_REM=%f => %f", value, StdMetrics.ms_v_charge_duration_full->AsFloat());
      }
      break;

    case VWGOLF_BRK_TPMS:
      if (GPollReply.FromUint8("VWGOLF_BRK_TPMS", value, 43)) {
        std::vector<float> tpms_health(4);
        std::vector<short> tpms_alert(4);
        float threshold_warn = MyConfig.GetParamValueFloat("xvg", "tpms_warn", 80);
        float threshold_alert = MyConfig.GetParamValueFloat("xvg", "tpms_alert", 60);
        std::vector<string> tyre_abb = OvmsVehicle::GetTpmsLayout();
        int i;

        for (i = 0; i < 4; i++) {
          // read diffusion:
          GPollReply.FromUint8("VWGOLF_BRK_TPMS", value, 36+i);
          tpms_health[i] = TRUNCPREC(value / 2.55f, 1);
          TPMSDiffusion->SetElemValue(i,value);
          VALUE_LOG(TAG, "VWGOLF_BRK_TPMS Diffusion %s: %f", tyre_abb[i].c_str(), value);

          // read emergency:
          GPollReply.FromUint8("VWGOLF_BRK_TPMS", value, 40+i);
          if (value / 2.55f < tpms_health[i])
            tpms_health[i] = TRUNCPREC(value / 2.55f, 1);
          TPMSEmergency->SetElemValue(i,value);
          VALUE_LOG(TAG, "VWGOLF_BRK_TPMS Emergency %s: %f", tyre_abb[i].c_str(), value);

          // invalid?
          if (tpms_health[i] == 0)
            break;

          // Set alert?
          if (tpms_health[i] <= threshold_alert)
            tpms_alert[i] = 2;
          else if (tpms_health[i] <= threshold_warn)
            tpms_alert[i] = 1;
          else 
            tpms_alert[i] = 0;
        }

        // all wheels valid?
        if (i == 4) {
          StdMetrics.ms_v_tpms_health->SetValue(tpms_health);
          StdMetrics.ms_v_tpms_alert->SetValue(tpms_alert);
        }
      }
      break;

    default:
      VALUE_LOG(TAG, "IncomingGPollReply: unhandled PID %X: %s", pid, GPollReply.GetHexString().c_str());
      break;
  }
}


/**
 * UpdateChargePower: update power & efficiency, calculate energy sum drawn from grid
 *  Called by either the AC or DC charge handler after obtaining the voltage & current.
 */
void OvmsVehicleVWeGolf::UpdateChargePower(float power_kw)
{
  // Accumulate grid energy sum:
  int time_seconds = StdMetrics.ms_v_charge_power->Age();
  if (time_seconds < 60) {
    m_charge_kwh_grid += (double)power_kw * time_seconds / 3600;
    StdMetrics.ms_v_charge_kwh_grid->SetValue(m_charge_kwh_grid);
    StdMetrics.ms_v_charge_kwh_grid_total->SetValue(m_charge_kwh_grid_start + m_charge_kwh_grid);
  }

  // Standard Charge Power and Charge Efficiency calculation as requested by the standard
  StdMetrics.ms_v_charge_power->SetValue(power_kw);
  float efficiency = (power_kw == 0)
                     ? 0
                     : ((StdMetrics.ms_v_bat_power->AsFloat() * -1) / power_kw) * 100;
  StdMetrics.ms_v_charge_efficiency->SetValue(efficiency);
  VALUE_LOG(TAG, "VWGOLF_CHG_EFF_STD=%f", efficiency);
}


/**
 * UpdateChargeCap: calculate normalized & absolute battery capacities during charge,
 *  update CAC & SOH accordingly after charge stop.
 *  Called by IncomingGPollReply() after receiving SOCs & energy/coulomb counts.
 */
void OvmsVehicleVWeGolf::UpdateChargeCap(bool charging)
{
  // Below ~30% normalized SOC difference, values tend to be unstable and far off.
  // Values stabilize beyond ~60% normalized SOC difference.
  // Absolute SOC has currently a value resolution of 0.4% (uint8 / 2.5 from BMS).
  // As the SOC is corrected once in a while during charging (supposedly from voltage
  // feedback), we need to smooth the derived capacities. To get a consistent number of
  // values for a fixed smoothing, we take samples every 2.4% of absolute SOC difference.
  // From 30-100% normalized SOC diff = ~62% absolute SOC that will give us ~26 samples
  // on a full charge. Smoothing is done with a sample count of 15, so the average can
  // adapt to a new level within 1-2 typical charges.

  // IOW: to get a rough capacity estimation, charge at least 30% normalized SOC difference.
  //  To get a good capacity estimation, do at least three charges with each covering 60%
  //  or more normalized SOC difference.

  const int checkpoint_step = 24;         // 2.4% absolute SOC diff
  const int charged_min_valid = 272;      // 27.2% absolute SOC diff = ~30% normalized SOC diff

  static int checkpoint = 9999;
  bool log_data = false, update_caps = false, update_soh = false;

  int   charge_time   = StdMetrics.ms_v_charge_time->AsInt();
  float soc_abs_diff  = BatMgmtSoCAbs->AsFloat() - m_soc_abs_start;
  float soc_norm_diff = StdMetrics.ms_v_bat_soc->AsFloat() - m_soc_norm_start;
  float energy_diff   = StdMetrics.ms_v_bat_energy_recd_total->AsFloat() - m_energy_charged_start;
  float coulomb_diff  = StdMetrics.ms_v_bat_coulomb_recd_total->AsFloat() - m_coulomb_charged_start;

  int charged = soc_abs_diff * 10 + 0.5;  // round to avoid float errors

  if (charging && charged < checkpoint) {
    // charge started:
    checkpoint = 0;
  }
  if (charged >= checkpoint + checkpoint_step) {
    // next checkpoint reached:
    checkpoint += checkpoint_step;
    log_data = true;
    if (charged >= charged_min_valid)
      update_caps = true;
  }
  if (!charging && checkpoint != 9999) {
    // charge stopped:
    checkpoint = 9999;
    if (charged >= charged_min_valid)
      update_soh = true;
  }

  if (log_data)
  {
    // Calculate battery capacities from current coulomb/energy & SOC deltas:
    float cap_ah_abs   = coulomb_diff / soc_abs_diff  * 100;
    float cap_ah_norm  = coulomb_diff / soc_norm_diff * 100;
    float cap_kwh_abs  = energy_diff  / soc_abs_diff  * 100;
    float cap_kwh_norm = energy_diff  / soc_norm_diff * 100;
    
    // Update smoothed battery capacities:
    if (update_caps) {
      m_bat_cap_ah_abs  ->SetValue(m_smooth_cap_ah_abs  .Add(cap_ah_abs  ));
      m_bat_cap_ah_norm ->SetValue(m_smooth_cap_ah_norm .Add(cap_ah_norm ));
      m_bat_cap_kwh_abs ->SetValue(m_smooth_cap_kwh_abs .Add(cap_kwh_abs ));
      m_bat_cap_kwh_norm->SetValue(m_smooth_cap_kwh_norm.Add(cap_kwh_norm));
    }
    
    // Log local:
    ESP_LOGI(TAG, "ChargeCap: charge_time=%ds bat_temp=%.1f°C energy_range=%.2fkWh "
      "soc_norm=%.1f+%.1f%% soc_abs=%.1f+%.1f%% energy+=%.3fkWh coulomb+=%.3fAh "
      "=> cap_ah_norm=%.2f cap_ah_abs=%.2f cap_kwh_norm=%.2f cap_kwh_abs=%.2f",
      charge_time, StdMetrics.ms_v_bat_temp->AsFloat(), m_bat_energy_range->AsFloat(),
      m_soc_norm_start, soc_norm_diff, m_soc_abs_start, soc_abs_diff,
      energy_diff, coulomb_diff, cap_ah_norm, cap_ah_abs, cap_kwh_norm, cap_kwh_abs);
    
    // Log to server:
    int storetime_days = MyConfig.GetParamValueInt("xvg", "log.chargecap.storetime", 0);
    if (storetime_days > 0)
    {
      MyNotify.NotifyStringf("data", "xvg.log.chargecap",
        "XVG-LOG-ChargeCap,1,%d,%d,%.1f,%.2f,%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f",
        storetime_days * 86400,
        charge_time, StdMetrics.ms_v_bat_temp->AsFloat(), m_bat_energy_range->AsFloat(),
        m_soc_norm_start, soc_norm_diff, m_soc_abs_start, soc_abs_diff,
        energy_diff, coulomb_diff, cap_ah_norm, cap_ah_abs, cap_kwh_norm, cap_kwh_abs);
    }
  }

  if (update_soh)
  {
    // Get smoothed capacities:
    float cap_ah_abs   = m_smooth_cap_ah_abs  .Value();
    float cap_ah_norm  = m_smooth_cap_ah_norm .Value();
    float cap_kwh_abs  = m_smooth_cap_kwh_abs .Value();
    float cap_kwh_norm = m_smooth_cap_kwh_norm.Value();

    // For now, calculate SOH directly from CAC:
    // Gen2: 32.3 kWh net / 36.8 kWh gross, 2P84S = 120 Ah, 260 km WLTP
    // Gen1: 16.4 kWh net / 18.7 kWh gross, 2P102S = 50 Ah, 160 km WLTP
    float cac        = cap_ah_abs;
    float soh        = cac * 100 / ((vwegolf_modelyear > 2016) ? 120 :  50);
    float range_full = soh / 100 * ((vwegolf_modelyear > 2016) ? 260 : 160);
    float soc_fct    = StdMetrics.ms_v_bat_soc->AsFloat() / 100;
    
    // Log local:
    ESP_LOGI(TAG, "ChargeCap SOH update: CAC %.2f -> %.2fAh, SOH %.1f -> %.1f%%; "
      "smoothed: cap_ah_norm=%.2f cap_ah_abs=%.2f cap_kwh_norm=%.2f cap_kwh_abs=%.2f",
      StdMetrics.ms_v_bat_cac->AsFloat(), cac,
      StdMetrics.ms_v_bat_soh->AsFloat(), soh,
      cap_ah_norm, cap_ah_abs, cap_kwh_norm, cap_kwh_abs);
    
    // Log to server:
    int storetime_days = MyConfig.GetParamValueInt("xvg", "log.chargecap.storetime", 0);
    if (storetime_days > 0)
    {
      MyNotify.NotifyStringf("data", "xvg.log.chargecap.soh",
        "XVG-LOG-ChargeCapSOH,1,%d,%.2f,%.2f,%.1f,%.1f,%.2f,%.2f,%.2f,%.2f",
        storetime_days * 86400,
        StdMetrics.ms_v_bat_cac->AsFloat(), cac,
        StdMetrics.ms_v_bat_soh->AsFloat(), soh,
        cap_ah_norm, cap_ah_abs, cap_kwh_norm, cap_kwh_abs);
    }

    // Update metrics:
    StdMetrics.ms_v_bat_cac->SetValue(cac);
    StdMetrics.ms_v_bat_soh->SetValue(soh);
    StdMetrics.ms_v_bat_range_full->SetValue(range_full);
    StdMetrics.ms_v_bat_range_ideal->SetValue(range_full * soc_fct);
  }
}


/**
 * UpdateChargeParams: update charge SOC limit and charge mode
 */
void OvmsVehicleVWeGolf::UpdateChargeParams()
{
  bool timermode = StdMetrics.ms_v_charge_timermode->AsBool();
  int soc = StdMetrics.ms_v_bat_soc->AsInt();
  int socmin = m_chg_timer_socmin->AsInt();
  int socmax = m_chg_timer_socmax->AsInt();

  // Set v.c.limit.soc to either min or max SOC, or 100% depending on the state:
  int soclim = 100;
  if (timermode)
  {
    if (soc < socmin)
      soclim = socmin;
    else if (soc < socmax)
      soclim = socmax;
  }
  StdMetrics.ms_v_charge_limit_soc->SetValue(soclim);

  // Derive charge mode from final SOC destination:
  if (soclim == 100 || socmax == 100)
    StdMetrics.ms_v_charge_mode->SetValue("range");
  else
    StdMetrics.ms_v_charge_mode->SetValue("standard");
}
