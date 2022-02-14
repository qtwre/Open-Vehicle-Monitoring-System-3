/*
;    Project:       Open Vehicle Monitor System
;    Date:          5th July 2018
;
;    Changes:
;    1.0  Initial release
;
;    (C) 2011       Michael Stegen / Stegen Electronics
;    (C) 2011-2018  Mark Webb-Johnson
;    (C) 2011       Sonny Chen @ EPRO/DX

;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
*/

/*
;    Subproject:    Integration of support for the VW e-UP
;    Date:          30 December 2020
;
;    Changes:
;    0.1.0  Initial code
:           Crude merge of code from Chris van der Meijden (KCAN) and SokoFromNZ (OBD2)
;
;    0.1.1  make OBD code depend on car status from KCAN, no more OBD polling in off state
;
;    0.1.2  common namespace, metrics webinterface
;
;    0.1.3  bugfixes gen1/gen2, new metrics: temperatures & maintenance
;
;    0.1.4  bugfixes gen1/gen2, OBD refactoring
;
;    0.1.5  refactoring
;
;    0.1.6  Standard SoC now from OBD (more resolution), switch between car on and charging
;
;    0.2.0  refactoring: this is now plain VWEGOLF-module, KCAN-version moved to VWEGOLF_T26
;
;    0.2.1  SoC from OBD/KCAN depending on config & state
;
;    0.3.0  car state determined depending on connection
;
;    0.3.1  added charger standard metrics
;
;    0.3.2  refactoring; init CAN buses depending on connection; transmit charge current as float (server V2)
;
;    0.3.3  new standard metrics for maintenance range & days; added trip distance & energies as deltas
;
;    0.4.0  update changes in t26 code by devmarxx, update docs
;
;    (C) 2020 sharkcow <sharkcow@gmx.de> / Chris van der Meijden / SokoFromNZ
;
;    Biggest thanks to Dexter, Dimitrie78, E-Imo and 'der kleine Nik'.
*/

#include "ovms_log.h"
#include <string>
static const char *TAG = "v-vwegolf";

#define VERSION "0.14.2"

#include <stdio.h>
#include <string>
#include <iomanip>
#include "pcp.h"
#include "ovms_metrics.h"
#include "ovms_events.h"
#include "ovms_config.h"
#include "ovms_command.h"
#include "metrics_standard.h"
#include "ovms_notify.h"

#include "vehicle_vwegolf.h"

using namespace std;

/**
 * Framework registration
 */

class OvmsVehicleVWeGolfInit
{
public:
  OvmsVehicleVWeGolfInit();
} OvmsVehicleVWeGolfInit __attribute__((init_priority(9000)));

OvmsVehicleVWeGolfInit::OvmsVehicleVWeGolfInit()
{
  ESP_LOGI(TAG, "Registering Vehicle: VW e-Golf (9000)");
  MyVehicleFactory.RegisterVehicle<OvmsVehicleVWeGolf>("VWGOLF", "VW e-Golf");
}


OvmsVehicleVWeGolf *OvmsVehicleVWeGolf::GetInstance(OvmsWriter *writer)
{
  OvmsVehicleVWeGolf *egolf = (OvmsVehicleVWeGolf *) MyVehicleFactory.ActiveVehicle();

  string type = StdMetrics.ms_v_type->AsString();
  if (!egolf || type != "VWGOLF") {
    if (writer) {
      writer->puts("Error: VW e-Golf vehicle module not selected");
    }
    return NULL;
  }
  return egolf;
}


/**
 * Constructor & destructor
 */

//size_t OvmsVehicleVWeGolf::m_modifier = 0;

OvmsVehicleVWeGolf::OvmsVehicleVWeGolf()
{
  ESP_LOGI(TAG, "Start VW e-Golf vehicle module");

  // Init general state:
  vwegolf_enable_write = false;
  vwegolf_enable_obd = false;
  vwegolf_enable_t26 = false;
  vwegolf_con = 0;
  vwegolf_modelyear = 0;

  m_use_phase = UP_None;
  m_obd_state = OBDS_Init;

  // Init metrics:
  m_version = MyMetrics.InitString("xvg.m.version", 0, VERSION " " __DATE__ " " __TIME__);

  // init configs:
  MyConfig.RegisterParam("xvg", "VW e-Golf", true, true);

  ConfigChanged(NULL);

#ifdef CONFIG_OVMS_COMP_WEBSERVER
  WebInit();
#endif
}

