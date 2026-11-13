#include "cycle.h"
#include "config.h"
#include "outputs.h"
#include "sensors.h"
#include "store.h"
#include "util.h"
#include "events.h"
#include "kittest.h"
#include "post.h"
#include "../bsp/bsp.h"
#include <string.h>

/* ------------------------------------------------------------------------------------------
 * Step tables. Valve sets, exits and limits are PRD v0.1 section 4 "Valve open-state truth
 * table" and section 3 stage table. tools/check_cycle_table.py cross-checks them.
 * ------------------------------------------------------------------------------------------ */
#define S(ms) ((uint32_t)(ms))

static const step_t full_steps[] = {
 /* name                   stg valves                    pmp exit        target         value  max_ms     timeout alarm    flags */
 { "SELF-CHECK",            1, 0,                         0, EX_READY,   T_CONST,          0, S(10000), ALM_SENSOR,     SF_SELFCHECK },
 { "CONNECTOR CHECK",       2, V1|V3|V4|V6|V7|V9,         0, EX_PBUS_LE, T_CONST, CONNECTOR_VAC_MMHG, S(20000), ALM_VACUUM, SF_PBUS },
 { "EMPTY TANK",            3, V1|V3|V5|V6,               0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(120000), ALM_TIMEOUT, SF_TANK },
 { "DOSE CLEANER",          3, 0,                         1, EX_ADDED,   T_CLEAN_CONC,     0, S(60000),  ALM_ADD_CHEM,   SF_DOSE },
 { "DILUTE CLEANER",        3, V2|V5,                     0, EX_ADDED,   T_CLEAN_WATER,    0, S(45000),  ALM_TIMEOUT,    SF_TANK|SF_PULSE_V2 },
 { "CLEAN MEMBRANE",        4, V1|V5|V9|V6|V7,            0, EX_DRAWN,   T_HALF_START,     0, S(60000),  ALM_TIMEOUT,    SF_TANK },
 { "CLEAN TOP CONNECTOR",   4, V1|V5|V9|V7,               0, EX_TANK_LE, T_CONST, CLEAN_END_LEFT_G, S(60000), ALM_TIMEOUT, SF_TANK },
 { "RINSE BLOOD TOP>BOT",   5, V2|V4|V6,                  0, EX_TIME,    T_CONST,          0, S(30000),  ALM_NONE,       SF_RINSE },
 { "RINSE BLOOD BOT>TOP",   5, V2|V3|V7,                  0, EX_TIME,    T_CONST,          0, S(30000),  ALM_NONE,       SF_RINSE },
 { "RINSE DIALYSATE",       5, V2|V9|V8,                  0, EX_TIME,    T_CONST,          0, S(30000),  ALM_NONE,       SF_RINSE },
 /* ---- pre-clean program ends here ---- */
 { "FILL TANK FOR TCV",     6, V2|V5,                     0, EX_TANK_GE, T_CONST, TCV_FILL_TANK_G, S(45000), ALM_TIMEOUT, SF_TANK },
 { "EMPTY BLOOD SIDE",      6, V1|V10|V6,                 0, EX_TIME,    T_CONST,          0, S(20000),  ALM_NONE,       0 },
 { "TCV DRAW",              6, V1|V5|V3|V7,               0, EX_LIQUID,  T_CONST,          0, S(60000),  ALM_TIMEOUT,    SF_TANK|SF_TCV_DRAW },
 { "TCV SETTLE",            6, 0,                         0, EX_TIME,    T_CONST,          0, S(1500),   ALM_NONE,       SF_TCV_EVAL },
 { "EMPTY TANK",            6, V1|V3|V5|V6,               0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(120000), ALM_TIMEOUT, SF_TANK },
 { "LEAK: EVACUATE",        7, V1|V9|V8|V10,              0, EX_PBUS_LE, T_CONST, LEAK_VAC_MMHG, S(30000), ALM_LEAK_FAIL, SF_PBUS },
 { "LEAK: HOLD",            7, V9|V10,                    0, EX_TIME,    T_CONST,          0, S(LEAK_SETTLE_MS + LEAK_HOLD_MS), ALM_NONE, SF_PBUS|SF_LEAK_HOLD },
 { "DOSE GERMICIDE",        8, 0,                         1, EX_ADDED,   T_STER_CONC,      0, S(60000),  ALM_ADD_CHEM,   SF_DOSE },
 { "DILUTE GERMICIDE",      8, V2|V5,                     0, EX_ADDED,   T_STER_WATER,     0, S(45000),  ALM_TIMEOUT,    SF_TANK|SF_PULSE_V2 },
 { "GERMICIDE>DIALYSATE",   8, V1|V5|V9|V8|V7,            0, EX_DRAWN,   T_DI_FILL,        0, S(60000),  ALM_TIMEOUT,    SF_TANK|SF_GERMICIDE },
 { "GERMICIDE>BLOOD",       8, V1|V5|V3|V8|V7,            0, EX_DRAWN,   T_BD_FILL,        0, S(60000),  ALM_TIMEOUT,    SF_TANK|SF_GERMICIDE },
};
#define PRECLEAN_STEPS 10u

