/* Diala firmware host tests: the real application code against the simulated plant.
 * Every scenario runs in its own process (fork) so module state never leaks between tests. */
#define _POSIX_C_SOURCE 200809L
#include "sim.h"
#include "../app/app.h"
#include "../app/config.h"
#include "../app/cycle.h"
#include "../app/alarms.h"
#include "../app/store.h"
#include "../app/outputs.h"
#include "../app/sensors.h"
#include "../app/post.h"
#include "../app/kittest.h"
#include "../app/events.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

static int fails;
static uint32_t max_kick_gap;
static bool watch_invariants = true;
static char fail_msg[256];

#define EXPECT(c, ...) do { if (!(c)) { fails++; snprintf(fail_msg, sizeof fail_msg, __VA_ARGS__); \
    fprintf(stderr, "    FAIL line %d: %s\n", __LINE__, fail_msg); } } while (0)

/* One simulated millisecond per iteration. Time the firmware spends in blocking I/O
 * (sim_cost_us) stalls the main loop: the plant and the clock keep running without app_poll(),
 * so watchdog gaps and sensor staleness are measured as they would be on the board. */
static uint32_t max_stall_ms;
static void step(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        P.ms++;
        sim_physics(1.0);
        sim_uart_drain(1);
        sim_cost_us = 0.0;
        app_poll();
        uint32_t stall = (uint32_t)(sim_cost_us / 1000.0);
        if (stall > max_stall_ms) max_stall_ms = stall;
        for (uint32_t k = 0; k < stall; k++) { P.ms++; sim_physics(1.0); sim_uart_drain(1); }
        i += stall;
        if (watch_invariants) {
            uint32_t gap = P.ms - P.last_kick_ms;
            if (P.ms > 2000u && gap > max_kick_gap) max_kick_gap = gap;
        }
    }
}

static void boot(void)
{
    sim_defaults();
    sim_fram_clear();
    app_init();
    step(1600);
    if (alarm_active() == ALM_CAL_NEEDED) alarm_ack();   /* fresh FRAM: defaults are the plant's truth */
}

typedef bool (*cond_fn)(void);
static bool run_until(cond_fn f, uint32_t max_ms)
{
    for (uint32_t t = 0; t < max_ms; t += 10u) { if (f()) return true; step(10); }
    return f();
}

static bool st_hold(void) { return cycle_view()->state == CY_HOLD; }
static bool st_done(void) { return cycle_view()->state == CY_DONE; }
static bool st_idle(void) { return cycle_view()->state == CY_IDLE; }
static bool st_end(void)  { cy_state_t s = cycle_view()->state; return s == CY_HOLD || s == CY_DONE || s == CY_DISINF_HOLD || s == CY_STRIP; }
static const char *want_step;
static bool st_step(void) { const char *n = cycle_view()->step_name; return cycle_view()->state == CY_RUN && n && !strcmp(n, want_step); }
static uint8_t want_stage;
static bool st_stage(void) { return cycle_view()->state == CY_RUN && cycle_view()->stage == want_stage; }

static alarm_t start(program_t p, uint8_t dtype, uint16_t prime, uint32_t id)
{
    cycle_req_t r = { p, dtype, prime, id };
    return cycle_start(&r);
}

static void finish_hold(void) { P.dialyser = false; run_until(st_idle, 5000); P.dialyser = true; step(50); }

static void key(uint8_t k) { P.keys = k; step(80); P.keys = 0; step(80); }

/* full reprocess helper: returns final state */
static void full_cycle(program_t p, uint8_t dt, uint32_t id)
{
    EXPECT(start(p, dt, 120, id) == ALM_NONE, "start refused");
    run_until(st_end, 30u * 60u * 1000u);
}

/* ======================================================================= scenarios */

static void t_tables(void)
{
    char why[64];
    EXPECT(cycle_validate_tables(why, sizeof why), "table validation: %s", why);
    EXPECT(app_tables_ok(), "app refused tables");
    /* interlocks in the output layer */
    out_safe();
    EXPECT(!out_set(V1, 0), "valve accepted with rail off (PCB-03)");
    out_rail(true); step(300);
    EXPECT(out_rail_ready(), "rail not ready");
    EXPECT(!out_set(V2 | V5, 0), "V5 accepted without tank permission (SAF-02)");
    EXPECT(out_set(V2 | V5, OUT_ALLOW_TANK), "V5 refused with permission");
    out_safe();
}

static void t_first_use_pass(void)
{
    full_cycle(PROG_LOW, DT_LOW, 1001);
    const cycle_view_t *v = cycle_view();
    EXPECT(v->state == CY_HOLD, "state %d alarm %s", v->state, alarm_text(v->alarm));
    EXPECT(v->pass && v->first_use, "pass=%d first=%d", v->pass, v->first_use);
    EXPECT(v->tcv_x10 >= 1150 && v->tcv_x10 <= 1260, "TCV %u (true 120 ml)", v->tcv_x10);
    EXPECT(v->leak_ok && v->leak_x10 < 100, "leak %d", v->leak_x10);
    EXPECT(v->cycle_elapsed_s <= 12u * 60u, "cycle %u s > 12 min (GEN-02)", v->cycle_elapsed_s);
    dlz_entry_t e;
    EXPECT(store_dlz_find(1001, &e) && e.baseline_x10 == v->tcv_x10 && e.uses == 1, "baseline not stored");
    log_rec_t r;
    EXPECT(store_log_get(0, &r) && (r.result & LOG_PASS) && (r.result & LOG_FIRST_USE), "log record");
    EXPECT(max_kick_gap <= 300u, "watchdog gap %u ms", max_kick_gap);
    EXPECT(!P.v1_v2_together && !P.illegal_write, "illegal valve pattern");
    printf("    TCV %.1f ml, leak loss %.1f mmHg, cycle %u:%02u\n", v->tcv_x10 / 10.0, v->leak_x10 / 10.0, v->cycle_elapsed_s / 60u, v->cycle_elapsed_s % 60u);
    finish_hold();
    EXPECT(st_idle() && !P.rail, "not idle/rail off after removal");
}

