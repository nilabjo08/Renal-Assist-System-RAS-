#include "kittest.h"
#include "config.h"
#include "alarms.h"
#include "cycle.h"
#include "events.h"
#include "outputs.h"
#include "post.h"
#include "sensors.h"
#include "store.h"
#include "util.h"
#include "../bsp/bsp.h"
#include <string.h>

/* ------------------------------------------------------------------ item table */
enum {
    K_IMAGE, K_CLOCK, K_RTC, K_RAILS,
    K_KEYS, K_LCD, K_BUZZER,
    K_FRAM_RW, K_FRAM_KEEP, K_SCALE_ALIVE, K_SCALE_ZERO, K_SCALE_SPAN, K_SCALE_REPEAT, K_SCALE_DRIFT,
    K_RAIL_UP, K_DRV_CHECK, K_OPEN_LOAD, K_VALVE_MAP, K_TEMP_PROBE, K_HEAT,
    K_S2, K_S2_OPEN, K_S3, K_S6, K_S7, K_S8, K_FLOW, K_PUMP,
    K_COUNT
};

typedef struct { uint8_t stage; const char *name; uint8_t fmt; const char *unit; const char *limit; } kdef_t;
enum { F_INT, F_X10, F_HEX, F_MASK };   /* how the value is printed */

static const kdef_t defs[K_COUNT] = {
    [K_IMAGE]        = { 1, "IMAGE CRC",    F_HEX, "",       "matches build" },
    [K_CLOCK]        = { 1, "CLOCK",        F_INT, "",       "info" },
    [K_RTC]          = { 1, "RTC",          F_INT, " s",     "2-3 s in 2.1 s" },
    [K_RAILS]        = { 1, "24V/5V",       F_INT, " mV",    "21.0-27.5 V, 4.6-5.4 V" },
    [K_KEYS]         = { 2, "KEYS",         F_INT, " of 6",  "all 6 within 30 s each" },
    [K_LCD]          = { 2, "LCD",          F_INT, "",       "technician: all rows clear" },
    [K_BUZZER]       = { 2, "BUZZER/LEDS",  F_INT, "",       "technician: beep, green, red" },
    [K_FRAM_RW]      = { 3, "FRAM R/W",     F_INT, "",       "read-back equal" },
    [K_FRAM_KEEP]    = { 3, "FRAM KEEP",    F_INT, " boots", "token kept over power-off" },
    [K_SCALE_ALIVE]  = { 3, "SCALE ALIVE",  F_INT, "",       "samples within 2 s" },
    [K_SCALE_ZERO]   = { 3, "SCALE ZERO",   F_X10, " g p-p", "<= 1.0 g noise over 5 s" },
    [K_SCALE_SPAN]   = { 3, "SCALE SPAN",   F_INT, " cnt/g", "100-5000 counts per g" },
    [K_SCALE_REPEAT] = { 3, "SCALE REPEAT", F_X10, " g",     "0 and 500 g within 1.0 g, 3 times" },
    [K_SCALE_DRIFT]  = { 3, "SCALE DRIFT",  F_X10, " g p-p", "<= 2.0 g over 15 min (long: 60)" },
    [K_RAIL_UP]      = { 4, "VALVE RAIL",   F_INT, " mV",    ">= 20 V within 1 s" },
    [K_DRV_CHECK]    = { 4, "DRV CHECK",    F_INT, " fails", "nFAULT high after reset" },
    [K_OPEN_LOAD]    = { 4, "OPEN LOAD",    F_INT, "",       "V2 unplugged seen, then clear" },
    [K_VALVE_MAP]    = { 4, "VALVE MAP",    F_MASK, "",      "technician: each output only its own" },
    [K_TEMP_PROBE]   = { 4, "TEMP PROBE",   F_X10, " C",     "DS18B20 on J4 answers" },
    [K_HEAT]         = { 4, "DRIVER HEAT",  F_X10, " C max", "<= 85.0 C, 5 x 20 s six valves on" },
    [K_S2]           = { 5, "S2 SCALE",     F_X10, " mmHg",  "-750 / 0 / +1500 within 40" },
    [K_S2_OPEN]      = { 5, "S2 OPEN WIRE", F_INT, "",       "0 V flagged as fault" },
    [K_S3]           = { 5, "S3 SCALE",     F_X10, " psig",  "0 / 21.8 within 1, 174 within 3" },
    [K_S6]           = { 5, "S6 DIALYSER",  F_INT, "",       "closed then open seen" },
    [K_S7]           = { 5, "S7 CONC LOW",  F_INT, "",       "closed then open seen" },
    [K_S8]           = { 5, "S8 LIQUID",    F_INT, "",       "closed then open seen" },
    [K_FLOW]         = { 5, "S4 FLOW",      F_X10, " L/min", "75 Hz = 10.0 within 0.3" },
    [K_PUMP]         = { 5, "PUMP CURRENT", F_INT, " mA",    ">= 15 mA when on" },
};

