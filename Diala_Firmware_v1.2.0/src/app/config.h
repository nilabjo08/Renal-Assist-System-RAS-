/* Diala main controller firmware - build-time configuration.
 * Values marked [verify] are PRD engineering proposals to be confirmed on the bench. */
#ifndef CONFIG_H
#define CONFIG_H

#define FW_NAME            "DIALA"
#define FW_VERSION_STR     "1.2.0"
#define FW_VERSION_NUM     0x0120u
#define HW_TARGET_STR      "main PCB RC4"

/* ---- Scheduler ---- */
#define TICK_SEQ_MS          10u     /* sequencer + safety checks          */
#define TICK_KEYS_MS         10u
#define TICK_UI_MS           100u
#define WDG_KICK_MS          100u    /* TPS3823 times out after 1.12 s min */

/* ---- Valve outputs: bit n = V(n+1). Same numbering as PRD section 4. ---- */
#define V1  (1u << 0)   /* water -> venturi (vacuum + drain) */
#define V2  (1u << 1)   /* water -> bus                      */
#define V3  (1u << 2)   /* bus <-> A (blood, bottom)         */
#define V4  (1u << 3)   /* bus -> V (blood, top)             */
#define V5  (1u << 4)   /* bus <-> mix tank bottom           */
#define V6  (1u << 5)   /* A -> suction bus                  */
#define V7  (1u << 6)   /* V -> suction bus                  */
#define V8  (1u << 7)   /* DO -> suction bus                 */
#define V9  (1u << 8)   /* bus <-> DI                        */
#define V10 (1u << 9)   /* filtered air -> V                 */
#define VALVE_COUNT      10u
#define VALVE_ALL_MASK   0x03FFu
#define SPARE_OUT        (1u << 10)   /* J20 spare output (U11 OUT3) */

/* ---- Process limits (PRD sections 3 and 6) ---- */
#define SUPPLY_MIN_PSIG_X10        150     /* 15.0 psig                       */
#define SUPPLY_LOW_ALARM_MS        3000u   /* low for 3 s -> alarm            */
#define TANK_EMPTY_G               5
#define TANK_OVERFILL_G            1200
#define CONNECTOR_VAC_MMHG         (-100)
#define LEAK_VAC_MMHG              (-250)
#define LEAK_HOLD_MS               20000u  /* measurement window              */
#define LEAK_SETTLE_MS             1000u   /* isolate, settle, then take P0   */
#define LEAK_LIMIT_LOWMID_X10      170     /* 17.0 mmHg loss                  */
#define LEAK_LIMIT_HIGH_X10        250     /* 25.0 mmHg loss                  */
#define TCV_PASS_PCT_X10           800     /* 80.0 % of first-use baseline    */
#define TCV_FILL_TANK_G            400
#define CLEAN_END_LEFT_G           10
#define DILUTE_SLOW_BAND_G         30      /* pulse V2 in the last 30 g [verify] */
#define VENT_PERIOD_MS             10000u  /* PRC-03                          */
#define VENT_PULSE_MS              500u
#define DOSE_STALL_MS              10000u  /* pump must add >= 1 g per 10 s   */
#define DISINFECT_HOLD_H           6       /* SAN-01                          */
#define DISINFECT_LEFT_G           200     /* stop distributing at 200 g left [verify] */

/* ---- Sensors ---- */
#define ADC_VREF_MV         3300
#define S2_KPA_MIN          (-100)  /* bus transducer range [verify part, manager sign-off] */
#define S2_KPA_MAX          200
#define S3_KPA_MIN          0       /* supply transducer 0-1.2 MPa */
#define S3_KPA_MAX          1200
#define FLOW_HZ_PER_LPM_X10 75      /* 1/2" hall sensor: F = 7.5 x Q [verify] */
#define V24_MIN_MV          21000
#define V24_MAX_MV          27500
#define V5_MIN_MV           4600
#define V5_MAX_MV           5400
#define VRAIL_ON_MIN_MV     20000
#define VRAIL_SETTLE_MS     100u

/* ---- Commissioning / bench (v1.1) ---- */
#define TMO_PCT_DEFAULT       100     /* step time-limit scale, condition steps only  */
#define TMO_PCT_MIN           100
#define TMO_PCT_MAX           300
#define SEAL_EVAC_MMHG        (-250)  /* valve seal test target vacuum                */
#define SEAL_HOLD_MS          20000u  /* each seal-test hold window                   */
#define SEAL_LIMIT_X10        30      /* 3.0 mmHg loss per window [verify on bench]   */
#define PUMP_OPEN_MA          15      /* pump on but below this for 1 s = open circuit [verify] */
#define PUMP_CHECK_MS         1000u
#define DISINFECT_HOLD_S      (DISINFECT_HOLD_H * 3600u)
#define DRV_ENERGISE_CODE     0x09u   /* DRV8860 C4:C1 = 1001: 110 ms at 100 % before hold PWM */
#define LCD_REFRESH_MS        30000u  /* full LCD re-init against ribbon-cable noise   */
#define POST_TIMEOUT_MS       2500u
#define STEP_LOG_MAX          40u     /* step telemetry rows kept for the last run     */

/* ---- Service ---- */
#define SERVICE_PIN_DEFAULT     2580u
#define SERVICE_VALVE_MAX_MS    10000u

#endif