static void t_reuse(double tcv, bool expect_pass)
{
    full_cycle(PROG_LOW, DT_LOW, 2002);
    finish_hold();
    P.tcv_ml = tcv;
    P.ms += 1000;
    full_cycle(PROG_LOW, DT_LOW, 2002);
    const cycle_view_t *v = cycle_view();
    if (expect_pass) {
        EXPECT(v->state == CY_HOLD && v->pass && !v->first_use, "reuse pass expected, state %d alarm %s", v->state, alarm_text(v->alarm));
        EXPECT(v->pct_x10 >= 880 && v->pct_x10 <= 950, "pct %u", v->pct_x10);
    } else {
        EXPECT(v->state == CY_DONE && v->alarm == ALM_TCV_FAIL, "TCV fail expected, state %d alarm %s", v->state, alarm_text(v->alarm));
        log_rec_t r;
        EXPECT(store_log_get(0, &r) && (r.result & LOG_TCV_FAIL) && !(r.result & LOG_PASS), "log flags");
        EXPECT(!P.rail, "rail still on after fail");
    }
}
static void t_reuse_pass(void) { t_reuse(110.0, true); }
static void t_reuse_tcv_fail(void) { t_reuse(90.0, false); }

static void t_leak(program_t p, uint8_t dt, double rate, bool expect_pass)
{
    P.leak_mmhg_s = rate;
    full_cycle(p, dt, 3003);
    const cycle_view_t *v = cycle_view();
    if (expect_pass) EXPECT(v->state == CY_HOLD && v->leak_ok, "leak pass expected, loss %d alarm %s", v->leak_x10, alarm_text(v->alarm));
    else EXPECT(v->state == CY_DONE && v->alarm == ALM_LEAK_FAIL, "leak fail expected, loss %d state %d", v->leak_x10, v->state);
}
static void t_leak_fail_low(void)  { t_leak(PROG_LOW, DT_LOW, 1.5, false); }
static void t_leak_20_high(void)   { t_leak(PROG_HIGH, DT_HIGH, 1.0, true); }
static void t_leak_20_low(void)    { t_leak(PROG_LOW, DT_LOW, 1.0, false); }

static void t_connector(void)
{
    P.connector_leak = true;
    full_cycle(PROG_LOW, DT_LOW, 4004);
    const cycle_view_t *v = cycle_view();
    EXPECT(v->state == CY_DONE && v->alarm == ALM_VACUUM && v->stage == 2, "vacuum alarm expected: %s stage %u", alarm_text(v->alarm), v->stage);
    EXPECT(!P.rail && P.valves == 0, "outputs not safe");
}

static uint16_t max_valves_seen;
static void t_low_supply_start(void)
{
    P.supply_psig = 10.0;
    EXPECT(start(PROG_LOW, DT_LOW, 120, 5005) == ALM_NONE, "start");
    for (int i = 0; i < 2000 && !st_done(); i++) { step(10); if (P.valves) max_valves_seen |= P.valves; }
    EXPECT(st_done() && cycle_view()->alarm == ALM_LOW_SUPPLY, "alarm %s", alarm_text(cycle_view()->alarm));
    EXPECT((max_valves_seen & (V1 | V2)) == 0, "water valves opened with low supply: 0x%x", max_valves_seen);
}

static void t_supply_drop_rinse(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 5006) == ALM_NONE, "start");
    want_stage = 5; run_until(st_stage, 20u * 60u * 1000u);
    uint32_t t_drop = P.ms; P.supply_fail_at_ms = P.ms;
    run_until(st_done, 30000);
    uint32_t dt = P.ms - t_drop;
    EXPECT(cycle_view()->alarm == ALM_LOW_SUPPLY, "alarm %s", alarm_text(cycle_view()->alarm));
    EXPECT(dt >= 3000u && dt <= 6000u, "reaction %u ms (3 s rule + vent)", dt);
}

static void t_conc_low(void)
{
    P.conc_switch_low = true;
    full_cycle(PROG_LOW, DT_LOW, 6006);
    EXPECT(cycle_view()->alarm == ALM_ADD_CHEM && cycle_view()->stage == 3, "alarm %s", alarm_text(cycle_view()->alarm));
}

static void t_dose_stall(void)
{
    P.conc_available = false;
    full_cycle(PROG_LOW, DT_LOW, 6007);
    EXPECT(cycle_view()->alarm == ALM_ADD_CHEM, "alarm %s", alarm_text(cycle_view()->alarm));
    EXPECT(P.pump_duty == 0, "pump left on");
}

static void t_overfill(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 7007) == ALM_NONE, "start");
    want_stage = 5; run_until(st_stage, 20u * 60u * 1000u);
    P.v5_stuck = true;                         /* tank valve leaks: rinse water floods the tank */
    run_until(st_done, 5u * 60u * 1000u);
    EXPECT(cycle_view()->alarm == ALM_OVERFILL, "alarm %s", alarm_text(cycle_view()->alarm));
    P.v5_stuck = false;
}

static void t_tcv_timeout(void)
{
    P.s8_dead = true;
    full_cycle(PROG_LOW, DT_LOW, 8008);
    EXPECT(cycle_view()->alarm == ALM_TIMEOUT && cycle_view()->stage == 6, "alarm %s stage %u", alarm_text(cycle_view()->alarm), cycle_view()->stage);
}

static void t_stop_key(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 9009) == ALM_NONE, "start");
    want_stage = 4; run_until(st_stage, 20u * 60u * 1000u);
    key(KEY_STOP);
    run_until(st_done, 10000);
    EXPECT(cycle_view()->alarm == ALM_STOPPED, "alarm %s", alarm_text(cycle_view()->alarm));
    log_rec_t r; EXPECT(store_log_get(0, &r) && (r.result & LOG_ABORTED), "aborted log");
    key(KEY_ENTER);
    EXPECT(st_idle() && alarm_active() == ALM_NONE, "ENTER should clear");
}

static void t_dialyser_removed(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 9010) == ALM_NONE, "start");
    want_stage = 5; run_until(st_stage, 20u * 60u * 1000u);
    P.dialyser = false;
    step(50);
    EXPECT(st_done() && cycle_view()->alarm == ALM_DLZ_REMOVED && !P.rail, "alarm %s rail %d", alarm_text(cycle_view()->alarm), P.rail);
}

static void t_disinfect_blocked(void)
{
    EXPECT(start(PROG_DISINFECT, 0, 0, 0) == ALM_DLZ_FITTED, "disinfect with dialyser must be refused (SAF-03)");
    P.dialyser = false; step(50);
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1) == ALM_NO_DIALYSER, "patient program without dialyser must be refused");
}