/* Daily disinfection (SAN-01): bypass connector fitted instead of a dialyser. */
static const step_t disinfect_steps[] = {
 { "SELF-CHECK",            1, 0,                         0, EX_READY,   T_CONST,          0, S(10000),  ALM_SENSOR,     SF_SELFCHECK },
 { "EMPTY TANK",            2, V1|V3|V5|V6,               0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(120000), ALM_TIMEOUT, SF_TANK },
 { "DOSE GERMICIDE",        2, 0,                         1, EX_ADDED,   T_STER_CONC,      0, S(90000),  ALM_ADD_CHEM,   SF_DOSE },
 { "DILUTE GERMICIDE",      2, V2|V5,                     0, EX_ADDED,   T_STER_WATER,     0, S(90000),  ALM_TIMEOUT,    SF_TANK|SF_PULSE_V2 },
 { "FILL A PATH",           3, V1|V5|V3|V6,               0, EX_DRAWN,   T_CONST,        150, S(60000),  ALM_TIMEOUT,    SF_TANK|SF_GERMICIDE },
 { "FILL V PATH",           3, V1|V5|V4|V7,               0, EX_DRAWN,   T_CONST,        150, S(60000),  ALM_TIMEOUT,    SF_TANK|SF_GERMICIDE },
 { "FILL DIALYSATE PATH",   3, V1|V5|V9|V8,               0, EX_DRAWN,   T_CONST,        150, S(60000),  ALM_TIMEOUT,    SF_TANK|SF_GERMICIDE },
 { "FILL CROSS PATH",       3, V1|V5|V3|V7,               0, EX_DRAWN,   T_CONST,        100, S(60000),  ALM_TIMEOUT,    SF_TANK|SF_GERMICIDE },
};

/* Rinse-out after disinfection (SAN-03). */
static const step_t rinseout_steps[] = {
 { "SELF-CHECK",            1, 0,                         0, EX_READY,   T_CONST,          0, S(10000),  ALM_SENSOR,     SF_SELFCHECK },
 { "DRAIN GERMICIDE",       2, V1|V3|V5|V6,               0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(180000), ALM_TIMEOUT, SF_TANK },
 { "FILL TANK WATER",       3, V2|V5,                     0, EX_TANK_GE, T_CONST,        500, S(60000),  ALM_TIMEOUT,    SF_TANK },
 { "FLUSH V PATH",          3, V1|V5|V4|V7,               0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(120000), ALM_TIMEOUT, SF_TANK },
 { "FILL TANK WATER",       3, V2|V5,                     0, EX_TANK_GE, T_CONST,        500, S(60000),  ALM_TIMEOUT,    SF_TANK },
 { "FLUSH DIALYSATE PATH",  3, V1|V5|V9|V8,               0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(120000), ALM_TIMEOUT, SF_TANK },
 { "RINSE A>V",             4, V2|V3|V7,                  0, EX_TIME,    T_CONST,          0, S(60000),  ALM_NONE,       SF_RINSE },
 { "RINSE V>A",             4, V2|V4|V6,                  0, EX_TIME,    T_CONST,          0, S(60000),  ALM_NONE,       SF_RINSE },
 { "RINSE DIALYSATE",       4, V2|V9|V8,                  0, EX_TIME,    T_CONST,          0, S(60000),  ALM_NONE,       SF_RINSE },
};

/* ---- Service programs (v1.1). Same sequencer, same interlocks; started from the service menu. ---- */
static const step_t svc_drain_steps[] = {
 { "SELF-CHECK",            1, 0,                         0, EX_READY,   T_CONST,          0, S(10000),  ALM_SENSOR,     SF_SELFCHECK },
 { "DRAIN TANK",            2, V1|V3|V5|V6,               0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(120000), ALM_TIMEOUT, SF_TANK },
};
static const step_t svc_fill_steps[] = {
 { "SELF-CHECK",            1, 0,                         0, EX_READY,   T_CONST,          0, S(10000),  ALM_SENSOR,     SF_SELFCHECK },
 { "FILL TANK 400 g",       2, V2|V5,                     0, EX_TANK_GE, T_CONST, TCV_FILL_TANK_G, S(45000), ALM_TIMEOUT, SF_TANK },
};
static const step_t svc_prime_steps[] = {   /* dialyser or bypass connector must be fitted */
 { "SELF-CHECK",            1, 0,                         0, EX_READY,   T_CONST,          0, S(10000),  ALM_SENSOR,     SF_SELFCHECK },
 { "PRIME BLOOD TOP>BOT",   2, V2|V4|V6,                  0, EX_TIME,    T_CONST,          0, S(30000),  ALM_NONE,       SF_RINSE },
 { "PRIME BLOOD BOT>TOP",   2, V2|V3|V7,                  0, EX_TIME,    T_CONST,          0, S(30000),  ALM_NONE,       SF_RINSE },
 { "PRIME DIALYSATE",       2, V2|V9|V8,                  0, EX_TIME,    T_CONST,          0, S(30000),  ALM_NONE,       SF_RINSE },
};
/* Seal test: A, V, DI and DO ports capped. Evacuate bus + A + V + DI lines, then trap them and watch
 * the vacuum in three windows, each closing off more of the circuit (Cycle Flow Walkthrough, open point 1):
 *   phase 1  V3 V4 V9 open: stresses V2, V5, V6, V7, V10
 *   phase 2  V3 V9 open:    V line excluded -> V7 and V10 no longer counted
 *   phase 3  V9 open:       bus only -> V2 and V5 (and the DI cap)                                     */