OvmsVehicleVWeGolf::~OvmsVehicleVWeGolf()
{
  ESP_LOGI(TAG, "Stop VW e-Golf vehicle module");

#ifdef CONFIG_OVMS_COMP_WEBSERVER
  WebDeInit();
#endif

  // delete metrics:
  MyMetrics.DeregisterMetric(m_version);
  // TODO: delete all xvg metrics
}

/*
const char* OvmsVehicleVWeGolf::VehicleShortName()
{
  return "e-Golf";
}
*/

bool OvmsVehicleVWeGolf::SetFeature(int key, const char *value)
{
  int i;
  int n;
  switch (key) {
    case 15: {
      int bits = atoi(value);
      MyConfig.SetParamValueBool("xvg", "canwrite", (bits & 1) != 0);
      return true;
    }
    case 20:
      // check:
      if (strlen(value) == 0) {
        value = "2017";
      }
      for (i = 0; i < strlen(value); i++) {
        if (isdigit(value[i]) == false) {
          value = "2017";
          break;
        }
      }
      n = atoi(value);
      if (n < 2014) {
        value = "2014";
      }
      MyConfig.SetParamValue("xvg", "modelyear", value);
      return true;
    case 21:
      // check:
      if (strlen(value) == 0) {
        value = "22";
      }
      for (i = 0; i < strlen(value); i++) {
        if (isdigit(value[i]) == false) {
          value = "22";
          break;
        }
      }
      n = atoi(value);
      if (n < 15) {
        value = "15";
      }
      if (n > 30) {
        value = "30";
      }
      MyConfig.SetParamValue("xvg", "cc_temp", value);
      return true;
    default:
      return OvmsVehicle::SetFeature(key, value);
  }
}

const std::string OvmsVehicleVWeGolf::GetFeature(int key)
{
  switch (key) {
    case 15: {
      int bits = (MyConfig.GetParamValueBool("xvg", "canwrite", false) ? 1 : 0);
      char buf[4];
      sprintf(buf, "%d", bits);
      return std::string(buf);
    }
    case 20:
      return MyConfig.GetParamValue("xvg", "modelyear", STR(DEFAULT_MODEL_YEAR));
    case 21:
      return MyConfig.GetParamValue("xvg", "cc_temp", STR(21));
    default:
      return OvmsVehicle::GetFeature(key);
  }
}

