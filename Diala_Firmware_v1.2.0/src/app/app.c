#include "app.h"
#include "config.h"
#include "alarms.h"
#include "cli.h"
#include "cycle.h"
#include "outputs.h"
#include "post.h"
#include "events.h"
#include "kittest.h"
#include "sensors.h"
#include "store.h"
#include "ui.h"
#include "util.h"
#include "../bsp/bsp.h"

static uint32_t t_seq, t_keys, t_ui, t_wdg, t_lcd;
static uint32_t drv_fail_seen;
static bool storage_alarmed;
static uint8_t key_stable, key_last, key_cnt;
static uint32_t key_down_t;
static bool key_long_sent;
static bool tables_ok;
static uint8_t checkin;        /* bit0 sequencer, bit1 ui; watchdog kicked only when all ran */

bool app_tables_ok(void) { return tables_ok; }

static void keys_tick(uint32_t now)
{
    uint8_t raw = bsp_keys_raw();
    if (raw == key_last) { if (key_cnt < 255u) key_cnt++; } else { key_cnt = 0; key_last = raw; }
    if (key_cnt == 3u && raw != key_stable) {          /* 30 ms debounce */
        uint8_t pressed = (uint8_t)(raw & (uint8_t)~key_stable);
        key_stable = raw;
        for (uint8_t b = 0; b < 6u; b++) {
            uint8_t k = (uint8_t)(1u << b);
            if (!(pressed & k)) continue;
            key_down_t = now; key_long_sent = false;
            ev_log(EV_KEY, k, 0);
            if (k == KEY_STOP) cycle_stop();          /* STOP always reaches the sequencer (UI-02) */
            ui_key(k, KEV_PRESS);
        }
    }
    if ((key_stable & KEY_ENTER) && !key_long_sent && elapsed(now, key_down_t) >= 3000u) {
        key_long_sent = true;
        ui_key(KEY_ENTER, KEV_LONG);
    }
}

void app_init(void)
{
    uint8_t st = store_init();
    { settings_t *set = store_settings(); set->boot_count++; (void)store_save_settings(); }
    ev_log(EV_BOOT, bsp_reset_flags(), (int32_t)store_settings()->boot_count);
    sensors_init();
    out_init();
    cycle_init();
    char why[48];
    tables_ok = cycle_validate_tables(why, sizeof why);
    ui_init();
    cli_init();
    if (st & STORE_CAL_DEFAULTS) alarm_raise(ALM_CAL_NEEDED);

    runstate_t rs;
    if (store_run_read(&rs)) {                     /* power was lost during a cycle */
        log_rec_t r = {0};
        r.time = bsp_rtc_get(); r.dlz_id = rs.dlz_id; r.program = rs.program; r.result = LOG_ABORTED;
        r.alarm = (uint8_t)ALM_POWER_LOSS; r.stage = rs.stage;
        (void)store_log_append(&r);
        if (rs.germicide) alarm_raise(ALM_POWER_LOSS);   /* vent before removing the dialyser */
        store_run_clear();
    }
    uint32_t now = bsp_millis();
    t_seq = t_keys = t_ui = t_wdg = t_lcd = now;
    drv_fail_seen = out_selftest_fail_count();
    storage_alarmed = false;
    post_start(now);
    kittest_init(cli_line);
}

void app_poll(void)
{
    uint32_t now = bsp_millis();
    if (!tables_ok) {                              /* corrupt build: never drive valves */
        out_safe();
        if (elapsed(now, t_wdg) >= WDG_KICK_MS) { t_wdg = now; bsp_wdg_kick(); }
        return;
    }
    if (elapsed(now, t_seq) >= TICK_SEQ_MS) {
        t_seq += TICK_SEQ_MS;
        if (elapsed(now, t_seq) > 100u) t_seq = now;   /* never try to catch up a long stall */
        sensors_tick(now);
        out_tick(now);
        if (!post_done()) post_tick(now);
        cycle_tick(now);
        kittest_tick(now);
        alarm_tick(now);
        { bool g, r; if (!kittest_leds(&g, &r)) { g = alarm_active() == ALM_NONE; r = !g; } bsp_leds(g, r); }
        uint32_t f = out_selftest_fail_count();
        if (f != drv_fail_seen) { drv_fail_seen = f; alarm_raise(ALM_DRV_SELFTEST); }
        if (!store_healthy() && !storage_alarmed) { storage_alarmed = true; alarm_raise(ALM_STORAGE); }
        checkin |= 1u;
    }
    if (elapsed(now, t_keys) >= TICK_KEYS_MS) { t_keys = now; keys_tick(now); }
    if (elapsed(now, t_lcd) >= LCD_REFRESH_MS) { t_lcd = now; bsp_lcd_reinit(); }   /* resync after any glitch */
    if (elapsed(now, t_ui) >= TICK_UI_MS) { t_ui = now; ui_tick(now); checkin |= 2u; }
    cli_poll();
    if (elapsed(now, t_wdg) >= WDG_KICK_MS && checkin == 3u) {   /* FW-04: kick only from a healthy loop */
        t_wdg = now; checkin = 0; bsp_wdg_kick();
    }
}