static const step_t svc_seal_steps[] = {
 { "SELF-CHECK",            1, 0,                         0, EX_READY,   T_CONST,          0, S(10000),  ALM_SENSOR,     SF_SELFCHECK },
 { "SEAL: EVACUATE",        2, V1|V3|V4|V6|V7|V9,         0, EX_PBUS_LE, T_CONST, SEAL_EVAC_MMHG, S(30000), ALM_VACUUM, SF_PBUS },
 { "SEAL 1: ALL LINES",     3, V3|V4|V9,                  0, EX_TIME,    T_CONST,          0, S(LEAK_SETTLE_MS + SEAL_HOLD_MS), ALM_NONE, SF_PBUS|SF_SEAL_HOLD },
 { "SEAL 2: NO V LINE",     3, V3|V9,                     0, EX_TIME,    T_CONST,          1, S(LEAK_SETTLE_MS + SEAL_HOLD_MS), ALM_NONE, SF_PBUS|SF_SEAL_HOLD },
 { "SEAL 3: BUS ONLY",      3, V9,                        0, EX_TIME,    T_CONST,          2, S(LEAK_SETTLE_MS + SEAL_HOLD_MS), ALM_NONE, SF_PBUS|SF_SEAL_HOLD },
};

static const step_t vent_step  = { "VENTING",    0, V5|V9|V10, 0, EX_TIME,    T_CONST, 0,            S(2000),   ALM_NONE, SF_TANK };
static const step_t drain_step = { "DRAINING",   0, V1|V3|V5|V6, 0, EX_TANK_LE, T_CONST, TANK_EMPTY_G, S(120000), ALM_NONE, SF_TANK };

const step_t *cycle_program_steps(program_t p, uint32_t *count)
{
    switch (p) {
    case PROG_LOW: case PROG_MID: case PROG_HIGH:
        *count = sizeof full_steps / sizeof full_steps[0]; return full_steps;
    case PROG_PRECLEAN:  *count = PRECLEAN_STEPS; return full_steps;
    case PROG_DISINFECT: *count = sizeof disinfect_steps / sizeof disinfect_steps[0]; return disinfect_steps;
    case PROG_RINSEOUT:  *count = sizeof rinseout_steps / sizeof rinseout_steps[0]; return rinseout_steps;
    case PROG_SVC_DRAIN: *count = sizeof svc_drain_steps / sizeof svc_drain_steps[0]; return svc_drain_steps;
    case PROG_SVC_FILL:  *count = sizeof svc_fill_steps / sizeof svc_fill_steps[0]; return svc_fill_steps;
    case PROG_SVC_PRIME: *count = sizeof svc_prime_steps / sizeof svc_prime_steps[0]; return svc_prime_steps;
    case PROG_SVC_SEAL:  *count = sizeof svc_seal_steps / sizeof svc_seal_steps[0]; return svc_seal_steps;
    default: *count = 0; return NULL;
    }
}

/* Static safety checks, run at boot and by the host tests. */
bool cycle_validate_tables(char *why, uint32_t why_len)
{
    sb_t sb;
    sb_init(&sb, why, why_len);
    for (uint32_t p = 0; p < PROG_COUNT; p++) {
        uint32_t n;
        const step_t *t = cycle_program_steps((program_t)p, &n);
        if (!t || n == 0u) { sb_str(&sb, "empty program"); return false; }
        for (uint32_t i = 0; i < n; i++) {
            const step_t *s = &t[i];
            bool bad = false;
            if (s->max_ms == 0u) bad = true;                                    /* FW-02: every step has a max time */
            if ((s->valves & V5) && !(s->flags & SF_TANK)) bad = true;          /* tank only where declared         */
            if ((s->flags & SF_RINSE) && (s->valves & V5)) bad = true;          /* SAF-02                           */
            if ((s->flags & SF_RINSE) && (s->flags & SF_GERMICIDE)) bad = true;
            if (s->valves & (uint16_t)~VALVE_ALL_MASK) bad = true;
            if ((s->valves & V1) && (s->valves & V2)) bad = true;               /* never venturi and bus fill together */
            if (s->exit != EX_TIME && s->timeout_alarm == ALM_NONE) bad = true; /* condition steps must alarm */
            if ((s->flags & (SF_SEAL_HOLD | SF_LEAK_HOLD)) && (s->exit != EX_TIME || !(s->flags & SF_PBUS))) bad = true;
            if ((s->flags & SF_SEAL_HOLD) && (s->value < 0 || s->value > 2)) bad = true;
            if (s->name == NULL || s->name[0] == '\0') bad = true;
            if (bad) { sb_str(&sb, program_name((program_t)p)); sb_str(&sb, ": "); sb_str(&sb, s->name ? s->name : "?"); return false; }
        }
    }
    return true;
}