static void t_disinfect_rinseout(void)
{
    P.dialyser = false; P.bypass = true; step(50);
    EXPECT(start(PROG_DISINFECT, 0, 0, 0) == ALM_NONE, "disinfect start");
    run_until(st_end, 20u * 60u * 1000u);
    EXPECT(cycle_view()->state == CY_DISINF_HOLD, "state %d alarm %s", cycle_view()->state, alarm_text(cycle_view()->alarm));
    EXPECT(store_settings()->germicide_in_lines == 1, "flag not set");
    cycle_enter(); step(50);
    P.dialyser = true; P.bypass = false; step(50);
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1) == ALM_RINSE_NEEDED, "patient program must be blocked until rinse-out (SAN-03)");
    P.dialyser = false; P.bypass = true; step(50);
    EXPECT(start(PROG_RINSEOUT, 0, 0, 0) == ALM_NONE, "rinse-out start");
    run_until(st_end, 30u * 60u * 1000u);
    EXPECT(cycle_view()->state == CY_STRIP, "state %d alarm %s", cycle_view()->state, alarm_text(cycle_view()->alarm));
    cycle_strip(true); step(50);
    EXPECT(store_settings()->germicide_in_lines == 0, "flag not cleared");
    cycle_enter(); step(50);
    P.dialyser = true; P.bypass = false; step(50);
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1) == ALM_NONE, "patient program should start after rinse-out");
}

static void t_power_loss(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1111) == ALM_NONE, "start");
    want_step = "GERMICIDE>BLOOD"; run_until(st_step, 25u * 60u * 1000u);
    EXPECT(st_step(), "did not reach germicide step");
    /* power cut: everything restarts from FRAM */
    P.rail = false; P.valves = 0;
    app_init();
    step(500);
    EXPECT(alarm_active() == ALM_POWER_LOSS, "power-loss warning missing: %s", alarm_text(alarm_active()));
    log_rec_t r; EXPECT(store_log_get(0, &r) && r.alarm == ALM_POWER_LOSS, "power-loss log");
    EXPECT(!P.rail, "rail on after restart");
}

static void t_valve_fault(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1212) == ALM_NONE, "start");
    want_stage = 4; run_until(st_stage, 20u * 60u * 1000u);
    P.nfault = true;
    step(200);
    EXPECT(st_done() && cycle_view()->alarm == ALM_VALVE_FAULT && !P.rail, "alarm %s", alarm_text(cycle_view()->alarm));
}

static void t_drv_selftest(void)
{
    P.nfault = true; P.nfault_stuck_after_reset = true;
    full_cycle(PROG_LOW, DT_LOW, 1313);
    EXPECT(out_selftest_failed(), "self-test should fail");
    EXPECT(cycle_view()->state == CY_HOLD, "cycle should still complete with monitor disabled, alarm %s", alarm_text(cycle_view()->alarm));
}

static void t_preclean(void)
{
    full_cycle(PROG_PRECLEAN, DT_MID, 1414);
    const cycle_view_t *v = cycle_view();
    EXPECT(v->state == CY_DONE && v->pass && !v->aborted && !v->tcv_done, "state %d alarm %s", v->state, alarm_text(v->alarm));
}

static void t_log_wrap(void)
{
    for (uint32_t i = 0; i < 2100u; i++) { log_rec_t r = {0}; r.dlz_id = i; r.result = LOG_PASS; EXPECT(store_log_append(&r), "append"); }
    EXPECT(store_log_count() == LOG_CAPACITY, "count %u", store_log_count());
    log_rec_t r; EXPECT(store_log_get(0, &r) && r.dlz_id == 2099u, "newest %u", r.dlz_id);
    EXPECT(store_log_get(LOG_CAPACITY - 1u, &r) && r.dlz_id == 2100u - LOG_CAPACITY, "oldest %u", r.dlz_id);
    /* corrupt the newest record on FRAM */
    uint32_t slot = (store_settings()->log_head + LOG_CAPACITY - 1u) % LOG_CAPACITY;
    sim_fram()[NV_LOG + slot * LOG_REC_SIZE + 5] ^= 0x55u;
    EXPECT(!store_log_get(0, &r), "corrupt record not detected");
}

static void t_cal_copies(void)
{
    store_cal()->wf_x10 = 654; store_save_cal();
    sim_fram()[NV_CAL_A + 4] ^= 0xFFu;                      /* corrupt copy A */
    store_init();
    EXPECT(store_cal()->wf_x10 == 654, "copy B not used");
    sim_fram()[NV_CAL_B + 4] ^= 0xFFu;                      /* corrupt copy B too */
    uint8_t st = store_init();
    EXPECT((st & STORE_CAL_DEFAULTS) && store_cal()->wf_x10 == 700, "defaults not loaded");
    sim_fram()[NV_CAL_A + 4] ^= 0xFFu; sim_fram()[NV_CAL_B + 4] ^= 0xFFu;
    store_cal()->wf_x10 = 1; store_save_cal(); sim_fram()[NV_CAL_A + 6] ^= 1u; sim_fram()[NV_CAL_B + 6] ^= 1u;
    app_init(); step(200);
    EXPECT(alarm_active() == ALM_CAL_NEEDED, "calibration alarm missing");
}

static void t_ui_flow(void)
{
    step(200);
    key(KEY_START);                                         /* HOME: LOW FLUX selected -> ID entry */
    for (int i = 0; i < 6; i++) key(KEY_ENTER);             /* digits 1-6 = 0 */
    for (int i = 0; i < 4; i++) key(KEY_UP);
    key(KEY_ENTER);                                         /* digit 7 = 4 */
    for (int i = 0; i < 2; i++) key(KEY_UP);
    key(KEY_ENTER);                                         /* digit 8 = 2 -> ID 00000042 */
    for (int i = 0; i < 3; i++) key(KEY_ENTER);             /* priming 120 ml */
    step(150);
    EXPECT(!strncmp(sim_lcd_row(0), "RUN LOW FLUX", 12), "confirm screen: '%s'", sim_lcd_row(0));
    key(KEY_START);
    step(300);
    EXPECT(cycle_view()->state == CY_RUN && cycle_view()->dlz_id == 42u, "cycle not started from keypad, id %u", cycle_view()->dlz_id);
    step(2000);
    EXPECT(sim_lcd_row(0)[0] == 'S', "run screen: '%s'", sim_lcd_row(0));
    printf("    LCD: [%s] [%s]\n", sim_lcd_row(0), sim_lcd_row(2));
}

static void t_hold_vent(void)
{
    full_cycle(PROG_LOW, DT_LOW, 1515);
    EXPECT(st_hold(), "not in hold");
    int pulses = 0; bool was = false; uint32_t on_ms = 0;
    for (int i = 0; i < 35000; i++) {
        step(1);
        bool on = (P.valves & (V5 | V9 | V10)) == (V5 | V9 | V10);
        if (on) on_ms++;
        if (on && !was) pulses++;
        was = on;
    }
    EXPECT(pulses >= 3 && pulses <= 4, "vent pulses %d in 35 s", pulses);
    EXPECT(on_ms >= (uint32_t)pulses * 450u && on_ms <= (uint32_t)pulses * 560u, "vent on time %u ms", on_ms);
}

