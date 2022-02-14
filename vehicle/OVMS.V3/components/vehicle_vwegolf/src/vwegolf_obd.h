/*
;    Project:       Open Vehicle Monitor System
;
;    Changes:
;    1.0  Initial release
;
;    (C) 2011       Michael Stegen / Stegen Electronics
;    (C) 2011-2018  Mark Webb-Johnson
;    (C) 2011       Sonny Chen @ EPRO/DX
;    (C) 2020       Chris van der Meijden
;    (C) 2020       Soko
;    (C) 2020       sharkcow <sharkcow@gmx.de>
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

#ifndef __VEHICLE_EGOLF_OBD_H__
#define __VEHICLE_EGOLF_OBD_H__

#include "vehicle.h"
#include "ovms_webserver.h"
#include "g_poll_reply_helper.h"

using namespace std;

//
// ECU TX/RX IDs                                                                          [Availability]
//
#define VWGOLF_MOT_ELEC_TX                0x7E0   // ECU 01 motor elecronics                [DRV]
#define VWGOLF_MOT_ELEC_RX                0x7E8
#define VWGOLF_BRK_TX                     0x713   // ECU 03 brake electronics               [DRV]
#define VWGOLF_BRK_RX                     0x77D
#define VWGOLF_MFD_TX                     0x714   // ECU 17 multi-function display          [DRV]
#define VWGOLF_MFD_RX                     0x77E
#define VWGOLF_BRKBOOST_TX                0x73B   // ECU 23 brake boost                     [DRV]
#define VWGOLF_BRKBOOST_RX                0x743
#define VWGOLF_STEER_TX                   0x712   // ECU 44 steering support                [DRV,CHG]
#define VWGOLF_STEER_RX                   0x71A
#define VWGOLF_ELD_TX                     0x7E6   // ECU 51 electric drive                  [DRV,CHG]
#define VWGOLF_ELD_RX                     0x7EE
#define VWGOLF_INF_TX                     0x773   // ECU 5F information electronics         [DRV]
#define VWGOLF_INF_RX                     0x77B
#define VWGOLF_BAT_MGMT_TX                0x7E5   // ECU 8C hybrid battery management       [DRV,CHG]
#define VWGOLF_BAT_MGMT_RX                0x7ED
#define VWGOLF_BRKSENS_TX                 0x762   // ECU AD sensors for braking system      [DRV]
#define VWGOLF_BRKSENS_RX                 0x76A
#define VWGOLF_CHG_TX                     0x744   // ECU C6 HV charger                      [DRV,CHG]
#define VWGOLF_CHG_RX                     0x7AE
#define VWGOLF_CHG_MGMT_TX                0x765   // ECU BD HV charge management            [DRV,CHG]
#define VWGOLF_CHG_MGMT_RX                0x7CF

//
// Poll list shortcuts                                                                    [Availability]
//
#define VWGOLF_MOT_ELEC                   VWGOLF_MOT_ELEC_TX, VWGOLF_MOT_ELEC_RX            //  [DRV]
#define VWGOLF_BRK                        VWGOLF_BRK_TX,      VWGOLF_BRK_RX                 //  [DRV]
#define VWGOLF_MFD                        VWGOLF_MFD_TX,      VWGOLF_MFD_RX                 //  [DRV]
#define VWGOLF_BRKBOOST                   VWGOLF_BRKBOOST_TX, VWGOLF_BRKBOOST_RX            //  [DRV]
#define VWGOLF_STEER                      VWGOLF_STEER_TX,    VWGOLF_STEER_RX               //  [DRV,CHG]
#define VWGOLF_ELD                        VWGOLF_ELD_TX,      VWGOLF_ELD_RX                 //  [DRV,CHG]
#define VWGOLF_INF                        VWGOLF_INF_TX,      VWGOLF_INF_RX                 //  [DRV]
#define VWGOLF_BAT_MGMT                   VWGOLF_BAT_MGMT_TX, VWGOLF_BAT_MGMT_RX            //  [DRV,CHG]
#define VWGOLF_BRKSENS                    VWGOLF_BRKSENS_TX,  VWGOLF_BRKSENS_RX             //  [DRV]
#define VWGOLF_CHG                        VWGOLF_CHG_TX,      VWGOLF_CHG_RX                 //  [DRV,CHG]
#define VWGOLF_CHG_MGMT                   VWGOLF_CHG_MGMT_TX, VWGOLF_CHG_MGMT_RX            //  [DRV,CHG]

#define UDS_READ                        VEHICLE_POLL_TYPE_READDATA
#define UDS_SESSION                     VEHICLE_POLL_TYPE_OBDIISESSION

//
// ECU diagnostic session(s)
//
#define VWGOLF_EXTDIAG_START              0x03
#define VWGOLF_EXTDIAG_STOP               0x01

//
// PIDs of ECUs
//
#define VWGOLF_MOT_ELEC_SOC_NORM          0x1164
#define VWGOLF_MOT_ELEC_SOC_ABS           0xF45B
#define VWGOLF_MOT_ELEC_TEMP_AMB          0xF446    // Ambient temperature
#define VWGOLF_MOT_ELEC_TEMP_DCDC         0x116F    // DCDC converter temperature current value
#define VWGOLF_MOT_ELEC_TEMP_PEM          0x1116    // pulse inverter temperature current value
#define VWGOLF_MOT_ELEC_TEMP_COOL1        0x1611    // coolant temperature after heat exchanger HV battery current value
#define VWGOLF_MOT_ELEC_TEMP_COOL2        0x1612    // coolant temperature before e-machine current value
#define VWGOLF_MOT_ELEC_TEMP_COOL3        0x1613    // coolant temperature after heat exchanger current value
#define VWGOLF_MOT_ELEC_TEMP_COOL4        0x1614    // coolant temperature before PTC current value
#define VWGOLF_MOT_ELEC_TEMP_COOL5        0x1615    // coolant temperature before power electronics current value
#define VWGOLF_MOT_ELEC_TEMP_COOL_BAT     0x1169    // cooling temperature of hybrid battery
#define VWGOLF_MOT_ELEC_VIN               0xF802    // Vehicle Identification Number
#define VWGOLF_MOT_ELEC_SPEED             0xF40D    // Speed [kph]
#define VWGOLF_MOT_ELEC_POWER_MOT         0x147E    // Inverter output / motor input power level [kW]

#define VWGOLF_BRK_TPMS                   0x1821

#define VWGOLF_MFD_ODOMETER               0x2203
#define VWGOLF_MFD_SERV_RANGE             0x2260
#define VWGOLF_MFD_SERV_TIME              0x2261
//#define VWGOLF_MFD_TEMP_COOL              0x        // coolant temperature
#define VWGOLF_MFD_RANGE_DSP              0x22E0    // Estimated range displayed
#define VWGOLF_MFD_RANGE_CAP              0x22E4    // Usable battery energy [kWh] from range estimation

#define VWGOLF_BRKBOOST_TEMP_ECU          0x028D    // control unit temperature
#define VWGOLF_BRKBOOST_TEMP_ACC          0x4E06    // temperature brake accumulator control unit
//#define VWGOLF_BRKBOOST_TEMP_COOL         0x        // coolant temperature

#define VWGOLF_STEER_TEMP                 0x0295    // temperature power amplifier

#define VWGOLF_ELD_TEMP_COOL              0xF405    // coolant temperature
#define VWGOLF_ELD_TEMP_MOT               0x3E94    // temperature electrical machine
#define VWGOLF_ELD_TEMP_DCDC1             0x3EB5    // DCDC converter temperature current value
#define VWGOLF_ELD_TEMP_DCDC2             0x464E    // DCDC converter PCB temperature
#define VWGOLF_ELD_TEMP_DCDC3             0x464F    // DCDC converter temperature of power electronics
#define VWGOLF_ELD_TEMP_PEW               0x4662    // power electronics IGBT temperature phase W
#define VWGOLF_ELD_TEMP_PEV               0x4663    // power electronics IGBT temperature phase V
#define VWGOLF_ELD_TEMP_PEU               0x4664    // power electronics IGBT temperature phase U
#define VWGOLF_ELD_TEMP_STAT              0x4672    // e-machine stator temperature
#define VWGOLF_ELD_DCDC_U                 0x465C    // DCDC converter output voltage
#define VWGOLF_ELD_DCDC_I                 0x465B    // DCDC converter output current

#define VWGOLF_INF_TEMP_PCB               0x1863    // temperature processor PCB
//#define VWGOLF_INF_TEMP_OPT               0x        // temperature optical drive
//#define VWGOLF_INF_TEMP_MMX               0x        // temperature MMX PCB
#define VWGOLF_INF_TEMP_AUDIO             0x1866    // temperature audio amplifier

#define VWGOLF_BAT_MGMT_U                 0x1E3B
#define VWGOLF_BAT_MGMT_I                 0x1E3D
#define VWGOLF_BAT_MGMT_SOC_ABS           0x028C
#define VWGOLF_BAT_MGMT_ENERGY_COUNTERS   0x1E32
#define VWGOLF_BAT_MGMT_CELL_MAX          0x1E33    // max battery voltage
#define VWGOLF_BAT_MGMT_CELL_MIN          0x1E34    // min battery voltage
#define VWGOLF_BAT_MGMT_TEMP              0x2A0B
#define VWGOLF_BAT_MGMT_TEMP_MAX          0x1E0E    // maximum battery temperature
#define VWGOLF_BAT_MGMT_TEMP_MIN          0x1E0F    // minimum battery temperature
#define VWGOLF_BAT_MGMT_CELL_VBASE        0x1E40    // cell voltage base address
#define VWGOLF_BAT_MGMT_CELL_VLAST        0x1EA5    // cell voltage last address
#define VWGOLF_BAT_MGMT_CELL_TBASE        0x1EAE    // cell temperature base address
#define VWGOLF_BAT_MGMT_CELL_TLAST        0x1EBD    // cell temperature last address
#define VWGOLF_BAT_MGMT_CELL_T17          0x7425    // cell temperature for module #17 (gen1)

#define VWGOLF_BRKSENS_TEMP               0x1024    // sensor temperature

#define VWGOLF_CHG_POWER_EFF              0x15D6
#define VWGOLF_CHG_POWER_LOSS             0x15E1
#define VWGOLF_CHG_TEMP_BRD               0x15D8    // temperature battery charger system board
#define VWGOLF_CHG_TEMP_PFCCOIL           0x15D9    // temperature battery charger PFC coil
#define VWGOLF_CHG_TEMP_DCDCCOIL          0x15DA    // temperature battery charger DCDC coil
#define VWGOLF_CHG_TEMP_COOLER            0x15E2    // charger coolant temperature
#define VWGOLF_CHG_TEMP_DCDCPL            0x15EC    // temperature DCDC powerline
#define VWGOLF1_CHG_AC_U                  0x1DA9
#define VWGOLF1_CHG_AC_I                  0x1DA8
#define VWGOLF1_CHG_DC_U                  0x1DA7
#define VWGOLF1_CHG_DC_I                  0x4237
#define VWGOLF2_CHG_AC_U                  0x41FC
#define VWGOLF2_CHG_AC_I                  0x41FB
#define VWGOLF2_CHG_DC_U                  0x41F8
#define VWGOLF2_CHG_DC_I                  0x41F9

#define VWGOLF_CHG_MGMT_SOC_NORM          0x1DD0
#define VWGOLF_CHG_MGMT_SOC_LIMITS        0x1DD1    // Minimum & maximum SOC
#define VWGOLF_CHG_MGMT_TIMER_DEF         0x1DD4    // Scheduled charge configured
#define VWGOLF_CHG_MGMT_HV_CHGMODE        0x1DD6    // High voltage charge mode
#define VWGOLF_CHG_MGMT_REM               0x1DE4    // remaining time for full charge
#define VWGOLF_CHG_MGMT_LV_PWRSTATE       0x1DEC    // Low voltage (12V) systems power state
#define VWGOLF_CHG_MGMT_LV_AUTOCHG        0x1DED    // Low voltage (12V) auto charge mode
#define VWGOLF_CHG_MGMT_CCS_STATUS        0x1DEF    // CCS charger capabilities, current & voltage, flags

#define VWGOLF_CHG_MGMT_TEST_1DD7         0x1DD7    // Charger calibration?
#define VWGOLF_CHG_MGMT_TEST_1DDA         0x1DDA    // Charge plug status?
#define VWGOLF_CHG_MGMT_TEST_1DE6         0x1DE6    // BMS internal data?

#endif //#ifndef __VEHICLE_EGOLF_OBD_H__