/* ------------------------------------------------------------------ FRAM record */
typedef struct { uint8_t st; uint8_t run; uint8_t pad[2]; int32_t val; char note[16]; } kitem_t;
typedef struct {
    uint32_t magic;
    uint8_t  active, stage, cur, mode;     /* mode: 0 normal, 1 quick, 2 long */
    uint32_t token;
    uint32_t start_time;
    uint32_t boot_at_keep;
    kitem_t  it[K_COUNT];
    uint16_t crc;
    uint16_t pad;
} krec_t;
#define KT_MAGIC 0x4B495431u
_Static_assert(sizeof(krec_t) <= NV_KITTEST_MAX, "kit-test record must fit its FRAM area");

static krec_t R;
static kt_print_t out;
static uint8_t sub;
static uint32_t t0, now_ms;
static kt_answer_t ans;
static bool waiting;
static char pcode[8];
static char lcd1[21], lcd2[21];
static int32_t acc_min, acc_max, acc_n, worst;
static int64_t acc_sum;
static uint8_t idx2;
static uint16_t yes_mask;
static bool flag_a;
static bool led_g, led_r, led_drive;
static bool resumed_keep;
static bool water_wait;

static void save(void)
{
    R.magic = KT_MAGIC;
    R.crc = crc16_ccitt(&R, offsetof(krec_t, crc), 0xFFFFu);
    (void)store_kt_write(&R, sizeof R);
}

static void say(const char *s) { if (out) out(s); }

static void code_of(uint8_t k, char *b)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i <= k; i++) if (defs[i].stage == defs[k].stage) n++;
    b[0] = (char)('0' + defs[k].stage); b[1] = '.';
    if (n >= 10u) { b[2] = (char)('0' + n / 10u); b[3] = (char)('0' + n % 10u); b[4] = '\0'; }
    else { b[2] = (char)('0' + n); b[3] = '\0'; }
}

/* ------------------------------------------------------------------ helpers for items */
static void next_sub(uint8_t s) { sub = s; t0 = now_ms; ans = KA_NONE; waiting = false; pcode[0] = '\0'; water_wait = false; }

static void prompt(const char *l1, const char *l2, const char *usb)
{
    char b[160]; sb_t s;
    char c[6]; code_of(R.cur, c);
    waiting = true; ans = KA_NONE;
    sb_init(&s, pcode, sizeof pcode); sb_str(&s, c); sb_char(&s, '.'); sb_uint_w(&s, sub, 0, ' ');
    lcd_fmt(lcd1, l1); lcd1[20] = '\0';
    lcd_fmt(lcd2, l2); lcd2[20] = '\0';
    sb_init(&s, b, sizeof b); sb_str(&s, "PROMPT,"); sb_str(&s, pcode); sb_char(&s, ','); sb_str(&s, usb);
    say(b);
}

static void finish(kt_status_t st, int32_t val, const char *note)
{
    if (sub >= 0xFEu) return;          /* already finished in this tick */
    kitem_t *it = &R.it[R.cur];
    it->st = (uint8_t)st; it->val = val;
    memset(it->note, 0, sizeof it->note);
    if (note) { for (uint32_t i = 0; i < sizeof it->note - 1u && note[i]; i++) it->note[i] = note[i]; }
    ev_log(EV_KT, R.cur, (int32_t)st);
    char b[160];
    (void)kittest_report_line(R.cur, b, sizeof b);
    say(b);
    out_safe();
    led_drive = false;
    sub = 0xFFu;                       /* done marker; the tick moves to the next item */
}

static uint32_t el(void) { return elapsed(now_ms, t0); }
static bool water_on(void) { const sensors_t *s = sensors(); return s->p_supply_ok && s->p_supply_x10 > 50; }

/* true while the item must wait for RO water to be switched off (valves must not open on a live supply) */
static bool water_gate(void)
{
    if (water_on()) {
        if (!water_wait) { water_wait = true; prompt("RO WATER OFF", "(S3 below 5 psig)", "Turn RO water OFF (S3 must read below 5 psig); the test continues by itself"); }
        return true;
    }
    if (water_wait) { water_wait = false; waiting = false; pcode[0] = '\0'; ans = KA_NONE; }
    return false;
}