/* ------------------------------------------------------------------------------------------ */

static cycle_view_t v;
static cycle_req_t req;
static const recipe_t *rc;
static const step_t *steps;
static uint32_t nsteps, idx;
static const step_t *cur;          /* current step (table row or recovery step) */
static uint32_t step_t0, cycle_t0;
static int32_t w_start, w1, p0;
static bool p0_taken;
static uint32_t supply_low_t; static bool supply_low;
static uint32_t dose_t; static int32_t dose_w;
static uint32_t sensor_bad_t; static bool sensor_bad;
static uint8_t liquid_cnt;
static uint8_t press_cnt;
static uint32_t vent_t; static bool vent_on;
static uint32_t removed_t; static bool removed;
static uint16_t cur_mask;
static bool cur_pump;
static runstate_t rs;
static bool enter_req, stop_req;
static dlz_entry_t dlz;
static uint32_t pump_low_t; static bool pump_low;

/* step telemetry */
static step_rec_t trec[STEP_LOG_MAX];
static uint32_t ntrec;
static step_rec_t tcur;
static int64_t acc_sup, acc_flow;
static uint32_t acc_n;
static step_hook_t step_hook;

void cycle_set_step_hook(step_hook_t h) { step_hook = h; }
const step_rec_t *cycle_step_log(uint32_t *count) { *count = ntrec; return trec; }
const char *step_end_name(uint8_t end)
{
    switch (end) { case END_EXIT: return "exit"; case END_TIME: return "time"; case END_TIMEOUT: return "TIMEOUT"; default: return "ABORT"; }
}

static void trec_begin(const step_t *st)
{
    const sensors_t *ss = sensors();
    memset(&tcur, 0, sizeof tcur);
    tcur.name = st->name;
    tcur.stage = st->stage;
    tcur.valves = st->valves;
    tcur.w0_x10 = ss->weight_x10;
    tcur.pmin_x10 = tcur.pmax_x10 = ss->p_bus_x10;
    tcur.tmax_x10 = ss->temp_ok ? ss->temp_x10 : -999;
    acc_sup = acc_flow = 0; acc_n = 0;
}

static void trec_sample(void)
{
    const sensors_t *ss = sensors();
    if (ss->p_bus_x10 < tcur.pmin_x10) tcur.pmin_x10 = ss->p_bus_x10;
    if (ss->p_bus_x10 > tcur.pmax_x10) tcur.pmax_x10 = ss->p_bus_x10;
    acc_sup += ss->p_supply_x10; acc_flow += ss->flow_x10; acc_n++;
    if (ss->temp_ok && ss->temp_x10 > tcur.tmax_x10) tcur.tmax_x10 = ss->temp_x10;
}

static void trec_end(uint8_t end, uint32_t ms)
{
    const sensors_t *ss = sensors();
    if (tcur.name == NULL) return;
    tcur.end = end;
    tcur.ms = ms;
    tcur.w1_x10 = ss->weight_x10;
    tcur.sup_avg_x10 = acc_n ? (int32_t)(acc_sup / (int64_t)acc_n) : ss->p_supply_x10;
    tcur.flow_avg_x10 = acc_n ? (int32_t)(acc_flow / (int64_t)acc_n) : ss->flow_x10;
    uint32_t at = ntrec;
    ev_log(EV_STEP, at, (int32_t)end);
    if (ntrec < STEP_LOG_MAX) trec[ntrec++] = tcur;
    if (step_hook) step_hook(&tcur, at);
    tcur.name = NULL;
}

/* condition steps may be given more time on the bench (`set tmo`); fixed-time steps and the
 * leak / seal windows never change */
static uint32_t step_max(const step_t *st)
{
    if (st->exit == EX_TIME || (st->flags & (SF_SELFCHECK | SF_LEAK_HOLD | SF_SEAL_HOLD))) return st->max_ms;
    return (uint32_t)(((uint64_t)st->max_ms * v.tmo_pct) / 100u);
}

uint32_t cycle_disinfect_hold_left_s(void)
{
    const settings_t *st = store_settings();
    if (!st->germicide_in_lines || st->disinfect_time == 0u) return 0;
    uint32_t now = bsp_rtc_get();
    if (now < st->disinfect_time) return 0;                     /* clock was reset: cannot tell */
    uint32_t el = now - st->disinfect_time;
    return el >= DISINFECT_HOLD_S ? 0u : DISINFECT_HOLD_S - el;
}

const cycle_view_t *cycle_view(void) { return &v; }
bool cycle_busy(void) { return v.state != CY_IDLE; }
static bool is_patient(program_t p) { return p <= PROG_HIGH; }
static bool needs_dialyser(program_t p) { return p <= PROG_PRECLEAN; }

void cycle_init(void)
{
    memset(&v, 0, sizeof v);
    v.state = CY_IDLE;
    steps = NULL;
    cur = NULL;
}