static void t_mid_recipe(void)
{
    EXPECT(start(PROG_MID, DT_MID, 80, 1616) == ALM_NONE, "start");
    want_step = "CLEAN MEMBRANE"; run_until(st_step, 10u * 60u * 1000u);
    double t = P.tank_g;
    EXPECT(t >= 690.0 && t <= 720.0, "tank %.1f g, recipe 16 + 684 = 700 g", t);
}

static void t_cli(void)
{
    full_cycle(PROG_LOW, DT_LOW, 1717);
    finish_hold();
    sim_uart_clear();
    sim_uart_send("status\r");
    step(50);
    sim_uart_send("log\r");
    step(200);
    const char *o = sim_uart_output();
    EXPECT(strstr(o, "weight_g=") != NULL, "status output");
    EXPECT(strstr(o, "seq,date,dialyser_id") != NULL && strstr(o, "00001717,LOW FLUX,LOW,PASS+FIRST") != NULL, "log CSV:\n%s", o);
    sim_uart_send("valve 3 on\r"); step(50);
    EXPECT(strstr(sim_uart_output(), "ERR unknown or locked") != NULL, "service command must need unlock");
}

static void t_sensor_fault(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1818) == ALM_NONE, "start");
    want_stage = 2; run_until(st_stage, 60000);
    EXPECT(st_stage(), "stage 2 not reached (stage %u state %d)", cycle_view()->stage, cycle_view()->state);
    P.s2_broken = true;
    run_until(st_done, 5000);
    EXPECT(cycle_view()->alarm == ALM_SENSOR && cycle_view()->stage == 2, "alarm %s stage %u", alarm_text(cycle_view()->alarm), cycle_view()->stage);
}

static void t_rail_fault(void)
{
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1919) == ALM_NONE, "start");
    want_stage = 3; run_until(st_stage, 60000);
    P.v24 = 18.0;
    step(500);
    EXPECT(st_done() && cycle_view()->alarm == ALM_POWER_RAIL, "alarm %s", alarm_text(cycle_view()->alarm));
}


/* ======================================================================= v1.1 scenarios */

static bool cli_end_seen(void) { return strstr(sim_uart_output(), "\r\nend\r\n") != NULL; }

static void t_log_dump_during_cycle(void)
{
    for (uint32_t i = 0; i < LOG_CAPACITY; i++) { log_rec_t r = {0}; r.dlz_id = i; r.result = LOG_PASS; (void)store_log_append(&r); }
    EXPECT(start(PROG_LOW, DT_LOW, 120, 3101) == ALM_NONE, "start");
    want_stage = 3; run_until(st_stage, 120000);
    sim_uart_send("log\r");
    uint32_t lines0 = sim_uart_lines, t0 = P.ms;
    max_kick_gap = 0; max_stall_ms = 0;
    run_until(cli_end_seen, 120000);
    EXPECT(cli_end_seen(), "log dump did not finish");
    uint32_t lines = sim_uart_lines - lines0, dump_ms = P.ms - t0;
    EXPECT(lines >= LOG_CAPACITY + 2u, "only %u lines", lines);
    EXPECT(max_kick_gap <= 300u, "watchdog gap %u ms during dump (TPS3823 trips at 1120 ms)", max_kick_gap);
    EXPECT(max_stall_ms <= 25u, "main loop stalled %u ms", max_stall_ms);
    EXPECT(cycle_view()->state == CY_RUN || st_end(), "cycle disturbed: state %d alarm %s", cycle_view()->state, alarm_text(cycle_view()->alarm));
    run_until(st_end, 30u * 60u * 1000u);
    EXPECT(st_hold() && cycle_view()->pass, "cycle should still pass, alarm %s", alarm_text(cycle_view()->alarm));
    printf("    %u log lines in %.1f s during the cycle, max watchdog gap %u ms, max loop stall %u ms\n", lines, dump_ms / 1000.0, max_kick_gap, max_stall_ms);
}

static void t_post_pass(void)
{
    EXPECT(post_done(), "self-test not finished after boot");
    EXPECT((post_result() & POST_FAIL_MASK) == 0u, "self-test result 0x%x", post_result());
    EXPECT(!P.rail, "rail left on after self-test");
    EXPECT(P.fault_resets >= 1u, "fault reset not issued at self-test rail-up");
}

static void t_post_fram_dead(void)
{
    P.fram_dead = true;
    app_init();
    step(2000);
    EXPECT(post_done() && (post_result() & POST_FRAM), "FRAM failure not detected: 0x%x", post_result());
    EXPECT(alarm_active() == ALM_STORAGE, "alarm %s", alarm_text(alarm_active()));
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1) == ALM_STORAGE, "patient cycle must be refused without FRAM");
}

static void t_post_drv(void)
{
    P.nfault = true; P.nfault_stuck_after_reset = true;
    app_init();
    step(2000);
    EXPECT(post_result() & POST_DRV, "driver check not flagged: 0x%x", post_result());
    EXPECT(alarm_active() == ALM_DRV_SELFTEST, "alarm %s", alarm_text(alarm_active()));
}

static void t_svc_drain_fill(void)
{
    P.dialyser = false; P.tank_g = 500.0; step(50);
    EXPECT(start(PROG_SVC_FILL, 0, 0, 0) == ALM_NONE, "fill start");
    run_until(st_done, 120000);
    EXPECT(cycle_view()->pass && !cycle_view()->aborted, "fill: alarm %s", alarm_text(cycle_view()->alarm));
    /* tank already had 500 g: fill ends at once */
    cycle_enter(); step(50);
    EXPECT(start(PROG_SVC_DRAIN, 0, 0, 0) == ALM_NONE, "drain start");
    run_until(st_done, 200000);
    EXPECT(cycle_view()->pass && P.tank_g <= 6.0, "drain: tank %.1f g alarm %s", P.tank_g, alarm_text(cycle_view()->alarm));
    uint32_t n; const step_rec_t *t = cycle_step_log(&n);
    EXPECT(n == 2u && t[1].end == END_EXIT && t[1].w0_x10 > 4900 && t[1].w1_x10 <= 50, "drain telemetry n=%u", n);
    cycle_enter(); step(50);
    EXPECT(start(PROG_SVC_FILL, 0, 0, 0) == ALM_NONE, "fill start 2");
    run_until(st_done, 120000);
    EXPECT(cycle_view()->pass && P.tank_g >= 399.0 && P.tank_g <= 430.0, "fill: tank %.1f g", P.tank_g);
    step(200);
    EXPECT(!strncmp(sim_lcd_row(0), "FILL TANK DONE", 14) && strstr(sim_lcd_row(2), "g/s") != NULL, "LCD '%s' '%s'", sim_lcd_row(0), sim_lcd_row(2));
    printf("    LCD: [%s] [%s]\n", sim_lcd_row(1), sim_lcd_row(2));
}