/* averaged uncalibrated reading over 1 s, once the technician confirms the setting */
static bool average(int32_t v, int32_t *mean)
{
    if (el() < 200u) { acc_sum = 0; acc_n = 0; return false; }
    acc_sum += v; acc_n++;
    if (el() < 1200u) return false;
    *mean = (int32_t)(acc_sum / (acc_n ? acc_n : 1));
    return true;
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------------ items. Each runs every 10 ms until finish(). */
static const uint8_t key_order[6] = { KEY_START, KEY_STOP, KEY_MUTE, KEY_UP, KEY_DOWN, KEY_ENTER };
static const char *const key_name[6] = { "START", "STOP", "MUTE", "UP", "DOWN", "ENTER" };
static uint8_t key_seen;

static void run_item(void)
{
    const sensors_t *s = sensors();
    char nb[24]; sb_t ns;
    switch (R.cur) {
    case K_IMAGE: { uint32_t crc; bool ok = bsp_image_check(&crc); finish(ok ? KS_PASS : KS_FAIL, (int32_t)crc, ok ? FW_VERSION_STR : "REFLASH"); break; }
    case K_CLOCK: { bsp_diag_t d; bsp_diag(&d); finish(KS_INFO, d.hse ? 1 : 0, d.hse ? "HSE crystal" : "HSI internal"); break; }
    case K_RTC:
        if (sub == 0) { acc_min = (int32_t)bsp_rtc_get(); next_sub(1); }
        else if (el() >= 2100u) {
            bsp_diag_t d; bsp_diag(&d);
            int32_t dt = (int32_t)bsp_rtc_get() - acc_min;
            finish(dt >= 2 && dt <= 3 ? KS_PASS : KS_FAIL, dt, d.lse ? "LSE crystal" : "LSI internal");
        }
        break;
    case K_RAILS: {
        bool ok = sensors_rails_ok();
        sb_init(&ns, nb, sizeof nb); sb_str(&ns, "5V="); sb_uint_w(&ns, s->v5_mv, 0, ' '); sb_str(&ns, "mV");
        finish(ok ? KS_PASS : KS_FAIL, (int32_t)s->v24_mv, nb); break; }

    case K_KEYS:
        if (sub == 0) { key_seen = 0; idx2 = 0; next_sub(1); }
        if (sub == 1) {
            if (idx2 >= 6u) { finish(KS_PASS, 6, "all keys"); break; }
            if (!waiting) {
                char l1[21], u[60]; sb_t a;
                sb_init(&a, l1, sizeof l1); sb_str(&a, "PRESS "); sb_str(&a, key_name[idx2]);
                sb_init(&a, u, sizeof u); sb_str(&a, "Press the "); sb_str(&a, key_name[idx2]); sb_str(&a, " key on the keypad");
                prompt(l1, "on the keypad", u);
            }
            if (key_seen & key_order[idx2]) { idx2++; t0 = now_ms; waiting = false; }
            else if (el() >= 30000u || ans == KA_SKIP || ans == KA_NO) {
                sb_init(&ns, nb, sizeof nb); sb_str(&ns, "no "); sb_str(&ns, key_name[idx2]);
                finish(KS_FAIL, idx2, nb);
            }
        }
        break;
    case K_LCD:
        if (!waiting) { lcd_fmt(lcd1, "ABCDEFGHIJKLMNOPQRST"); prompt("ABCDEFGHIJKLMNOPQRST", "All 4 rows clear?", "Are all four LCD rows clear and complete? START = yes, STOP = no (or type ok / no)");
                        lcd_fmt(lcd1, "abcdefghij0123456789"); }
        if (ans == KA_OK) finish(KS_PASS, 1, "confirmed");
        else if (ans == KA_NO) finish(KS_FAIL, 0, "technician: no");
        else if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        break;
    case K_BUZZER:
        if (!waiting) { prompt("Beep, green, red?", "START=yes STOP=no", "Did you hear the buzzer and see the green and the red LED in turn? START = yes, STOP = no"); }
        led_drive = true;
        { uint32_t ph = (el() / 700u) % 3u; led_g = ph == 1u; led_r = ph == 2u; if (ph == 0u && (el() % 700u) < 20u) alarm_beep(300); }
        if (ans == KA_OK) finish(KS_PASS, 1, "confirmed");
        else if (ans == KA_NO) finish(KS_FAIL, 0, "technician: no");
        else if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        break;

    case K_FRAM_RW: finish(store_selftest() ? KS_PASS : KS_FAIL, 0, store_healthy() ? "ok" : "FRAM fault"); break;
    case K_FRAM_KEEP:
        if (resumed_keep) {
            resumed_keep = false;
            finish(KS_PASS, (int32_t)store_settings()->boot_count, "kept");
            break;
        }
        if (sub == 0) {
            R.token = (now_ms * 2654435761u) ^ bsp_rtc_get() ^ 0x5A5Au;
            R.boot_at_keep = store_settings()->boot_count;
            save();
            next_sub(1);
            prompt("POWER OFF 5 s", "then ON again", "Switch the board power OFF for 5 s, then ON. The test resumes by itself after power-on");
        }
        if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        else if (el() >= 300000u) finish(KS_FAIL, 0, "no power cycle");
        break;
    case K_SCALE_ALIVE:
        if (s->weight_ok) finish(KS_PASS, s->scale_raw, "samples");
        else if (el() >= 2000u) finish(KS_FAIL, 0, "no HX711 data");
        break;
    case K_SCALE_ZERO:
        if (sub == 0) { next_sub(1); prompt("REMOVE ALL WEIGHT", "then START", "Take everything off the load cell, then press START (or type ok)"); }
        else if (sub == 1 && ans == KA_OK) { next_sub(2); acc_min = 0x7FFFFFFF; acc_max = -0x7FFFFFFF; acc_sum = 0; acc_n = 0; lcd_fmt(lcd1, "MEASURING NOISE"); lcd_fmt(lcd2, "5 s, do not touch"); }
        else if (sub == 2) {
            if (el() >= 500u) { int32_t r = s->scale_raw; if (r < acc_min) acc_min = r; if (r > acc_max) acc_max = r; acc_sum += r; acc_n++; }
            if (el() >= 5500u) {
                cal_t *cal = store_cal();
                int32_t cpg = cal->scale_cpg_x100 > 0 ? cal->scale_cpg_x100 : 1;
                int32_t pp = (int32_t)((int64_t)(acc_max - acc_min) * 1000 / cpg);
                cal->scale_zero = (int32_t)(acc_sum / (acc_n ? acc_n : 1));
                (void)store_save_cal();
                finish(pp <= 10 ? KS_PASS : KS_FAIL, pp, "zero saved");
            }
        }
        if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        break;
    case K_SCALE_SPAN:
        if (sub == 0) { next_sub(1); prompt("PLACE 500 g", "then START", "Place the 500 g reference weight, then press START"); }
        else if (sub == 1 && ans == KA_OK) { next_sub(2); lcd_fmt(lcd1, "SETTLING 3 s"); lcd_fmt(lcd2, ""); }
        else if (sub == 2 && el() >= 3000u) {
            cal_t *cal = store_cal();
            int32_t counts = s->scale_raw - cal->scale_zero;
            if (counts <= 0) { finish(KS_FAIL, counts, "no load seen"); break; }
            cal->scale_cpg_x100 = (int32_t)((int64_t)counts * 100 / 500);
            (void)store_save_cal();
            int32_t cpg = counts / 500;
            finish(cpg >= 100 && cpg <= 5000 ? KS_PASS : KS_FAIL, cpg, "span saved");
        }
        if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        break;
    case K_SCALE_REPEAT:
        /* subs: odd = remove / even = place; 3 rounds = subs 1..6 */
        if (sub == 0) { worst = 0; next_sub(1); }
        if (sub >= 1u && sub <= 6u) {
            bool place = (sub % 2u) == 0u;
            if (!waiting && !flag_a) prompt(place ? "PLACE 500 g" : "REMOVE WEIGHT", "then START",
                                            place ? "Place the 500 g weight, then press START" : "Remove the weight, then press START");
            if (ans == KA_OK && !flag_a) { flag_a = true; t0 = now_ms; waiting = false; lcd_fmt(lcd1, "READING..."); lcd_fmt(lcd2, ""); }
            if (flag_a && el() >= 3000u) {
                int32_t e = iabs(s->weight_x10 - (place ? 5000 : 0));
                if (e > worst) worst = e;
                flag_a = false;
                next_sub((uint8_t)(sub + 1u));
            }
        }
        if (sub == 7u) finish(worst <= 10 ? KS_PASS : KS_FAIL, worst, "500 g left on");
        if (ans == KA_SKIP) { flag_a = false; finish(KS_SKIP, 0, "skipped"); }
        break;
    case K_SCALE_DRIFT: {
        uint32_t dur = R.mode == 2u ? 3600000u : 900000u;
        if (R.mode == 1u) { finish(KS_SKIP, 0, "quick mode"); break; }
        if (sub == 0) { next_sub(1); acc_min = 0x7FFFFFFF; acc_max = -0x7FFFFFFF; }
        if (s->weight_ok) { if (s->weight_x10 < acc_min) acc_min = s->weight_x10; if (s->weight_x10 > acc_max) acc_max = s->weight_x10; }
        { char l[21]; sb_t a; sb_init(&a, l, sizeof l); sb_str(&a, "DRIFT "); sb_uint_w(&a, (dur - (el() < dur ? el() : dur)) / 60000u, 0, ' '); sb_str(&a, " min left");
          lcd_fmt(lcd1, l); lcd_fmt(lcd2, "500 g on, no touch"); }
        if (el() >= dur) finish(acc_max - acc_min <= 20 ? KS_PASS : KS_FAIL, acc_max - acc_min, "500 g");
        if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        break; }

    case K_RAIL_UP:
        if (sub == 0) { if (water_gate()) break; out_rail(true); next_sub(1); lcd_fmt(lcd1, "VALVE RAIL ON"); lcd_fmt(lcd2, "all outputs off"); }
        else if (sub == 1) {
            if (out_rail_ready()) {
                /* PASS, and the rail stays on so the driver check reads this rail-up */
                kitem_t *it = &R.it[R.cur]; it->st = KS_PASS; it->val = (int32_t)s->vrail_mv; memset(it->note, 0, sizeof it->note);
                memcpy(it->note, out_selftest_failed() ? "check FAILED" : "check ok", out_selftest_failed() ? 12u : 8u);
                ev_log(EV_KT, R.cur, KS_PASS);
                { char b[160]; (void)kittest_report_line(R.cur, b, sizeof b); say(b); }
                sub = 0xFEu;
            } else if (el() >= 1000u) finish(KS_FAIL, (int32_t)s->vrail_mv, "rail low");
        }
        break;
    case K_DRV_CHECK:
        if (sub == 0 && !out_rail_ready()) { if (water_gate()) break; out_rail(true); next_sub(1); break; }
        if (sub == 1 && !out_rail_ready()) { if (el() >= 1000u) finish(KS_FAIL, 0, "rail not ready"); break; }
        finish(out_selftest_failed() ? KS_FAIL : KS_PASS, (int32_t)out_selftest_fail_count(), out_selftest_failed() ? "plug open / B1" : "nFAULT high");
        break;
    case K_OPEN_LOAD:
        if (sub == 0) { if (water_gate()) break; out_safe(); next_sub(1); prompt("UNPLUG V2 LOAD", "then START", "Unplug the V2 load (J11 plug), then press START"); }
        else if (sub == 1 && ans == KA_OK) { out_rail(true); next_sub(2); }
        else if (sub == 2) {
            if (out_rail_ready()) { flag_a = out_selftest_failed(); out_safe(); next_sub(3); prompt("PLUG V2 BACK IN", "then START", "Plug the V2 load back in, then press START"); }
            else if (el() >= 1000u) finish(KS_FAIL, 0, "rail not ready");
        }
        else if (sub == 3 && ans == KA_OK) { out_rail(true); next_sub(4); }
        else if (sub == 4) {
            if (out_rail_ready()) {
                bool clear = !out_selftest_failed();
                finish(flag_a && clear ? KS_PASS : KS_FAIL, (flag_a ? 1 : 0) + (clear ? 2 : 0),
                       !flag_a ? "open NOT seen" : !clear ? "not clear after" : "seen + cleared");
                flag_a = false;
            } else if (el() >= 1000u) finish(KS_FAIL, 0, "rail not ready");
        }
        if (ans == KA_SKIP) { flag_a = false; finish(KS_SKIP, 0, "skipped"); }
        break;
    case K_VALVE_MAP:
        if (sub == 0) { if (water_gate()) break; yes_mask = 0; idx2 = 0; out_rail(true); next_sub(1); }
        if (sub == 1) { if (!out_rail_ready()) { if (el() >= 1500u) finish(KS_FAIL, 0, "rail not ready"); break; } next_sub(3); }
        if (sub == 3) {
            if (idx2 >= 11u) {
                char nbuf[16]; sb_t a; sb_init(&a, nbuf, sizeof nbuf);
                if (yes_mask == 0x7FFu) sb_str(&a, "all 11 ok");
                else { sb_str(&a, "NO:"); for (uint8_t i = 0; i < 11u && a.len < 13u; i++) if (!(yes_mask & (1u << i))) { sb_char(&a, ' '); if (i < 10u) { sb_char(&a, 'V'); sb_uint_w(&a, i + 1u, 0, ' '); } else sb_char(&a, 'P'); } }
                finish(yes_mask == 0x7FFu ? KS_PASS : KS_FAIL, yes_mask, nbuf);
                break;
            }
            if (!waiting) {
                out_all_off();
                if (idx2 < 10u) (void)out_set((uint16_t)(1u << idx2), OUT_ALLOW_TANK); else out_pump(true);
                char l1[21], u[90]; sb_t a;
                sb_init(&a, l1, sizeof l1); sb_str(&a, "ONLY ");
                if (idx2 < 10u) { sb_char(&a, 'V'); sb_uint_w(&a, idx2 + 1u, 0, ' '); } else sb_str(&a, "PUMP");
                sb_str(&a, " ON?");
                sb_init(&a, u, sizeof u); sb_str(&a, "Is ONLY ");
                if (idx2 < 10u) { sb_char(&a, 'V'); sb_uint_w(&a, idx2 + 1u, 0, ' '); } else sb_str(&a, "the pump");
                sb_str(&a, " acting (click / 24 V on its load)? START = yes, STOP = no");
                prompt(l1, "START=yes STOP=no", u);
            }
            if (ans == KA_OK || ans == KA_NO) {
                if (ans == KA_OK) yes_mask |= (uint16_t)(1u << idx2);
                idx2++; waiting = false; ans = KA_NONE;
            } else if (ans == KA_SKIP) finish(KS_SKIP, yes_mask, "skipped");
        }
        break;
    case K_TEMP_PROBE:
        if (s->temp_ok) finish(KS_PASS, s->temp_x10, "DS18B20");
        else if (el() >= 3000u) finish(KS_SKIP, 0, "no probe on J4");
        break;
    case K_HEAT: {
        static const uint16_t pattern = V1 | V3 | V4 | V6 | V7 | V9;   /* connector-check set */
        if (!s->temp_ok && sub == 0) { finish(KS_SKIP, 0, "no probe on J4"); break; }
        if (sub == 0) { if (water_gate()) break; worst = s->temp_x10; idx2 = 0; out_rail(true); next_sub(2); }
        if (sub == 2) { if (!out_rail_ready()) { if (el() >= 1500u) finish(KS_FAIL, 0, "rail not ready"); break; } next_sub(3); }
        if (sub >= 3u) {
            if (s->temp_ok && s->temp_x10 > worst) worst = s->temp_x10;
            if (out_fault() || bsp_valve_nfault()) { finish(KS_FAIL, worst, "driver tripped"); break; }
            bool on_phase = sub == 3u;
            if (on_phase && out_valves() != pattern) (void)out_set(pattern, 0);
            if (!on_phase && out_valves()) out_all_off();
            { char l[21]; sb_t a; sb_init(&a, l, sizeof l); sb_str(&a, "HEAT "); sb_uint_w(&a, idx2 + 1u, 0, ' '); sb_str(&a, "/5 ");
              sb_str(&a, on_phase ? "ON " : "off "); sb_fixed1(&a, s->temp_x10); sb_char(&a, 'C'); lcd_fmt(lcd1, l); lcd_fmt(lcd2, "6 valves, 20 s each"); }
            if (on_phase && el() >= 20000u) next_sub(4);
            else if (!on_phase && el() >= 10000u) { idx2++; if (idx2 >= 5u) {
                    sb_init(&ns, nb, sizeof nb); sb_str(&ns, "hold="); sb_uint_w(&ns, store_settings()->drv_hold, 0, ' ');
                    finish(worst <= 850 ? KS_PASS : KS_FAIL, worst, nb); } else next_sub(3); }
        }
        if (ans == KA_SKIP) finish(KS_SKIP, worst, "skipped");
        break; }

    case K_S2: case K_S3: {
        bool s2 = R.cur == K_S2;
        static const char *const v2[3] = { "0.50 V", "1.83 V", "4.50 V" };
        static const char *const v3[3] = { "0.50 V", "1.00 V", "4.50 V" };
        static const int32_t e2[3] = { -7500, 0, 15000 }, e3[3] = { 0, 218, 1740 };
        static const int32_t t2[3] = { 400, 400, 400 }, t3[3] = { 10, 10, 30 };
        if (sub == 0) { worst = 0; idx2 = 0; flag_a = true; next_sub(1); }
        if (idx2 >= 3u) { finish(flag_a ? KS_PASS : KS_FAIL, worst, flag_a ? "3 points ok" : "out of tol"); break; }
        if (sub == 1 && !waiting) {
            char l1[21], u[90]; sb_t a;
            sb_init(&a, l1, sizeof l1); sb_str(&a, s2 ? "S2 POT " : "S3 POT "); sb_str(&a, s2 ? v2[idx2] : v3[idx2]);
            sb_init(&a, u, sizeof u); sb_str(&a, "Set the "); sb_str(&a, s2 ? "S2" : "S3"); sb_str(&a, " potentiometer wiper to "); sb_str(&a, s2 ? v2[idx2] : v3[idx2]);
            sb_str(&a, " (meter), then press START");
            prompt(l1, "then START", u);
        }
        if (sub == 1 && ans == KA_OK) next_sub(2);
        if (sub == 2) {
            int32_t m;
            if (average(s2 ? sensors_bus_uncal_x10() : sensors_supply_uncal_x10(), &m)) {
                int32_t e = iabs(m - (s2 ? e2[idx2] : e3[idx2]));
                if (e > worst) worst = e;
                if (e > (s2 ? t2[idx2] : t3[idx2])) flag_a = false;
                idx2++; next_sub(1);
            }
        }
        if (ans == KA_SKIP) { finish(KS_SKIP, 0, "skipped"); }
        break; }
    case K_S2_OPEN:
        if (sub == 0) { next_sub(1); prompt("S2 POT TO 0 V", "then START", "Turn the S2 potentiometer to 0 V (open-wire test), then press START"); }
        else if (sub == 1 && ans == KA_OK) next_sub(2);
        else if (sub == 2 && el() >= 1000u) finish(!s->p_bus_ok ? KS_PASS : KS_FAIL, 0, !s->p_bus_ok ? "fault seen" : "NOT flagged");
        if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        break;
    case K_S6: case K_S7: case K_S8: {
        const char *nm = R.cur == K_S6 ? "S6" : R.cur == K_S7 ? "S7" : "S8";
        bool st = R.cur == K_S6 ? s->dialyser : R.cur == K_S7 ? s->conc_low : s->liquid;
        if (sub == 0) { next_sub(1); }
        if (sub == 1 && !waiting) { char l1[21], u[60]; sb_t a; sb_init(&a, l1, sizeof l1); sb_str(&a, "CLOSE "); sb_str(&a, nm);
            sb_init(&a, u, sizeof u); sb_str(&a, "Close switch "); sb_str(&a, nm); sb_str(&a, " (the test sees it by itself)"); prompt(l1, "", u); }
        if (sub == 1 && st) { next_sub(2); }
        if (sub == 2 && !waiting) { char l1[21], u[60]; sb_t a; sb_init(&a, l1, sizeof l1); sb_str(&a, "OPEN "); sb_str(&a, nm);
            sb_init(&a, u, sizeof u); sb_str(&a, "Open switch "); sb_str(&a, nm); prompt(l1, "", u); }
        if (sub == 2 && !st) finish(KS_PASS, 1, "closed+open");
        else if (el() >= 60000u) finish(KS_FAIL, 0, sub == 1 ? "close not seen" : "open not seen");
        if (ans == KA_SKIP || ans == KA_NO) finish(ans == KA_SKIP ? KS_SKIP : KS_FAIL, 0, ans == KA_SKIP ? "skipped" : "technician: no");
        break; }
    case K_FLOW:
        if (sub == 0) { next_sub(1); prompt("PULSES 75 Hz", "then START", "Set the flow-pulse source to 75 Hz, then press START"); }
        else if (sub == 1 && ans == KA_OK) { next_sub(2); lcd_fmt(lcd1, "MEASURING FLOW"); lcd_fmt(lcd2, ""); }
        else if (sub == 2 && el() >= 2500u) finish(iabs(s->flow_x10 - 100) <= 3 ? KS_PASS : KS_FAIL, s->flow_x10, "at 75 Hz");
        if (ans == KA_SKIP) finish(KS_SKIP, 0, "skipped");
        break;
    case K_PUMP:
        if (sub == 0) { out_rail(true); next_sub(1); lcd_fmt(lcd1, "PUMP ON 2 s"); lcd_fmt(lcd2, ""); }
        else if (sub == 1) {
            if (!out_rail_ready()) { if (el() >= 1500u) finish(KS_FAIL, 0, "rail not ready"); break; }
            out_pump(true); next_sub(2);
        } else if (sub == 2 && el() >= 2000u) {
            int32_t ma = (int32_t)s->pump_ma;
            finish(ma >= PUMP_OPEN_MA ? KS_PASS : KS_FAIL, ma, ma >= PUMP_OPEN_MA ? "pump draws" : "open wire?");
        }
        break;
    default: finish(KS_SKIP, 0, "?"); break;
    }
}

/* ------------------------------------------------------------------ sequencing */
static bool in_run(uint8_t k) { return R.stage == 0u || defs[k].stage == R.stage; }

static void end_test(const char *why)
{
    char b[120];
    out_safe();
    R.active = 0;
    save();
    led_drive = false;
    waiting = false; pcode[0] = '\0';
    kittest_summary(b, sizeof b);
    say(b);
    if (why) { char w[80]; sb_t s; sb_init(&s, w, sizeof w); sb_str(&s, "KITTEST END,"); sb_str(&s, why); say(w); }
    else say("KITTEST END");
}

static void start_item(void)
{
    char b[80]; sb_t s; char c[6];
    code_of(R.cur, c);
    sb_init(&s, b, sizeof b); sb_str(&s, "ITEM,"); sb_str(&s, c); sb_char(&s, ','); sb_str(&s, defs[R.cur].name); say(b);
    R.it[R.cur].run = 1;
    lcd_fmt(lcd1, defs[R.cur].name); lcd_fmt(lcd2, "");
    flag_a = false;
    next_sub(0);
}

static void advance(void)
{
    save();
    do { R.cur++; } while (R.cur < K_COUNT && !in_run(R.cur));
    if (R.cur >= K_COUNT) end_test(NULL);
    else start_item();
}

bool kittest_active(void) { return R.active != 0u; }
const char *kittest_prompt_code(void) { return waiting ? pcode : ""; }
void kittest_answer(kt_answer_t a) { if (R.active) ans = a; }

void kittest_key(uint8_t key)
{
    if (!R.active) return;
    if (R.cur == K_KEYS && sub == 1u) { key_seen |= key; return; }
    if (key == KEY_START || key == KEY_ENTER) ans = KA_OK;
    else if (key == KEY_STOP) ans = KA_NO;
}

bool kittest_leds(bool *g, bool *r)
{
    if (!R.active) return false;
    if (led_drive) { *g = led_g; *r = led_r; }
    else { *g = (now_ms / 500u) % 2u == 0u; *r = false; }      /* green blinks while a test runs */
    return true;
}

void kittest_lcd(char rows[4][20])
{
    char b[21]; sb_t s; char c[6];
    code_of(R.cur < K_COUNT ? R.cur : 0, c);
    sb_init(&s, b, sizeof b); sb_str(&s, "KIT "); sb_str(&s, c); sb_char(&s, ' ');
    sb_str(&s, R.cur < K_COUNT ? defs[R.cur].name : "");
    lcd_fmt(rows[0], b);
    lcd_fmt(rows[1], "");
    memcpy(rows[2], lcd1, 20);
    memcpy(rows[3], lcd2, 20);
    if (R.cur == K_LCD && R.active) { lcd_fmt(rows[1], "ABCDEFGHIJKLMNOPQRST"); lcd_fmt(rows[2], "abcdefghij0123456789"); }
}

bool kittest_start(uint8_t stage, bool quick, bool longer, char *why, uint32_t why_len)
{
    sb_t s; sb_init(&s, why, why_len);
    if (R.active) { sb_str(&s, "already running"); return false; }
    if (cycle_busy()) { sb_str(&s, "a program is running"); return false; }
    if (!post_done()) { sb_str(&s, "self-test running"); return false; }
    if (stage > 5u) { sb_str(&s, "stage 1-5 or all"); return false; }
    memset(&R, 0, sizeof R);
    R.active = 1; R.stage = stage; R.mode = quick ? 1u : longer ? 2u : 0u;
    R.start_time = bsp_rtc_get();
    out_safe();
    char b[120]; sb_t h; sb_init(&h, b, sizeof b);
    sb_str(&h, "KITTEST START,"); sb_str(&h, FW_NAME " " FW_VERSION_STR ","); sb_str(&h, store_settings()->serial[0] ? store_settings()->serial : "no-serial");
    sb_str(&h, ",stage "); if (stage) sb_uint_w(&h, stage, 0, ' '); else sb_str(&h, "all");
    sb_str(&h, R.mode == 1u ? ",quick" : R.mode == 2u ? ",long" : ",normal");
    say(b);
    R.cur = 0;
    while (R.cur < K_COUNT && !in_run(R.cur)) R.cur++;
    start_item();
    save();
    return true;
}

void kittest_abort(void)
{
    if (!R.active) return;
    if (R.cur < K_COUNT && R.it[R.cur].st == KS_NONE) { R.it[R.cur].st = KS_FAIL; memcpy(R.it[R.cur].note, "aborted", 8); }
    end_test("aborted");
}

void kittest_init(kt_print_t print)
{
    out = print;
    resumed_keep = false;
    if (!store_kt_read(&R, sizeof R) || R.magic != KT_MAGIC || R.crc != crc16_ccitt(&R, offsetof(krec_t, crc), 0xFFFFu)) {
        memset(&R, 0, sizeof R);
        return;
    }
    if (!R.active) return;
    if (R.cur == K_FRAM_KEEP && R.token != 0u && store_settings()->boot_count > R.boot_at_keep) {
        resumed_keep = true;           /* the record, with its token, survived the power cycle */
        say("KITTEST RESUME,power cycle seen");
        start_item();
        return;
    }
    if (R.cur < K_COUNT) { R.it[R.cur].st = KS_FAIL; memset(R.it[R.cur].note, 0, sizeof R.it[R.cur].note); memcpy(R.it[R.cur].note, "reset during", 12); }
    end_test("controller reset during the test");
}

void kittest_tick(uint32_t now)
{
    now_ms = now;
    if (!R.active || !post_done()) return;
    if (sub == 0xFFu || sub == 0xFEu) { advance(); return; }
    run_item();
}

/* ------------------------------------------------------------------ report */
uint32_t kittest_items(void) { return K_COUNT; }

bool kittest_report_line(uint32_t i, char *buf, uint32_t len)
{
    if (i >= K_COUNT || !R.it[i].run) return false;
    static const char *const stn[5] = { "-", "PASS", "FAIL", "SKIP", "INFO" };
    const kitem_t *it = &R.it[i];
    const kdef_t *d = &defs[i];
    sb_t s; char c[6]; code_of((uint8_t)i, c);
    sb_init(&s, buf, len);
    sb_str(&s, "K,"); sb_str(&s, c); sb_char(&s, ','); sb_str(&s, d->name); sb_char(&s, ',');
    sb_str(&s, it->st < 5u ? stn[it->st] : "?"); sb_char(&s, ',');
    switch (d->fmt) {
    case F_X10: sb_fixed1(&s, it->val); break;
    case F_HEX: { static const char hx[] = "0123456789ABCDEF"; sb_str(&s, "0x"); for (int k = 28; k >= 0; k -= 4) sb_char(&s, hx[((uint32_t)it->val >> k) & 0xFu]); } break;
    case F_MASK: { static const char hx[] = "0123456789ABCDEF"; sb_str(&s, "0x"); for (int k = 8; k >= 0; k -= 4) sb_char(&s, hx[((uint32_t)it->val >> k) & 0xFu]); } break;
    default: sb_int(&s, it->val); break;
    }
    sb_str(&s, d->unit); sb_char(&s, ','); sb_str(&s, d->limit); sb_char(&s, ',');
    char n[17]; memcpy(n, it->note, 16); n[16] = '\0'; sb_str(&s, n);
    return true;
}

void kittest_summary(char *buf, uint32_t len)
{
    uint32_t p = 0, f = 0, k = 0, n = 0;
    for (uint32_t i = 0; i < K_COUNT; i++) {
        if (!R.it[i].run) continue;
        n++;
        if (R.it[i].st == KS_PASS || R.it[i].st == KS_INFO) p++;
        else if (R.it[i].st == KS_FAIL || R.it[i].st == KS_NONE) f++;
        else k++;
    }
    sb_t s; sb_init(&s, buf, len);
    sb_str(&s, "SUMMARY,"); sb_str(&s, f ? "FAIL" : "PASS"); sb_char(&s, ',');
    sb_uint_w(&s, p, 0, ' '); sb_str(&s, " pass "); sb_uint_w(&s, f, 0, ' '); sb_str(&s, " fail "); sb_uint_w(&s, k, 0, ' ');
    sb_str(&s, " skipped of "); sb_uint_w(&s, n, 0, ' ');
    sb_str(&s, ",serial "); sb_str(&s, store_settings()->serial[0] ? store_settings()->serial : "-");
}