static int32_t target_g(const step_t *s)
{
    switch (s->target) {
    case T_CLEAN_CONC:  return rc ? rc->clean_conc : 0;
    case T_CLEAN_WATER: return rc ? rc->clean_water : 0;
    case T_STER_CONC:   return rc ? rc->ster_conc : 0;
    case T_STER_WATER:  return rc ? rc->ster_water : 0;
    case T_DI_FILL:     return rc ? rc->di_fill : 0;
    case T_BD_FILL:     return rc ? rc->bd_fill : 0;
    case T_HALF_START:  return w_start / 20;          /* half of the start weight, g */
    default:            return s->value;
    }
}

static void apply_outputs(uint16_t mask, bool pump, uint16_t flags)
{
    if (mask != cur_mask) {
        if (!out_set(mask, (flags & SF_TANK) ? OUT_ALLOW_TANK : 0u)) mask = 0;   /* interlock refused */
        cur_mask = mask;
    }
    if (pump != cur_pump) { out_pump(pump); cur_pump = pump; }
}

static void write_run(void) { (void)store_run_write(&rs); }

static void log_cycle(uint8_t extra_flags)
{
    log_rec_t r;
    memset(&r, 0, sizeof r);
    r.time = bsp_rtc_get();
    r.dlz_id = v.dlz_id;
    r.program = (uint8_t)v.prog;
    r.result = extra_flags;
    if (v.pass) r.result |= LOG_PASS;
    if (v.tcv_done && !v.tcv_ok) r.result |= LOG_TCV_FAIL;
    if (v.leak_done && !v.leak_ok) r.result |= LOG_LEAK_FAIL;
    if (v.first_use) r.result |= LOG_FIRST_USE;
    r.tcv_x10 = v.tcv_x10;
    r.baseline_x10 = v.baseline_x10;
    r.pct_x10 = v.pct_x10;
    r.leak_x10 = v.leak_x10;
    if (v.prog == PROG_SVC_SEAL)
        for (uint8_t i = 0; i < v.seal_n; i++) if (v.seal_x10[i] > r.leak_x10) r.leak_x10 = v.seal_x10[i];
    r.alarm = (uint8_t)v.alarm;
    r.stage = v.stage;
    r.duration_s = (uint16_t)clamp_i32((int32_t)v.cycle_elapsed_s, 0, 65535);
    r.uses = v.uses;
    r.dtype = v.dtype;
    ev_log(EV_PROG, (uint32_t)v.prog, 0);
    if (!store_log_append(&r)) alarm_raise(ALM_STORAGE);
}

static void enter_step(const step_t *s, uint32_t now)
{
    cur = s;
    step_t0 = now;
    w_start = sensors()->weight_x10;
    liquid_cnt = 0;
    press_cnt = 0;
    supply_low = false;
    sensor_bad = false;
    v.step_name = s->name;
    v.step_max_ms = step_max(s);
    pump_low = false;
    trec_begin(s);
    if (s->stage && s->stage != v.stage) {
        v.stage = s->stage;
        rs.stage = s->stage;
        write_run();
    }
    if (s->flags & SF_TCV_DRAW) w1 = w_start;
    if (s->flags & (SF_LEAK_HOLD | SF_SEAL_HOLD)) p0_taken = false;
    if ((s->flags & SF_GERMICIDE) && !rs.germicide) { rs.germicide = 1; write_run(); }
    if (s->flags & SF_DOSE) { dose_t = now; dose_w = w_start; }
    cur_mask = 0xFFFFu;   /* force rewrite */
    cur_pump = !s->pump;
}

static void finish_recovery(void);

static void abort_cycle_end(alarm_t a, uint32_t now, uint8_t end)
{
    if (v.state != CY_RUN) return;
    trec_end(end, elapsed(now, step_t0));
    alarm_raise(a);
    v.alarm = a;
    v.aborted = true;
    v.pass = false;
    out_all_off();
    cur_mask = 0; cur_pump = false;
    const step_t *rec = NULL;
    bool rail = out_rail_ready() && !out_fault();
    switch (a) {
    case ALM_VALVE_FAULT: case ALM_POWER_RAIL: case ALM_DLZ_REMOVED: case ALM_SENSOR: case ALM_ADD_CHEM:
    case ALM_PUMP_FAULT: case ALM_STORAGE:
        rec = NULL; break;
    case ALM_OVERFILL:
        rec = (sensors()->p_supply_ok && sensors()->p_supply_x10 >= SUPPLY_MIN_PSIG_X10) ? &drain_step : &vent_step; break;
    default:
        rec = &vent_step; break;
    }
    if (rec && rail) { v.state = CY_RECOVER; enter_step(rec, now); }
    else finish_recovery();
}

static void abort_cycle(alarm_t a, uint32_t now) { abort_cycle_end(a, now, END_ABORT); }

static void finish_recovery(void)
{
    out_safe();
    log_cycle(LOG_ABORTED);
    store_run_clear();
    v.state = CY_DONE;
}