static void t_svc_prime(void)
{
    P.dialyser = false; P.bypass = true; step(50);
    EXPECT(start(PROG_SVC_PRIME, 0, 0, 0) == ALM_NONE, "prime start");
    run_until(st_done, 200000);
    EXPECT(cycle_view()->pass && !cycle_view()->aborted, "prime: alarm %s", alarm_text(cycle_view()->alarm));
    uint32_t n; (void)cycle_step_log(&n);
    EXPECT(n == 4u, "prime steps %u", n);
    EXPECT(!P.rail && P.tank_g < 1.0, "tank must stay isolated during prime (%.1f g)", P.tank_g);
}

static void seal_run(uint16_t weep, bool expect_pass, const char *hint)
{
    P.dialyser = false; P.bypass = false; P.weep = weep; step(50);
    EXPECT(start(PROG_SVC_SEAL, 0, 0, 0) == ALM_NONE, "seal start");
    run_until(st_done, 200000);
    const cycle_view_t *v = cycle_view();
    EXPECT(!v->aborted && v->seal_n == 3u, "seal aborted: %s", alarm_text(v->alarm));
    EXPECT(v->pass == expect_pass, "seal pass=%d losses %d %d %d", v->pass, v->seal_x10[0], v->seal_x10[1], v->seal_x10[2]);
    step(200);
    if (hint) EXPECT(strstr(sim_lcd_row(2), hint) != NULL, "hint '%s' (want %s)", sim_lcd_row(2), hint);
    printf("    weep 0x%03x: [%s] [%s]\n", weep, sim_lcd_row(0), sim_lcd_row(1));
}
static void t_seal_pass(void)     { seal_run(0, true, NULL); }
static void t_seal_v10(void)      { seal_run(V10, false, "V7 V10"); }
static void t_seal_v5(void)       { seal_run(V5, false, "V2 V5"); }

static void t_tmo_scale(void)
{
    store_settings()->tmo_pct = 200; store_save_settings();
    EXPECT(start(PROG_LOW, DT_LOW, 120, 3201) == ALM_NONE, "start");
    want_step = "CONNECTOR CHECK"; run_until(st_step, 60000);
    EXPECT(cycle_view()->step_max_ms == 40000u, "connector max %u", cycle_view()->step_max_ms);
    want_step = "RINSE BLOOD TOP>BOT"; run_until(st_step, 20u * 60u * 1000u);
    EXPECT(cycle_view()->step_max_ms == 30000u, "fixed-time rinse must not scale: %u", cycle_view()->step_max_ms);
    want_step = "LEAK: HOLD"; run_until(st_step, 20u * 60u * 1000u);
    EXPECT(cycle_view()->step_max_ms == LEAK_SETTLE_MS + LEAK_HOLD_MS, "leak window must not scale: %u", cycle_view()->step_max_ms);
    run_until(st_end, 30u * 60u * 1000u);
    EXPECT(st_hold() && cycle_view()->pass, "cycle with tmo 200: %s", alarm_text(cycle_view()->alarm));
}

static void t_pump_open(void)
{
    P.pump_open = true;
    EXPECT(start(PROG_LOW, DT_LOW, 120, 3301) == ALM_NONE, "start");
    want_step = "DOSE CLEANER"; run_until(st_step, 120000);
    uint32_t t0 = P.ms;
    run_until(st_done, 20000);
    EXPECT(cycle_view()->alarm == ALM_PUMP_FAULT, "alarm %s", alarm_text(cycle_view()->alarm));
    EXPECT(P.ms - t0 <= 1600u && P.pump_duty == 0, "reaction %u ms, pump %u", P.ms - t0, P.pump_duty);
}

static bool cli_steps_end(void) { return strstr(sim_uart_output(), "GERMICIDE>BLOOD") && cli_end_seen(); }
static void t_telemetry(void)
{
    sim_uart_send("unlock 2580\r"); step(50);
    sim_uart_send("trace on\r"); step(50);
    full_cycle(PROG_LOW, DT_LOW, 3401);
    EXPECT(st_hold(), "cycle: %s", alarm_text(cycle_view()->alarm));
    uint32_t n; const step_rec_t *t = cycle_step_log(&n);
    EXPECT(n == 21u, "%u step records", n);
    EXPECT(strstr(sim_uart_output(), "STEP,17,7,LEAK: HOLD,V9 V10,time") != NULL, "live STEP line missing");
    EXPECT(strstr(sim_uart_output(), "\r\nT,") != NULL, "1 s trace lines missing");
    EXPECT(t[1].pmin_x10 <= -1000 && t[1].end == END_EXIT, "connector record pmin %d", t[1].pmin_x10);
    EXPECT(t[10].w1_x10 - t[10].w0_x10 >= 3900, "TCV fill record %d -> %d", t[10].w0_x10, t[10].w1_x10);
    finish_hold();
    sim_uart_clear();
    sim_uart_send("steps\r");
    run_until(cli_steps_end, 20000);
    EXPECT(strstr(sim_uart_output(), "n,stage,step,valves,end") && strstr(sim_uart_output(), "21,8,GERMICIDE>BLOOD,V1 V3 V5 V7 V8,exit"), "steps CSV:\n%s", sim_uart_output());
}

static void t_rinse_hold_warning(void)
{
    P.dialyser = false; P.bypass = true; step(50);
    EXPECT(start(PROG_DISINFECT, 0, 0, 0) == ALM_NONE, "disinfect start");
    run_until(st_end, 20u * 60u * 1000u);
    cycle_enter(); step(300);
    P.ms += 9000u * 1000u;                          /* two and a half hours later */
    step(300);
    EXPECT(cycle_disinfect_hold_left_s() > 3u * 3600u && cycle_disinfect_hold_left_s() < 4u * 3600u, "hold left %u s", cycle_disinfect_hold_left_s());
    for (int i = 0; i < 5; i++) key(KEY_DOWN);      /* LOW -> ... -> RINSE-OUT */
    key(KEY_START);
    step(200);
    EXPECT(!strncmp(sim_lcd_row(0), "RUN RINSE-OUT", 13) && !strncmp(sim_lcd_row(2), "HOLD LEFT 3h", 12), "confirm '%s' '%s'", sim_lcd_row(0), sim_lcd_row(2));
}

