/* Table-driven cycle sequencer (PRD section 6, FW-01..FW-03). One row per step, the same rows
 * as the PRD section 4 valve truth table. */
#ifndef CYCLE_H
#define CYCLE_H
#include <stdint.h>
#include <stdbool.h>
#include "recipes.h"
#include "alarms.h"

typedef enum { EX_TIME, EX_TANK_LE, EX_TANK_GE, EX_ADDED, EX_DRAWN, EX_PBUS_LE, EX_LIQUID, EX_READY } exit_kind_t;
typedef enum { T_CONST, T_CLEAN_CONC, T_CLEAN_WATER, T_STER_CONC, T_STER_WATER, T_DI_FILL, T_BD_FILL, T_HALF_START } target_t;

#define SF_TANK      0x0001u   /* step may open V5                          */
#define SF_PULSE_V2  0x0002u   /* slow fill: pulse V2 near the target       */
#define SF_DOSE      0x0004u   /* dosing pump step: S7 check, stall check   */
#define SF_TCV_DRAW  0x0008u   /* capture W1 at step start                  */
#define SF_TCV_EVAL  0x0010u   /* capture W2 and evaluate TCV at step end   */
#define SF_LEAK_HOLD 0x0020u   /* record P0 at start, evaluate loss at end  */
#define SF_GERMICIDE 0x0040u   /* germicide now enters the dialyser/lines   */
#define SF_RINSE     0x0080u   /* water rinse: V5 forbidden (SAF-02)        */
#define SF_SELFCHECK 0x0100u
#define SF_PBUS      0x0200u   /* step relies on S2                         */
#define SF_SEAL_HOLD 0x0400u   /* service seal test: P0 after settle, record loss (value = phase) */

typedef struct {
    const char *name;          /* <= 20 chars */
    uint8_t  stage;            /* 1..9 as PRD section 3 */
    uint16_t valves;
    uint8_t  pump;
    uint8_t  exit;             /* exit_kind_t */
    uint8_t  target;           /* target_t    */
    int16_t  value;            /* constant target: g, mmHg, or ms for EX_TIME */
    uint32_t max_ms;
    uint8_t  timeout_alarm;    /* alarm_t raised if max_ms is reached */
    uint16_t flags;
} step_t;

typedef enum {
    CY_IDLE = 0,
    CY_RUN,           /* executing step table                       */
    CY_RECOVER,       /* fault recovery (vent or drain)             */
    CY_HOLD,          /* stage 9: germicide hold, vent every 10 s   */
    CY_DISINF_HOLD,   /* machine full of germicide, >= 6 h          */
    CY_STRIP,         /* rinse-out done, operator confirms test strip */
    CY_DONE           /* finished or aborted, waiting for acknowledge */
} cy_state_t;

typedef struct {
    program_t prog;
    uint8_t   dtype;
    uint16_t  prime_ml;
    uint32_t  dlz_id;
} cycle_req_t;

typedef struct {
    cy_state_t state;
    program_t  prog;
    uint8_t    stage;
    const char *step_name;
    uint32_t   step_elapsed_ms, step_max_ms;
    uint32_t   cycle_elapsed_s;
    bool       first_use;
    bool       tcv_done, tcv_ok, leak_done, leak_ok, pass, aborted;
    uint16_t   tcv_x10, baseline_x10, pct_x10;
    int16_t    leak_x10;
    alarm_t    alarm;
    uint32_t   dlz_id;
    uint8_t    dtype;
    uint8_t    uses;
    uint8_t    seal_n;                     /* seal test: phases measured            */
    int16_t    seal_x10[3];                /* seal test: loss per phase, mmHg x10   */
    uint16_t   tmo_pct;                    /* time-limit scale in force for this run */
} cycle_view_t;

/* Step telemetry (v1.1): one row per executed step of the current / last run. */
enum { END_EXIT = 0, END_TIME, END_TIMEOUT, END_ABORT };
typedef struct {
    const char *name;
    uint8_t  stage, end;
    uint32_t ms;
    int32_t  w0_x10, w1_x10;               /* tank weight at start / end, g x10     */
    int32_t  pmin_x10, pmax_x10;           /* S2 bus, mmHg x10                      */
    int32_t  sup_avg_x10;                  /* S3 supply, psig x10                   */
    int32_t  flow_avg_x10;                 /* S4 inlet flow, L/min x10              */
    uint16_t valves;
    int16_t  tmax_x10;                     /* v1.2: highest driver temperature, degC x10 (-999 = no probe) */
} step_rec_t;
typedef void (*step_hook_t)(const step_rec_t *r, uint32_t index);
void cycle_set_step_hook(step_hook_t h);
const step_rec_t *cycle_step_log(uint32_t *count);
const char *step_end_name(uint8_t end);
uint32_t cycle_disinfect_hold_left_s(void);  /* SAN-01: time left of the 6 h germicide hold, 0 = done/unknown */

bool  cycle_validate_tables(char *why, uint32_t why_len);   /* static safety checks of all step tables */
void  cycle_init(void);
alarm_t cycle_start(const cycle_req_t *req);                /* ALM_NONE = started */
void  cycle_stop(void);                                     /* STOP key */
void  cycle_enter(void);                                    /* ENTER: finish hold / acknowledge */
void  cycle_strip(bool negative);                           /* rinse-out test strip result */
void  cycle_tick(uint32_t now);                             /* every 10 ms */
const cycle_view_t *cycle_view(void);
bool  cycle_busy(void);                                     /* true unless IDLE */

/* table access for tests and the PRD cross-check tool */
const step_t *cycle_program_steps(program_t p, uint32_t *count);

#endif