static void complete_program(uint32_t now)
{
    out_all_off();
    cur_mask = 0; cur_pump = false;
    if (is_patient(v.prog)) {
        v.pass = v.tcv_ok && v.leak_ok;
        if (v.first_use) dlz.baseline_x10 = v.tcv_x10;
        dlz.id = v.dlz_id;
        dlz.dtype = v.dtype;
        if (dlz.uses < 255u) dlz.uses++;
        v.uses = dlz.uses;
        (void)store_dlz_put(&dlz);
        log_cycle(LOG_COMPLETE);
        v.state = CY_HOLD;
        v.stage = 9;
        v.step_name = "HOLD + VENT";
        vent_t = now;
        vent_on = false;
        removed = false;
        alarm_beep(1500);
    } else if (PROG_IS_SERVICE(v.prog)) {
        v.pass = true;
        if (v.prog == PROG_SVC_SEAL) {
            v.pass = v.seal_n == 3u;
            for (uint8_t i = 0; i < v.seal_n; i++) if (v.seal_x10[i] > SEAL_LIMIT_X10) v.pass = false;
        }
        log_cycle(LOG_COMPLETE);
        out_safe();
        store_run_clear();
        v.state = CY_DONE;
        alarm_beep(v.pass ? 1500 : 300);
    } else if (v.prog == PROG_PRECLEAN) {
        v.pass = true;
        log_cycle(LOG_COMPLETE);
        out_safe();
        store_run_clear();
        v.state = CY_DONE;
        alarm_beep(1500);
    } else if (v.prog == PROG_DISINFECT) {
        settings_t *st = store_settings();
        st->germicide_in_lines = 1;
        st->disinfect_time = bsp_rtc_get();
        (void)store_save_settings();
        v.pass = true;
        log_cycle(LOG_COMPLETE);
        out_safe();
        store_run_clear();
        v.state = CY_DISINF_HOLD;
        v.step_name = "DISINFECTING";
        alarm_beep(1500);
    } else {                                   /* rinse-out */
        out_safe();
        v.state = CY_STRIP;
        v.step_name = "TEST STRIP?";
        alarm_beep(1500);
    }
}

alarm_t cycle_start(const cycle_req_t *r)
{
    const sensors_t *s = sensors();
    if (v.state != CY_IDLE) return ALM_STOPPED;
    if (r->prog >= PROG_COUNT) return ALM_SENSOR;
    if (!sensors_rails_ok()) return ALM_POWER_RAIL;
    if (out_fault()) return ALM_VALVE_FAULT;
    if (!store_healthy()) return ALM_STORAGE;  /* baseline TCV and the log cannot be kept */
    if (post_result() & POST_IMAGE) return ALM_FW_IMAGE;
    if (kittest_active()) return ALM_STOPPED;
    if (needs_dialyser(r->prog)) {
        if (!s->dialyser) return ALM_NO_DIALYSER;
        if (store_settings()->germicide_in_lines) return ALM_RINSE_NEEDED;
    } else if (!PROG_IS_SERVICE(r->prog) && s->dialyser) {
        return ALM_DLZ_FITTED;                 /* SAF-03 */
    }
    const recipe_t *found = NULL;
    if (r->prog <= PROG_PRECLEAN) {
        found = recipe_find(r->dtype, r->prime_ml);
        if (!found) return ALM_SENSOR;
    } else if (r->prog == PROG_DISINFECT) {
        found = recipe_disinfect();
    }

    req = *r;
    rc = found;
    memset(&v, 0, sizeof v);
    v.prog = r->prog;
    v.tmo_pct = store_tmo_pct();
    ntrec = 0;
    tcur.name = NULL;
    v.dlz_id = r->dlz_id;
    v.dtype = r->dtype;
    memset(&dlz, 0, sizeof dlz);
    if (is_patient(r->prog)) {
        if (store_dlz_find(r->dlz_id, &dlz) && dlz.baseline_x10 > 0u) { v.first_use = false; v.baseline_x10 = dlz.baseline_x10; }
        else { v.first_use = true; dlz.id = r->dlz_id; dlz.uses = 0; }
        v.uses = dlz.uses;
    }
    steps = cycle_program_steps(r->prog, &nsteps);
    idx = 0;
    uint32_t now = bsp_millis();
    cycle_t0 = now;
    memset(&rs, 0, sizeof rs);
    rs.active = 1;
    rs.program = (uint8_t)r->prog;
    rs.dlz_id = r->dlz_id;
    rs.start_time = bsp_rtc_get();
    write_run();
    out_rail(true);
    ev_log(EV_PROG, (uint32_t)r->prog, 1);
    v.state = CY_RUN;
    enter_req = stop_req = false;
    enter_step(&steps[0], now);
    return ALM_NONE;
}

void cycle_stop(void) { stop_req = true; }
void cycle_enter(void) { enter_req = true; }

void cycle_strip(bool negative)
{
    if (v.state != CY_STRIP) return;
    if (negative) {
        settings_t *st = store_settings();
        st->germicide_in_lines = 0;
        (void)store_save_settings();
        v.pass = true;
        log_cycle(LOG_COMPLETE);
        store_run_clear();
        v.state = CY_DONE;
    } else {
        /* still positive: rinse again */
        cycle_req_t again = req;
        store_run_clear();
        v.state = CY_IDLE;
        (void)cycle_start(&again);
    }
}