static void t_keypad_service(void)
{
    step(200);
    P.dialyser = false; P.tank_g = 300.0; step(50);
    P.keys = KEY_ENTER; step(3200); P.keys = 0; step(100);       /* hold ENTER 3 s */
    for (int i = 0; i < 2; i++) key(KEY_UP);                      /* PIN 2 5 8 0 */
    key(KEY_ENTER);
    for (int i = 0; i < 5; i++) key(KEY_UP);
    key(KEY_ENTER);
    for (int i = 0; i < 8; i++) key(KEY_UP);
    key(KEY_ENTER);
    key(KEY_ENTER);
    step(150);
    EXPECT(!strncmp(sim_lcd_row(0), "SERVICE MENU", 12), "menu: '%s' '%s'", sim_lcd_row(0), sim_lcd_row(1));
    key(KEY_DOWN); key(KEY_DOWN);                                 /* DRAIN TANK */
    key(KEY_ENTER); step(150);
    EXPECT(!strncmp(sim_lcd_row(0), "RUN DRAIN TANK", 14), "confirm: '%s'", sim_lcd_row(0));
    key(KEY_START);
    run_until(st_done, 120000);
    EXPECT(cycle_view()->prog == PROG_SVC_DRAIN && cycle_view()->pass, "drain from keypad: %s", alarm_text(cycle_view()->alarm));
    log_rec_t r; EXPECT(store_log_get(0, &r) && r.program == PROG_SVC_DRAIN, "service run not logged");
}

static void t_hold_pwm(void)
{
    store_settings()->drv_hold = 5; store_save_settings();
    out_rail(true); step(300);
    bool ok; uint8_t cw = out_hold_ctrl(&ok);
    EXPECT(cw == (0x80u | (5u << 4) | DRV_ENERGISE_CODE) && ok, "control 0x%02x verified %d", cw, ok);
    out_safe(); step(50);
    store_settings()->drv_hold = 0; store_save_settings();
    out_rail(true); step(300);
    cw = out_hold_ctrl(&ok);
    EXPECT(cw == 0u, "hold must be off by default, 0x%02x", cw);
    out_safe();
    full_cycle(PROG_LOW, DT_LOW, 3501);
    EXPECT(st_hold(), "cycle %s", alarm_text(cycle_view()->alarm));
}

static void t_set_pin(void)
{
    sim_uart_send("unlock 2580\r"); step(50);
    sim_uart_send("set pin 4711\r"); step(50);
    sim_uart_send("lock\r"); step(50);
    sim_uart_send("unlock 2580\r"); step(50);
    EXPECT(strstr(sim_uart_output(), "ERR pin") != NULL, "old PIN still accepted");
    sim_uart_send("unlock 4711\r"); step(50);
    EXPECT(strstr(sim_uart_output(), "unlocked") != NULL, "new PIN refused");
    sim_uart_send("diag\r"); step(100);
    EXPECT(strstr(sim_uart_output(), "self-test: PASS") != NULL, "diag:\n%s", sim_uart_output());
}


/* ======================================================================= v1.2 kit test: a simulated technician */

typedef struct { bool say_no_v3; double s2_offset_v; bool power_cycles; uint32_t answered; uint32_t valves_seen_with_water; } bot_t;
static bot_t B;

static void power_cycle(void) { P.rail = false; P.valves = 0; P.pump_duty = 0; app_init(); }

static double volts_in(const char *p)
{
    const char *t = strstr(p, "wiper to ");
    return t ? atof(t + 9) : 0.0;
}

static void bot_act(void)
{
    static uint32_t handled;
    if (sim_prompts == handled) return;
    handled = sim_prompts;
    const char *p = sim_last_prompt;
    const char *keys[6] = { "START", "STOP", "MUTE", "UP", "DOWN", "ENTER" };
    const uint8_t kb[6] = { KEY_START, KEY_STOP, KEY_MUTE, KEY_UP, KEY_DOWN, KEY_ENTER };
    B.answered++;
    if (strstr(p, "Press the ")) { for (int i = 0; i < 6; i++) if (strstr(p, keys[i]) && strstr(p, "Press the ") + 10 == strstr(p, keys[i])) { P.keys = kb[i]; step(60); P.keys = 0; step(60); } return; }
    if (strstr(p, "Switch the board power OFF")) { step(200); power_cycle(); return; }
    if (strstr(p, "Turn RO water OFF")) { step(3000); P.supply_psig = 0.0; P.s3_wiper_v = 0.5; return; }
    if (strstr(p, "Take everything off") || strstr(p, "Remove the weight")) { P.tank_g = 0.0; step(300); sim_uart_send("ok\r"); return; }
    if (strstr(p, "Place the 500 g")) { P.tank_g = 500.0; step(300); sim_uart_send("ok\r"); return; }
    if (strstr(p, "Unplug the V2")) { P.open_load |= V2; sim_uart_send("ok\r"); return; }
    if (strstr(p, "Plug the V2")) { P.open_load &= (uint16_t)~V2; sim_uart_send("ok\r"); return; }
    if (strstr(p, "Is ONLY ")) {
        step(100);
        bool ok;
        if (strstr(p, "the pump")) ok = P.valves == 0 && P.pump_duty > 0;
        else { int v = atoi(strstr(p, "Is ONLY V") + 9); ok = P.valves == (uint16_t)(1u << (v - 1)) && P.pump_duty == 0; if (v == 3 && B.say_no_v3) ok = false; }
        sim_uart_send(ok ? "ok\r" : "no\r"); return;
    }
    if (strstr(p, "Set the S2 potentiometer")) { P.s2_wiper_v = volts_in(p) + B.s2_offset_v; step(100); sim_uart_send("ok\r"); return; }
    if (strstr(p, "Set the S3 potentiometer")) { P.s3_wiper_v = volts_in(p); step(100); sim_uart_send("ok\r"); return; }
    if (strstr(p, "Turn the S2 potentiometer to 0 V")) { P.s2_wiper_v = 0.0; step(100); sim_uart_send("ok\r"); return; }
    if (strstr(p, "Close switch S6")) { step(500); P.dialyser = true; return; }
    if (strstr(p, "Open switch S6"))  { step(500); P.dialyser = false; return; }
    if (strstr(p, "Close switch S7")) { step(500); P.conc_switch_low = true; return; }
    if (strstr(p, "Open switch S7"))  { step(500); P.conc_switch_low = false; return; }
    if (strstr(p, "Close switch S8")) { step(500); P.s8_force = 1; return; }
    if (strstr(p, "Open switch S8"))  { step(500); P.s8_force = 0; return; }
    if (strstr(p, "flow-pulse source to 75 Hz")) { P.flow_hz = 75.0; step(100); sim_uart_send("ok\r"); return; }
    sim_uart_send("ok\r");            /* LCD clear? buzzer + LEDs? -> yes */
}