void OvmsVehicleVWeGolf::ConfigChanged(OvmsConfigParam *param)
{
  if (param && param->GetName() != "xvg") {
    return;
  }

  ESP_LOGD(TAG, "VW e-Golf reload configuration");
  int vwegolf_modelyear_new = MyConfig.GetParamValueInt("xvg", "modelyear", DEFAULT_MODEL_YEAR);
  bool vwegolf_enable_obd_new = MyConfig.GetParamValueBool("xvg", "con_obd", true);
  bool vwegolf_enable_t26_new = MyConfig.GetParamValueBool("xvg", "con_t26", true);
  vwegolf_enable_write = MyConfig.GetParamValueBool("xvg", "canwrite", false);
  vwegolf_cc_temp_int = MyConfig.GetParamValueInt("xvg", "cc_temp", 22);
  int dc_interval = MyConfig.GetParamValueInt("xvg", "dc_interval", 0);
  int cell_interval_drv = MyConfig.GetParamValueInt("xvg", "cell_interval_drv", 15);
  int cell_interval_chg = MyConfig.GetParamValueInt("xvg", "cell_interval_chg", 60);
  int cell_interval_awk = MyConfig.GetParamValueInt("xvg", "cell_interval_awk", 60);

  bool do_obd_init = (
    (!vwegolf_enable_obd && vwegolf_enable_obd_new) ||
    (vwegolf_enable_t26_new != vwegolf_enable_t26) ||
    (vwegolf_modelyear < 2017 && vwegolf_modelyear_new > 2016) ||
    (vwegolf_modelyear_new < 2017 && vwegolf_modelyear > 2016) ||
    (dc_interval != m_cfg_dc_interval) ||
    (cell_interval_drv != m_cfg_cell_interval_drv) ||
    (cell_interval_chg != m_cfg_cell_interval_chg) ||
    (cell_interval_awk != m_cfg_cell_interval_awk));

  vwegolf_modelyear = vwegolf_modelyear_new;
  vwegolf_enable_obd = vwegolf_enable_obd_new;
  vwegolf_enable_t26 = vwegolf_enable_t26_new;
  m_cfg_dc_interval = dc_interval;
  m_cfg_cell_interval_drv = cell_interval_drv;
  m_cfg_cell_interval_chg = cell_interval_chg;
  m_cfg_cell_interval_awk = cell_interval_awk;

  // Connectors:
  vwegolf_con = vwegolf_enable_obd * 2 + vwegolf_enable_t26;
  if (!vwegolf_con) {
    ESP_LOGW(TAG, "Module will not work without any connection!");
  }

  // Set model specific general vehicle properties:
  //  Note: currently using standard specs
  //  TODO: get actual capacity/SOH & max charge current
  float socfactor = StdMetrics.ms_v_bat_soc->AsFloat() / 100;
  float sohfactor = StdMetrics.ms_v_bat_soh->AsFloat() / 100;
  if (sohfactor == 0) sohfactor = 1;
  if (vwegolf_modelyear > 2016)
  {
    // 32.3 kWh net / 36.8 kWh gross, 2P84S = 120 Ah, 260 km WLTP
    if (StdMetrics.ms_v_bat_cac->AsFloat() == 0)
      StdMetrics.ms_v_bat_cac->SetValue(120 * sohfactor);
    StdMetrics.ms_v_bat_range_full->SetValue(260 * sohfactor);
    if (StdMetrics.ms_v_bat_range_ideal->AsFloat() == 0)
      StdMetrics.ms_v_bat_range_ideal->SetValue(260 * sohfactor * socfactor);
    if (StdMetrics.ms_v_bat_range_est->AsFloat() > 10 && StdMetrics.ms_v_bat_soc->AsFloat() > 10)
      m_range_est_factor = StdMetrics.ms_v_bat_range_est->AsFloat() / StdMetrics.ms_v_bat_soc->AsFloat();
    else
      m_range_est_factor = 2.6f;
    StdMetrics.ms_v_charge_climit->SetValue(32);

    // Battery pack layout: 2P84S in 14 modules
    BmsSetCellArrangementVoltage(84, 6);
    BmsSetCellArrangementTemperature(14, 1);
    BmsSetCellLimitsVoltage(2.0, 5.0);
    BmsSetCellLimitsTemperature(-39, 200);
    BmsSetCellDefaultThresholdsVoltage(0.020, 0.030);
    BmsSetCellDefaultThresholdsTemperature(2.0, 3.0);
  }
  else
  {
    // 16.4 kWh net / 18.7 kWh gross, 2P102S = 50 Ah, 160 km WLTP
    if (StdMetrics.ms_v_bat_cac->AsFloat() == 0)
      StdMetrics.ms_v_bat_cac->SetValue(50 * sohfactor);
    StdMetrics.ms_v_bat_range_full->SetValue(160 * sohfactor);
    if (StdMetrics.ms_v_bat_range_ideal->AsFloat() == 0)
      StdMetrics.ms_v_bat_range_ideal->SetValue(160 * sohfactor * socfactor);
    if (StdMetrics.ms_v_bat_range_est->AsFloat() > 10 && StdMetrics.ms_v_bat_soc->AsFloat() > 10)
      m_range_est_factor = StdMetrics.ms_v_bat_range_est->AsFloat() / StdMetrics.ms_v_bat_soc->AsFloat();
    else
      m_range_est_factor = 1.6f;
    StdMetrics.ms_v_charge_climit->SetValue(16);

    // Battery pack layout: 2P102S in 17 modules
    BmsSetCellArrangementVoltage(102, 6);
    BmsSetCellArrangementTemperature(17, 1);
    BmsSetCellLimitsVoltage(2.0, 5.0);
    BmsSetCellLimitsTemperature(-39, 200);
    BmsSetCellDefaultThresholdsVoltage(0.020, 0.030);
    BmsSetCellDefaultThresholdsTemperature(2.0, 3.0);
  }

  // Init OBD subsystem:
  // (needs to be initialized first as the T26 module depends on OBD being ready)
  if (vwegolf_enable_obd && do_obd_init) {
    OBDInit();
  }
  else if (!vwegolf_enable_obd) {
    OBDDeInit();
  }

  // Init T26 subsystem:
  if (vwegolf_enable_t26) {
    T26Init();
  }

#ifdef CONFIG_OVMS_COMP_WEBSERVER
  // Init Web subsystem:
  WebDeInit();    // this can probably be done more elegantly... :-/
  WebInit();
#endif
}