static bool step_done(const step_t *s, uint32_t now)
{
    const sensors_t *ss = sensors();
    int32_t w = ss->weight_x10;
    int32_t tg = target_g(s) * 10;
    switch (s->exit) {
    case EX_TIME:    return elapsed(now, step_t0) >= step_max(s);
    case EX_TANK_LE: return w <= tg;
    case EX_TANK_GE: return w >= tg;
    case EX_ADDED:   return w >= w_start + tg;
    case EX_DRAWN:   return w <= w_start - tg;
    case EX_PBUS_LE:   /* must hold for 200 ms: a failing sensor sweeping through the range cannot pass */
        if (ss->p_bus_ok && ss->p_bus_x10 <= (int32_t)s->value * 10) { if (press_cnt < 255u) press_cnt++; } else press_cnt = 0;
        return press_cnt >= 20u;
    case EX_LIQUID:  if (ss->liquid) { if (liquid_cnt < 255u) liquid_cnt++; } else liquid_cnt = 0; return liquid_cnt >= 3u;
    case EX_READY:   return out_rail_ready() && ss->weight_ok && ss->p_bus_ok && ss->p_supply_ok &&
                            elapsed(now, step_t0) >= 1000u;
    default:         return false;
    }
}

static void leave_step(const step_t *s, uint32_t now, uint8_t end)
{
    const sensors_t *ss = sensors();
    const cal_t *cal = store_cal();
    trec_end(end, elapsed(now, step_t0));
    if (s->flags & SF_TCV_EVAL) {
        int32_t tcv = (w1 - ss->weight_x10) - cal->wf_x10;
        if (tcv < 0) tcv = 0;
        v.tcv_x10 = (uint16_t)clamp_i32(tcv, 0, 65535);
        v.tcv_done = true;
        if (v.first_use) { v.baseline_x10 = v.tcv_x10; v.pct_x10 = 1000; v.tcv_ok = v.tcv_x10 > 0u; }
        else {
            v.pct_x10 = v.baseline_x10 ? (uint16_t)clamp_i32((int32_t)((uint32_t)v.tcv_x10 * 1000u / v.baseline_x10), 0, 9999) : 0u;
            v.tcv_ok = v.pct_x10 >= TCV_PASS_PCT_X10;
        }
        if (!v.tcv_ok) { abort_cycle(ALM_TCV_FAIL, now); return; }
    }
    if (s->flags & SF_LEAK_HOLD) {
        int32_t loss = ss->p_bus_x10 - p0;
        v.leak_x10 = (int16_t)clamp_i32(loss, -32768, 32767);
        v.leak_done = true;
        v.leak_ok = loss <= (int32_t)leak_limit_x10(v.dtype);
        if (!v.leak_ok) { abort_cycle(ALM_LEAK_FAIL, now); return; }
    }
    if ((s->flags & SF_SEAL_HOLD) && s->value >= 0 && s->value < 3) {
        v.seal_x10[s->value] = (int16_t)clamp_i32(p0_taken ? ss->p_bus_x10 - p0 : 9999, -32768, 32767);
        if ((uint8_t)(s->value + 1) > v.seal_n) v.seal_n = (uint8_t)(s->value + 1);
    }
    idx++;
    if (idx >= nsteps) complete_program(now);
    else enter_step(&steps[idx], now);
}