static void kit_rig(void)
{
    P.dialyser = false; P.kit_pots = true; P.s2_wiper_v = 1.83; P.s3_wiper_v = 0.5; P.temp_present = true;
    P.supply_psig = 0.0; P.tank_g = 0.0; P.s8_force = 0;
    step(3000);
    sim_uart_send("unlock 2580\r"); step(50);
    sim_uart_send("set serial KIT-01\r"); step(50);
}

static void run_kittest(const char *cmd, uint32_t max_ms)
{
    sim_uart_send(cmd); step(100);
    EXPECT(kittest_active(), "kit test did not start: %s", sim_uart_output() + (strlen(sim_uart_output()) > 300 ? strlen(sim_uart_output()) - 300 : 0));
    for (uint32_t t = 0; t < max_ms && kittest_active(); t += 10u) {
        if (P.valves && P.supply_psig > 5.0 && !P.kit_pots) B.valves_seen_with_water++;
        bot_act(); step(10);
    }
    step(500);
}

static void t_kittest_all(void)
{
    kit_rig();
    uint32_t boots0 = store_settings()->boot_count;
    run_kittest("kittest all\r", 40u * 60u * 1000u);
    const char *o = sim_uart_output();
    EXPECT(!kittest_active(), "kit test still running, prompt '%s'", sim_last_prompt);
    EXPECT(strstr(o, "SUMMARY,PASS,") != NULL, "summary not PASS:\n%s", o + (strlen(o) > 3000 ? strlen(o) - 3000 : 0));
    EXPECT(strstr(o, "K,3.2,FRAM KEEP,PASS") != NULL && store_settings()->boot_count == boots0 + 1u, "power-cycle check (boots %u -> %u)", boots0, store_settings()->boot_count);
    EXPECT(strstr(o, "K,4.6,DRIVER HEAT,PASS,") != NULL, "heat item");
    EXPECT(strstr(o, "K,4.4,VALVE MAP,PASS,0x7FF") != NULL, "valve map");
    EXPECT(strstr(o, "E,") && strstr(o, ",KITTEST,item"), "events not streamed during the test");
    EXPECT(strstr(o, "serial KIT-01") != NULL, "serial missing from summary");
    sim_uart_clear();
    sim_uart_send("kittest report\r"); step(3000);
    o = sim_uart_output();
    int lines = 0; for (const char *q = o; (q = strstr(q, "\nK,")) != NULL; q++) lines++;
    EXPECT(lines == 29, "report has %d K lines (header + 28 items)", lines);
    const char *h = strstr(o, "K,4.6,DRIVER HEAT");
    printf("    %u prompts answered; %.60s\n", B.answered, h ? h : "-");
}

static void t_kittest_faults(void)
{
    kit_rig();
    P.temp_present = false;           /* no DS18B20 fitted */
    B.say_no_v3 = true;               /* technician sees V4 click when V3 is on (swapped plug) */
    B.s2_offset_v = 0.12;             /* wrong divider resistor on S2 */
    run_kittest("kittest 4\r", 10u * 60u * 1000u);
    run_kittest("kittest 5\r", 10u * 60u * 1000u);
    sim_uart_clear();
    sim_uart_send("kittest report\r"); step(3000);
    const char *o = sim_uart_output();
    EXPECT(strstr(o, "K,5.1,S2 SCALE,FAIL") != NULL, "S2 out of tolerance not failed:\n%s", o);
    EXPECT(strstr(o, "SUMMARY,FAIL") != NULL, "summary should fail");
    sim_uart_clear();
    run_kittest("kittest 4\r", 10u * 60u * 1000u);
    o = sim_uart_output();
    EXPECT(strstr(o, "K,4.4,VALVE MAP,FAIL,0x7FB") != NULL && strstr(o, "NO: V3") != NULL, "valve map should fail on V3:\n%s", o);
    EXPECT(strstr(o, "K,4.5,TEMP PROBE,SKIP") != NULL && strstr(o, "K,4.6,DRIVER HEAT,SKIP") != NULL, "no probe should skip");
}

static void t_kittest_water_interlock(void)
{
    kit_rig();
    P.kit_pots = false; P.supply_psig = 25.0;        /* RO water still on: real S3 reads 25 psig */
    sim_uart_send("kittest 4\r"); step(100);
    uint16_t seen = 0;
    for (int i = 0; i < 400; i++) { step(10); seen |= P.valves; }
    EXPECT(seen == 0, "valves 0x%x opened with RO water on", seen);
    EXPECT(strstr(sim_last_prompt, "RO water OFF") != NULL, "no water prompt: '%s'", sim_last_prompt);
    P.dialyser = true; step(50);
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1) == ALM_STOPPED, "patient program must be refused during a kit test");
    P.dialyser = false;
    P.supply_psig = 0.0;
    run_kittest("", 10u * 60u * 1000u);
    EXPECT(strstr(sim_uart_output(), "K,4.1,VALVE RAIL,PASS") != NULL, "rail item after water off");
}

static void t_kittest_reset(void)
{
    kit_rig();
    sim_uart_send("kittest 5\r"); step(500);
    EXPECT(kittest_active(), "not started");
    power_cycle(); step(2500);                       /* unexpected reset, not at the power-cycle item */
    EXPECT(!kittest_active(), "test should end after an unexpected reset");
    EXPECT(strstr(sim_uart_output(), "KITTEST END,controller reset during the test") != NULL, "reset not reported");
    EXPECT(!P.rail, "rail on after reset");
}

static void t_image_bad(void)
{
    P.image_bad = true;
    app_init(); step(2000);
    EXPECT(post_result() & POST_IMAGE, "image fault not in self-test 0x%x", post_result());
    EXPECT(alarm_active() == ALM_FW_IMAGE, "alarm %s", alarm_text(alarm_active()));
    EXPECT(start(PROG_LOW, DT_LOW, 120, 1) == ALM_FW_IMAGE, "programs must be refused with a bad image");
}