void OvmsVehicleVWeGolf::Ticker1(uint32_t ticker)
{
  if (HasT26()) {
    T26Ticker1(ticker);
  }
}


/**
 * GetNotifyChargeStateDelay: framework hook
 */
int OvmsVehicleVWeGolf::GetNotifyChargeStateDelay(const char *state)
{
  // With OBD data, wait for first voltage & current when starting the charge:
  if (HasOBD() && strcmp(state, "charging") == 0) {
    return 6;
  }
  else {
    return 3;
  }
}


/**
 * SetUsePhase: track phase transitions between charging & driving
 */
void OvmsVehicleVWeGolf::SetUsePhase(use_phase_t use_phase)
{
  if (m_use_phase == use_phase)
    return;

  // Phase transition: reset BMS statistics?
  if (MyConfig.GetParamValueBool("xvg", "bms.autoreset")) {
    ESP_LOGD(TAG, "SetUsePhase %d: resetting BMS statistics", use_phase);
    BmsResetCellStats();
  }

  m_use_phase = use_phase;
}


/**
 * ResetTripCounters: called at trip start to set reference points
 *  Called by the connector subsystem detecting vehicle state changes,
 *  i.e. T26 has priority if available.
 */
void OvmsVehicleVWeGolf::ResetTripCounters()
{
  // Clear per trip counters:
  StdMetrics.ms_v_pos_trip->SetValue(0);
  StdMetrics.ms_v_bat_energy_recd->SetValue(0);
  StdMetrics.ms_v_bat_energy_used->SetValue(0);
  StdMetrics.ms_v_bat_coulomb_recd->SetValue(0);
  StdMetrics.ms_v_bat_coulomb_used->SetValue(0);

  // Get trip start references as far as available:
  //  (if we don't have them yet, IncomingGPollReply() will set them ASAP)
  if (IsOBDReady()) {
    m_soc_abs_start       = BatMgmtSoCAbs->AsFloat();
  }
  m_odo_start           = StdMetrics.ms_v_pos_odometer->AsFloat();
  m_soc_norm_start      = StdMetrics.ms_v_bat_soc->AsFloat();
  m_energy_recd_start   = StdMetrics.ms_v_bat_energy_recd_total->AsFloat();
  m_energy_used_start   = StdMetrics.ms_v_bat_energy_used_total->AsFloat();
  m_coulomb_recd_start  = StdMetrics.ms_v_bat_coulomb_recd_total->AsFloat();
  m_coulomb_used_start  = StdMetrics.ms_v_bat_coulomb_used_total->AsFloat();

  ESP_LOGD(TAG, "Trip start ref: socnrm=%f socabs=%f odo=%f, er=%f, eu=%f, cr=%f, cu=%f",
    m_soc_norm_start, m_soc_abs_start, m_odo_start,
    m_energy_recd_start, m_energy_used_start, m_coulomb_recd_start, m_coulomb_used_start);
}


/**
 * ResetChargeCounters: call at charge start to set reference points
 *  Called by the connector subsystem detecting vehicle state changes,
 *  i.e. T26 has priority if available.
 */
void OvmsVehicleVWeGolf::ResetChargeCounters()
{
  // Clear per charge counter:
  StdMetrics.ms_v_charge_kwh->SetValue(0);
  StdMetrics.ms_v_charge_kwh_grid->SetValue(0);
  m_charge_kwh_grid = 0;

  // Get charge start reference as far as available:
  //  (if we don't have it yet, IncomingGPollReply() will set it ASAP)
  if (IsOBDReady()) {
    m_soc_abs_start         = BatMgmtSoCAbs->AsFloat();
  }
  m_soc_norm_start        = StdMetrics.ms_v_bat_soc->AsFloat();
  m_energy_charged_start  = StdMetrics.ms_v_bat_energy_recd_total->AsFloat();
  m_coulomb_charged_start = StdMetrics.ms_v_bat_coulomb_recd_total->AsFloat();
  m_charge_kwh_grid_start = StdMetrics.ms_v_charge_kwh_grid_total->AsFloat();

  ESP_LOGD(TAG, "Charge start ref: socnrm=%f socabs=%f cr=%f er=%f gr=%f",
    m_soc_norm_start, m_soc_abs_start, m_coulomb_charged_start,
    m_energy_charged_start, m_charge_kwh_grid_start);
}