static void run_tick(uint32_t now)
{
    const sensors_t *ss = sensors();
    const step_t *s = cur;
    uint32_t el = elapsed(now, step_t0);
    uint32_t max_ms = step_max(s);
    v.step_elapsed_ms = el;
    trec_sample();

    /* ---- global safety, every tick ---- */
    if (stop_req) { stop_req = false; abort_cycle(ALM_STOPPED, now); return; }
    if (ss->weight_ok && ss->weight_x10 > TANK_OVERFILL_G * 10) { abort_cycle(ALM_OVERFILL, now); return; }
    if (out_fault()) { abort_cycle(ALM_VALVE_FAULT, now); return; }
    if (!sensors_rails_ok()) { abort_cycle(ALM_POWER_RAIL, now); return; }
    if (out_rail_ready() && ss->vrail_mv < VRAIL_ON_MIN_MV) { abort_cycle(ALM_POWER_RAIL, now); return; }
    if (needs_dialyser(v.prog) && !ss->dialyser) { abort_cycle(ALM_DLZ_REMOVED, now); return; }
    bool sensors_needed = !(s->flags & SF_SELFCHECK);
    /* S2 must be healthy for the whole patient cycle: results are only valid if it worked throughout */
    bool need_s2 = (s->flags & SF_PBUS) || (v.state == CY_RUN && needs_dialyser(v.prog));
    bool bad = sensors_needed && (!ss->weight_ok || (need_s2 && !ss->p_bus_ok));
    if (bad) { if (!sensor_bad) { sensor_bad = true; sensor_bad_t = now; } else if (elapsed(now, sensor_bad_t) >= 1000u) { abort_cycle(ALM_SENSOR, now); return; } }
    else sensor_bad = false;

    /* supply pressure: venturi (V1) and bus fill (V2) need RO water */
    bool uses_water = (s->valves & (V1 | V2)) != 0u;
    if ((s->flags & SF_SELFCHECK) && ss->p_supply_ok && out_rail_ready() && el >= 1000u && ss->p_supply_x10 < SUPPLY_MIN_PSIG_X10) {
        abort_cycle(ALM_LOW_SUPPLY, now); return;
    }
    if (uses_water) {
        bool low = !ss->p_supply_ok || ss->p_supply_x10 < SUPPLY_MIN_PSIG_X10;
        if (low) { if (!supply_low) { supply_low = true; supply_low_t = now; } else if (elapsed(now, supply_low_t) >= SUPPLY_LOW_ALARM_MS) { abort_cycle(ALM_LOW_SUPPLY, now); return; } }
        else supply_low = false;
    }

    /* dosing pump: concentrate switch and stall detection */
    if (s->flags & SF_DOSE) {
        if (ss->conc_low) { abort_cycle(ALM_ADD_CHEM, now); return; }
        if (elapsed(now, dose_t) >= DOSE_STALL_MS) {
            if (ss->weight_x10 - dose_w < 10) { abort_cycle(ALM_ADD_CHEM, now); return; }
            dose_t = now; dose_w = ss->weight_x10;
        }
    }

    /* dosing pump wiring: the 1 ohm shunt must see current while the pump runs */
    if (s->pump && out_pump_on() && !store_settings()->pump_check_off) {
        if (ss->pump_ma < (uint32_t)PUMP_OPEN_MA) {
            if (!pump_low) { pump_low = true; pump_low_t = now; }
            else if (elapsed(now, pump_low_t) >= PUMP_CHECK_MS) { abort_cycle(ALM_PUMP_FAULT, now); return; }
        } else pump_low = false;
    }

    /* leak test / seal test: P0 after the settle time, loss measured over the following window */
    if ((s->flags & (SF_LEAK_HOLD | SF_SEAL_HOLD)) && !p0_taken && el >= LEAK_SETTLE_MS) { p0 = ss->p_bus_x10; p0_taken = true; }

    /* ---- outputs ---- */
    uint16_t mask = s->valves;
    if ((s->flags & SF_PULSE_V2) && (mask & V2)) {
        int32_t remaining = (w_start + target_g(s) * 10) - ss->weight_x10;
        if (remaining <= DILUTE_SLOW_BAND_G * 10 && (now % 600u) >= 200u) mask &= (uint16_t)~V2;
    }
    bool wait_rail = (s->flags & SF_SELFCHECK) != 0u;
    if (!wait_rail) apply_outputs(mask, s->pump != 0u, s->flags);

    /* ---- exit / timeout ---- */
    if (s->exit != EX_TIME && step_done(s, now)) { leave_step(s, now, END_EXIT); return; }
    if (el >= max_ms) {
        if (s->exit == EX_TIME) leave_step(s, now, END_TIME);
        else abort_cycle_end((alarm_t)s->timeout_alarm, now, END_TIMEOUT);
    }
}

static void recover_tick(uint32_t now)
{
    const step_t *s = cur;
    uint32_t el = elapsed(now, step_t0);
    v.step_elapsed_ms = el;
    trec_sample();
    if (!out_rail_ready() || out_fault()) { trec_end(END_ABORT, el); finish_recovery(); return; }
    apply_outputs(s->valves, false, s->flags);
    if (step_done(s, now) || el >= s->max_ms) { trec_end(step_done(s, now) ? END_EXIT : END_TIME, el); out_all_off(); finish_recovery(); }
}

static void hold_tick(uint32_t now)
{
    const sensors_t *ss = sensors();
    /* PRC-03: vent pulse every 10 s until the dialyser is removed */
    if (!vent_on && elapsed(now, vent_t) >= VENT_PERIOD_MS) { vent_on = true; vent_t = now; (void)out_set(V5 | V9 | V10, OUT_ALLOW_TANK); }
    else if (vent_on && elapsed(now, vent_t) >= VENT_PULSE_MS) { vent_on = false; vent_t = now; (void)out_set(0, 0); }
    if (!ss->dialyser) { if (!removed) { removed = true; removed_t = now; } }
    else removed = false;
    if ((removed && elapsed(now, removed_t) >= 1000u) || enter_req) {
        enter_req = false;
        out_safe();
        store_run_clear();
        v.state = CY_IDLE;
    }
    stop_req = false;   /* STOP in hold just silences; dialyser stays vented */
}

void cycle_tick(uint32_t now)
{
    if (v.state != CY_IDLE && v.state != CY_DONE && v.state != CY_DISINF_HOLD && v.state != CY_STRIP)
        v.cycle_elapsed_s = elapsed(now, cycle_t0) / 1000u;
    switch (v.state) {
    case CY_RUN:     run_tick(now); break;
    case CY_RECOVER: recover_tick(now); break;
    case CY_HOLD:    hold_tick(now); break;
    case CY_DISINF_HOLD:
        if (enter_req) { enter_req = false; v.state = CY_IDLE; }
        stop_req = false;
        break;
    case CY_DONE:
        if (enter_req) { enter_req = false; alarm_ack(); v.state = CY_IDLE; }
        stop_req = false;
        break;
    case CY_STRIP:
        stop_req = false; enter_req = false;
        break;
    case CY_IDLE:
    default:
        stop_req = false; enter_req = false;
        break;
    }
}
