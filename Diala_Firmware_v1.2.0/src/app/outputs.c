#include "outputs.h"
#include "config.h"
#include "sensors.h"
#include "store.h"
#include "util.h"
#include "events.h"
#include "../bsp/bsp.h"

typedef enum { RAIL_OFF, RAIL_SETTLING, RAIL_RESET_WAIT, RAIL_READY } rail_state_t;

static rail_state_t rail;
static uint32_t rail_t;
static uint16_t mask;
static bool pump;
static bool fault;
static uint32_t nfault_since;
static bool nfault_seen;
static bool selftest_failed;      /* result of the check at the latest rail-up            */
static uint32_t selftest_fail_count;
static bool monitor = true;        /* setting: DRV8860 nFAULT stops the cycle              */
static bool monitor_session;       /* monitor && the check passed at this rail-up          */
static uint8_t ctrl_written;       /* DRV8860 control byte sent at this rail-up (0 = default, 100 %) */
static bool ctrl_ok = true;
static uint32_t refresh_t;

void out_init(void)
{
    rail = RAIL_OFF;
    mask = 0;
    pump = false;
    fault = false;
    bsp_valve_write(0);
    bsp_pump(0);
    bsp_valve_rail(false);
    monitor = store_settings()->drv_fault_monitor != 0u;
    monitor_session = monitor;
}

void out_monitor_enable(bool en) { monitor = en; monitor_session = en && !selftest_failed; }
uint32_t out_selftest_fail_count(void) { return selftest_fail_count; }

void out_rail(bool on)
{
    if (on && rail == RAIL_OFF) { rail = RAIL_SETTLING; rail_t = bsp_millis(); fault = false; bsp_valve_rail(true); }
    if (!on) { out_all_off(); if (rail != RAIL_OFF) ev_log(EV_RAIL, 0, 0); rail = RAIL_OFF; bsp_valve_rail(false); }
}

bool out_rail_ready(void) { return rail == RAIL_READY; }

bool out_set(uint16_t m, uint8_t flags)
{
    m &= (uint16_t)(VALVE_ALL_MASK | SPARE_OUT);
    if (m != 0u && rail != RAIL_READY) return false;              /* PCB-03: no rail, no valve */
    if ((m & V5) && !(flags & OUT_ALLOW_TANK)) return false;       /* SAF-02: tank isolated in rinse steps */
    if (m != mask) ev_log(EV_VALVES, m, 0);
    mask = m;
    bsp_valve_write(mask);
    return true;
}

void out_pump(bool on)
{
    bool p = on && rail == RAIL_READY;
    if (p != pump) ev_log(EV_PUMP, p ? 1u : 0u, 0);
    pump = p;
    bsp_pump(pump ? 100u : 0u);
}

void out_all_off(void)
{
    if (mask) ev_log(EV_VALVES, 0, 0);
    if (pump) ev_log(EV_PUMP, 0, 0);
    mask = 0;
    pump = false;
    bsp_valve_write(0);
    bsp_pump(0);
}

void out_safe(void)
{
    out_all_off();
    if (rail != RAIL_OFF) ev_log(EV_RAIL, 0, 0);
    rail = RAIL_OFF;
    bsp_valve_rail(false);
}

bool out_rail_on(void) { return rail != RAIL_OFF; }
uint8_t out_hold_ctrl(bool *verified) { *verified = ctrl_ok; return ctrl_written; }

uint16_t out_valves(void) { return mask; }
bool out_pump_on(void) { return pump; }
bool out_fault(void) { return fault; }
bool out_selftest_failed(void) { return selftest_failed; }

void out_tick(uint32_t now)
{
    const sensors_t *s = sensors();
    switch (rail) {
    case RAIL_OFF:
        break;
    case RAIL_SETTLING:
        if (elapsed(now, rail_t) >= VRAIL_SETTLE_MS && s->vrail_mv >= VRAIL_ON_MIN_MV) {
            /* Rail was off, so every output read as open load (OL latched). Clear it. */
            bsp_valve_fault_reset();
            /* optional energise-then-hold PWM (bench: coil temperature / U10 load, see README B8).
             * VM was off, so the control register is at its power-up default and must be rewritten. */
            {
                uint8_t h = store_settings()->drv_hold;
                ctrl_written = (h >= 1u && h <= 7u) ? (uint8_t)(0x80u | (h << 4) | DRV_ENERGISE_CODE) : 0u;
                if (ctrl_written) {
                    bsp_valve_control(ctrl_written);
                    uint16_t rb = bsp_valve_read_control();
                    ctrl_ok = rb == (uint16_t)((ctrl_written << 8) | ctrl_written);
                } else ctrl_ok = true;
            }
            rail = RAIL_RESET_WAIT;
            rail_t = now;
        }
        break;
    case RAIL_RESET_WAIT:
        if (elapsed(now, rail_t) >= 20u) {
            rail = RAIL_READY;
            nfault_seen = false;
            /* every rail-up: all outputs are off and the fault register was just reset, so nFAULT
             * must be high. Low = a valve plug open (open-load) or the reset command not taking
             * effect (bench item B1). Run on with monitoring off for this session and warn. */
            selftest_failed = bsp_valve_nfault();
            if (selftest_failed) selftest_fail_count++;
            monitor_session = monitor && !selftest_failed;
            ev_log(EV_RAIL, selftest_failed ? 2u : 1u, (int32_t)s->vrail_mv);
        }
        break;
    case RAIL_READY:
        if (elapsed(now, refresh_t) >= 200u) {          /* periodic rewrite against noise */
            refresh_t = now;
            bsp_valve_write(mask);
        }
        if (monitor_session && bsp_valve_nfault()) {
            if (!nfault_seen) { nfault_seen = true; nfault_since = now; }
            else if (elapsed(now, nfault_since) >= 50u) { fault = true; out_all_off(); }
        } else {
            nfault_seen = false;
        }
        break;
    }
}