/**
 * SetChargeType: set current internal & public charge type (AC / DC / None)
 *  Controlled by the OBD handler if enabled (derived from VWGOLF_CHG_MGMT_HV_CHGMODE).
 *  The charge type defines the source for the charge metrics, to query the type
 *  use IsChargeModeAC() and IsChargeModeDC().
 */
void OvmsVehicleVWeGolf::SetChargeType(chg_type_t chgtype)
{
  if (m_chg_type == chgtype)
    return;

  m_chg_type = chgtype;

  if (m_chg_type == CHGTYPE_AC) {
    StdMetrics.ms_v_charge_type->SetValue("type2");
  }
  else if (m_chg_type == CHGTYPE_DC) {
    StdMetrics.ms_v_charge_type->SetValue("ccs");
  }
  else {
    StdMetrics.ms_v_charge_type->SetValue("");
    // …and clear/reset charge metrics:
    if (IsOBDReady()) {
      ChargerPowerEffEcu->SetValue(100);
      ChargerPowerLossEcu->SetValue(0);
      ChargerPowerEffCalc->SetValue(100);
      ChargerPowerLossCalc->SetValue(0);
      ChargerAC1U->SetValue(0);
      ChargerAC1I->SetValue(0);
      ChargerAC2U->SetValue(0);
      ChargerAC2I->SetValue(0);
      ChargerACPower->SetValue(0);
      ChargerDC1U->SetValue(0);
      ChargerDC1I->SetValue(0);
      ChargerDC2U->SetValue(0);
      ChargerDC2I->SetValue(0);
      ChargerDCPower->SetValue(0);
      m_chg_ccs_voltage->SetValue(0);
      m_chg_ccs_current->SetValue(0);
      m_chg_ccs_power->SetValue(0);
    }
    StdMetrics.ms_v_charge_voltage->SetValue(0);
    StdMetrics.ms_v_charge_current->SetValue(0);
    StdMetrics.ms_v_charge_power->SetValue(0);
    StdMetrics.ms_v_charge_efficiency->SetValue(0);
  }
}


/**
 * SetChargeState: set v.c.charging, v.c.state and v.c.substate according to current
 *  charge timer mode, limits and SOC
 *  Note: changing v.c.state triggers the notification, so this should be called last.
 */
void OvmsVehicleVWeGolf::SetChargeState(bool charging)
{
  if (charging)
  {
    // Charge in progress:
    StdMetrics.ms_v_charge_inprogress->SetValue(true);

    if (StdMetrics.ms_v_charge_timermode->AsBool())
      StdMetrics.ms_v_charge_substate->SetValue("scheduledstart");
    else
      StdMetrics.ms_v_charge_substate->SetValue("onrequest");

    StdMetrics.ms_v_charge_state->SetValue("charging");
  }
  else
  {
    // Charge stopped:
    StdMetrics.ms_v_charge_inprogress->SetValue(false);

    int soc = StdMetrics.ms_v_bat_soc->AsInt();

    if (IsOBDReady() && StdMetrics.ms_v_charge_timermode->AsBool())
    {
      // Scheduled charge;
      int socmin = m_chg_timer_socmin->AsInt();
      int socmax = m_chg_timer_socmax->AsInt();
      // if stopped at maximum SOC, we've finished as scheduled:
      if (soc >= socmax-1 && soc <= socmax+1) {
        StdMetrics.ms_v_charge_substate->SetValue("scheduledstop");
        StdMetrics.ms_v_charge_state->SetValue("done");
      }
      // …if stopped at minimum SOC, we're waiting for the second phase:
      else if (soc >= socmin-1 && soc <= socmin+1) {
        StdMetrics.ms_v_charge_substate->SetValue("timerwait");
        StdMetrics.ms_v_charge_state->SetValue("stopped");
      }
      // …else the charge has been interrupted:
      else {
        StdMetrics.ms_v_charge_substate->SetValue("interrupted");
        StdMetrics.ms_v_charge_state->SetValue("stopped");
      }
    }
    else
    {
      // Unscheduled charge; done if fully charged:
      if (soc >= 99) {
        StdMetrics.ms_v_charge_substate->SetValue("stopped");
        StdMetrics.ms_v_charge_state->SetValue("done");
      }
      // …else the charge has been interrupted:
      else {
        StdMetrics.ms_v_charge_substate->SetValue("interrupted");
        StdMetrics.ms_v_charge_state->SetValue("stopped");
      }
    }
  }
}