static void t_events(void)
{
    sim_uart_send("unlock 2580\r"); step(50);
    key(KEY_UP);
    P.dialyser = false; step(100); P.dialyser = true; step(100);
    full_cycle(PROG_LOW, DT_LOW, 3601);
    finish_hold();
    sim_uart_clear();
    sim_uart_send("events 256\r");
    for (int i = 0; i < 600 && !cli_end_seen(); i++) step(10);
    const char *o = sim_uart_output();
    EXPECT(strstr(o, ",PROGRAM,low start") && strstr(o, ",VALVES,V1 V3 V4 V6 V7 V9") && strstr(o, ",RAIL,ready, driver check ok")
           && strstr(o, ",STEP,") && strstr(o, ",INPUT,S6 off"), "events dump:\n%s", o);
    EXPECT(ev_count() > 50u, "only %u events", ev_count());
}

/* ======================================================================= runner */
typedef struct { const char *name; const char *req; void (*fn)(void); } test_t;
static const test_t tests[] = {
    { "step tables + output interlocks",         "FW-02 SAF-01 SAF-02 PCB-03", t_tables },
    { "first use, low flux: PASS, baseline",     "PRC-05 GEN-02 GEN-05 FW-04", t_first_use_pass },
    { "reuse, TCV 92 % of baseline: PASS",       "PRC-05",              t_reuse_pass },
    { "reuse, TCV 75 % of baseline: FAIL",       "PRC-05 alarm TCV",    t_reuse_tcv_fail },
    { "leak 30 mmHg, low flux: FAIL",            "stage 7",             t_leak_fail_low },
    { "leak 20 mmHg, high flux (limit 25): PASS","stage 7",             t_leak_20_high },
    { "leak 20 mmHg, low flux (limit 17): FAIL", "stage 7",             t_leak_20_low },
    { "connector leak: vacuum alarm, vent",      "stage 2",             t_connector },
    { "low supply at start: no water valves",    "alarm low pressure",  t_low_supply_start },
    { "supply lost during rinse: 3 s rule",      "alarm low pressure",  t_supply_drop_rinse },
    { "concentrate low switch: add chemical",    "S7 alarm",            t_conc_low },
    { "dosing pump stall: add chemical",         "alarm add chemical",  t_dose_stall },
    { "tank overfill: drain recovery",           "alarm overfill",      t_overfill },
    { "liquid sensor dead: TCV step timeout",    "PRC-06",              t_tcv_timeout },
    { "STOP key: safe state, logged",            "UI-02 FW-03",         t_stop_key },
    { "dialyser removed mid-cycle",              "safety",              t_dialyser_removed },
    { "disinfect blocked with dialyser fitted",  "SAN-02 SAF-03",       t_disinfect_blocked },
    { "disinfect -> blocked -> rinse-out -> ok", "SAN-01 SAN-03",       t_disinfect_rinseout },
    { "power loss in germicide: warn on restart","alarm power loss",    t_power_loss },
    { "valve driver fault mid-cycle",            "alarm valve fault",   t_valve_fault },
    { "driver self-test fails: degraded run",    "bench item DRV-1",    t_drv_selftest },
    { "pre-clean program",                       "PRC-04",              t_preclean },
    { "log ring: 2,082 records, CRC",            "FW-06",               t_log_wrap },
    { "calibration A/B copies, CRC, defaults",   "FW-05",               t_cal_copies },
    { "keypad: ID + priming entry, start",       "UI-01 UI-03",         t_ui_flow },
    { "hold: vent 0.5 s every 10 s",             "PRC-03",              t_hold_vent },
    { "middle flux 71-90 ml recipe 16+684 g",    "PRC-02 recipes",      t_mid_recipe },
    { "USB CLI: status, CSV log, lock",          "GEN-06",              t_cli },
    { "pressure sensor fault",                   "alarm sensor",        t_sensor_fault },
    { "24 V supply out of range",                "alarm supply voltage",t_rail_fault },
    { "log dump (2,082 rows) during a cycle",    "v1.1 non-blocking USB", t_log_dump_during_cycle },
    { "power-on self-test: healthy board",       "v1.1 POST",           t_post_pass },
    { "power-on self-test: FRAM dead",           "v1.1 POST FW-06",     t_post_fram_dead },
    { "power-on self-test: driver nFAULT stuck", "v1.1 POST B1",        t_post_drv },
    { "service: fill / drain tank, rates",       "v1.1 commissioning",  t_svc_drain_fill },
    { "service: prime lines (bypass)",           "v1.1 commissioning",  t_svc_prime },
    { "service: seal test, all valves tight",    "walkthrough pt 1",    t_seal_pass },
    { "service: seal test, V10 weeping",         "walkthrough pt 1",    t_seal_v10 },
    { "service: seal test, V5 weeping",          "walkthrough pt 1",    t_seal_v5 },
    { "time-limit scale 200 % (bench)",          "v1.1 B4",             t_tmo_scale },
    { "dosing pump open wire",                   "v1.1 pump check",     t_pump_open },
    { "step telemetry, trace, steps CSV",        "v1.1 B4",             t_telemetry },
    { "rinse-out: 6 h hold warning",             "SAN-01",              t_rinse_hold_warning },
    { "keypad: service menu -> drain tank",      "v1.1 UI",             t_keypad_service },
    { "CLI: change PIN, diag",                   "v1.1 GEN-06",         t_set_pin },
    { "valve hold PWM written at every rail-up", "v1.1 B8",             t_hold_pwm },
    { "kit test, all 28 items, power cycle",     "v1.2 kittest",        t_kittest_all },
    { "kit test catches swap, bad divider, no probe", "v1.2 kittest",   t_kittest_faults },
    { "kit test: no valve opens with water on",  "v1.2 safety",         t_kittest_water_interlock },
    { "kit test: unexpected reset reported",     "v1.2 kittest",        t_kittest_reset },
    { "firmware image CRC fault",                "v1.2 POST",           t_image_bad },
    { "event log: keys, inputs, valves, steps",  "v1.2 events",         t_events },
};

int main(int argc, char **argv)
{
    int total = (int)(sizeof tests / sizeof tests[0]), failed = 0;
    printf("Diala firmware " FW_VERSION_STR " - host simulator, %d scenarios\n", total);
    for (int i = 0; i < total; i++) {
        if (argc > 1 && !strstr(tests[i].name, argv[1])) continue;
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            boot();
            tests[i].fn();
            fflush(stdout);
            _exit(fails ? 1 : 0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (!ok) failed++;
        printf("%-3d %-44s %-28s %s\n", i + 1, tests[i].name, tests[i].req, ok ? "PASS" : "FAIL");
    }
    printf("%d/%d scenarios passed\n", total - failed, total);
    return failed ? 1 : 0;
}
