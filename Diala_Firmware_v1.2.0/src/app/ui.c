#include "ui.h"
#include "config.h"
#include "cycle.h"
#include "sensors.h"
#include "outputs.h"
#include "post.h"
#include "kittest.h"
#include "store.h"
#include "alarms.h"
#include "util.h"
#include "../bsp/bsp.h"
#include <string.h>

typedef enum {
    SCR_SPLASH, SCR_POST, SCR_HOME, SCR_ID, SCR_KNOWN, SCR_TYPE, SCR_PRIME, SCR_CONFIRM, SCR_REFUSED, SCR_CYCLE,
    SCR_PIN, SCR_MENU, SCR_SENS, SCR_VALVE, SCR_NUM, SCR_MSG, SCR_SVC_CONFIRM, SCR_POST_SVC
} screen_t;

typedef enum { NUM_SPAN, NUM_WF, NUM_CLOCK } num_purpose_t;

typedef struct { uint8_t d[10]; uint8_t n; uint8_t cur; } numed_t;

static screen_t scr;
static uint32_t scr_t, now_ms;
static program_t sel_prog;
static uint8_t sel_type = DT_LOW;
static numed_t ed;
static cycle_req_t creq;
static dlz_entry_t known;
static alarm_t refused;
static uint8_t menu_i;
static uint8_t valve_i;
static uint32_t valve_on_t;
static num_purpose_t num_purpose;
static const char *msg_text;
static char rows[4][20];

enum { MI_SENS, MI_VALVE, MI_DRAIN, MI_FILL, MI_PRIME, MI_SEAL, MI_ZERO, MI_SPAN, MI_PZERO, MI_WF,
       MI_CLOCK, MI_MONITOR, MI_SELFTEST, MI_CLEARLOG, MI_EXIT };
static const char *const menu_items[] = {
    "SENSORS", "VALVE TEST", "DRAIN TANK", "FILL TANK 400 g", "PRIME LINES", "SEAL TEST (capped)",
    "SCALE ZERO", "SCALE SPAN", "PRESSURE ZERO", "LINE VOLUME WF",
    "SET CLOCK", "DRIVER MONITOR", "SELF TEST RESULT", "CLEAR LOGS", "EXIT"
};
#define MENU_N (sizeof menu_items / sizeof menu_items[0])

bool ui_in_service(void) { return scr >= SCR_PIN; }
static void render(void);

static void go(screen_t s) { scr = s; scr_t = now_ms; }

static void ed_init(uint8_t n, uint32_t value)
{
    memset(&ed, 0, sizeof ed);
    ed.n = n;
    for (int8_t i = (int8_t)(n - 1u); i >= 0; i--) { ed.d[i] = (uint8_t)(value % 10u); value /= 10u; }
}

static uint32_t ed_value(void)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < ed.n; i++) v = v * 10u + ed.d[i];
    return v;
}

/* returns true when the last digit was confirmed */
static bool ed_key(uint8_t key)
{
    if (key == KEY_UP)   ed.d[ed.cur] = (uint8_t)((ed.d[ed.cur] + 1u) % 10u);
    if (key == KEY_DOWN) ed.d[ed.cur] = (uint8_t)((ed.d[ed.cur] + 9u) % 10u);
    if (key == KEY_ENTER || key == KEY_START) {
        if (ed.cur + 1u >= ed.n) return true;
        ed.cur++;
    }
    return false;
}

static void ed_render(char row[20])
{
    char b[21]; sb_t s; sb_init(&s, b, sizeof b);
    for (uint8_t i = 0; i < ed.n; i++) sb_char(&s, (char)('0' + ed.d[i]));
    sb_pad(&s, 20);
    lcd_fmt(row, b);
    if (ed.cur < ed.n && (now_ms / 400u) % 2u) row[ed.cur] = '_';
}

static void line(uint8_t r, const char *t) { lcd_fmt(rows[r], t); }

void ui_init(void)
{
    now_ms = bsp_millis();
    sel_prog = PROG_LOW;
    go(SCR_SPLASH);
}

static void start_cycle(void)
{
    alarm_t a = cycle_start(&creq);
    if (a == ALM_NONE) go(SCR_CYCLE);
    else { refused = a; alarm_beep(300); go(SCR_REFUSED); }
}

static void prog_selected(void)
{
    memset(&creq, 0, sizeof creq);
    creq.prog = sel_prog;
    if (sel_prog <= PROG_HIGH) { creq.dtype = program_dtype(sel_prog); ed_init(8, 0); go(SCR_ID); }
    else if (sel_prog == PROG_PRECLEAN) { go(SCR_TYPE); }
    else go(SCR_CONFIRM);
}

static void svc_program(program_t p)
{
    memset(&creq, 0, sizeof creq);
    creq.prog = p;
    go(SCR_SVC_CONFIRM);
}

static void service_action(void)
{
    cal_t *cal = store_cal();
    const sensors_t *s = sensors();
    switch (menu_i) {
    case MI_SENS:  go(SCR_SENS); break;
    case MI_VALVE: valve_i = 0; out_rail(true); go(SCR_VALVE); break;
    case MI_DRAIN: svc_program(PROG_SVC_DRAIN); break;
    case MI_FILL:  svc_program(PROG_SVC_FILL); break;
    case MI_PRIME: svc_program(PROG_SVC_PRIME); break;
    case MI_SEAL:  svc_program(PROG_SVC_SEAL); break;
    case MI_ZERO:  cal->scale_zero = s->scale_raw; msg_text = store_save_cal() ? "SCALE ZERO SAVED" : "SAVE FAILED"; go(SCR_MSG); break;
    case MI_SPAN:  num_purpose = NUM_SPAN; ed_init(4, 500); go(SCR_NUM); break;
    case MI_PZERO: cal->s2_zero_x10 = (int16_t)sensors_bus_uncal_x10(); cal->s3_zero_x10 = (int16_t)sensors_supply_uncal_x10();
                   msg_text = store_save_cal() ? "PRESSURE ZERO SAVED" : "SAVE FAILED"; go(SCR_MSG); break;
    case MI_WF:    num_purpose = NUM_WF; ed_init(4, (uint32_t)(cal->wf_x10 > 0 ? cal->wf_x10 : 0)); go(SCR_NUM); break;
    case MI_CLOCK: { uint16_t y; uint8_t mo, d, h, mi, se; date_from_secs(bsp_rtc_get(), &y, &mo, &d, &h, &mi, &se);
              num_purpose = NUM_CLOCK; ed_init(10, 0);
              uint32_t parts[5] = { (uint32_t)(y % 100u), mo, d, h, mi };
              for (uint8_t i = 0; i < 5u; i++) { ed.d[i * 2u] = (uint8_t)(parts[i] / 10u); ed.d[i * 2u + 1u] = (uint8_t)(parts[i] % 10u); }
              go(SCR_NUM); } break;
    case MI_MONITOR: { settings_t *st = store_settings(); st->drv_fault_monitor = st->drv_fault_monitor ? 0u : 1u;
              out_monitor_enable(st->drv_fault_monitor != 0u); (void)store_save_settings();
              msg_text = st->drv_fault_monitor ? "DRV MONITOR ON" : "DRV MONITOR OFF"; go(SCR_MSG); } break;
    case MI_SELFTEST: go(SCR_POST_SVC); break;
    case MI_CLEARLOG: store_log_clear(); msg_text = "LOGS CLEARED"; go(SCR_MSG); break;
    default: go(SCR_HOME); break;
    }
}

static void num_done(void)
{
    cal_t *cal = store_cal();
    uint32_t val = ed_value();
    if (num_purpose == NUM_SPAN) {
        int32_t counts = sensors()->scale_raw - cal->scale_zero;
        if (val > 0u && counts > 0) {
            cal->scale_cpg_x100 = (int32_t)((int64_t)counts * 100 / (int64_t)val);
            (void)store_save_cal(); msg_text = "SCALE SPAN SAVED";
        } else msg_text = "SPAN NOT VALID";
    } else if (num_purpose == NUM_WF) {
        cal->wf_x10 = (int16_t)clamp_i32((int32_t)val, 0, 3000);   /* entered as ml x10 */
        (void)store_save_cal(); msg_text = "LINE VOLUME SAVED";
    } else {
        uint8_t p[5];
        for (uint8_t i = 0; i < 5u; i++) p[i] = (uint8_t)(ed.d[i * 2u] * 10u + ed.d[i * 2u + 1u]);
        if (p[1] >= 1u && p[1] <= 12u && p[2] >= 1u && p[2] <= 31u && p[3] < 24u && p[4] < 60u) {
            bsp_rtc_set(secs_from_date((uint16_t)(2000u + p[0]), p[1], p[2], p[3], p[4], 0));
            msg_text = "CLOCK SET";
        } else msg_text = "DATE NOT VALID";
    }
    go(SCR_MSG);
}

void ui_key(uint8_t key, key_ev_t ev)
{
    if (kittest_active()) { if (ev == KEV_PRESS) kittest_key(key); return; }   /* the kit test owns the keypad */
    if (key == KEY_MUTE) { alarm_mute(); return; }
    const cycle_view_t *cv = cycle_view();

    switch (scr) {
    case SCR_SPLASH: if (post_done()) go((post_result() & POST_FAIL_MASK) ? SCR_POST : SCR_HOME); break;
    case SCR_POST:   if (key == KEY_ENTER || key == KEY_START || key == KEY_STOP) go(SCR_HOME); break;
    case SCR_POST_SVC: go(SCR_MENU); break;
    case SCR_SVC_CONFIRM:
        if (key == KEY_STOP) go(SCR_MENU);
        else if (key == KEY_START) start_cycle();
        break;
    case SCR_HOME:
        if (alarm_active() != ALM_NONE && key == KEY_ENTER && ev == KEV_PRESS) { alarm_ack(); break; }
        if (key == KEY_ENTER && ev == KEV_LONG) { ed_init(4, 0); go(SCR_PIN); break; }
        if (key == KEY_UP)   sel_prog = (program_t)((sel_prog + PROG_USER_COUNT - 1u) % PROG_USER_COUNT);
        if (key == KEY_DOWN) sel_prog = (program_t)((sel_prog + 1u) % PROG_USER_COUNT);
        /* START selects. ENTER is reserved for "clear alarm" and, held 3 s, the service PIN: in v1.0 a
         * short ENTER also selected the program, so the long press never reached this screen. */
        if (key == KEY_START && ev == KEV_PRESS) prog_selected();
        break;
    case SCR_ID:
        if (key == KEY_STOP) { go(SCR_HOME); break; }
        if (ed_key(key)) {
            creq.dlz_id = ed_value();
            if (creq.dlz_id == 0u) { ed_init(8, 0); break; }
            if (store_dlz_find(creq.dlz_id, &known) && known.baseline_x10 > 0u) {
                if (known.dtype >= DT_LOW && known.dtype <= DT_HIGH) creq.dtype = known.dtype;
                creq.prime_ml = 0; go(SCR_KNOWN);
            } else { ed_init(3, 120); go(SCR_PRIME); }
        }
        break;
    case SCR_KNOWN:
        if (key == KEY_STOP) { go(SCR_HOME); break; }
        if (key == KEY_START || key == KEY_ENTER) { ed_init(3, 120); go(SCR_PRIME); }
        break;
    case SCR_TYPE:
        if (key == KEY_STOP) { go(SCR_HOME); break; }
        if (key == KEY_UP)   sel_type = sel_type <= DT_LOW ? DT_HIGH : (uint8_t)(sel_type - 1u);
        if (key == KEY_DOWN) sel_type = sel_type >= DT_HIGH ? DT_LOW : (uint8_t)(sel_type + 1u);
        if (key == KEY_ENTER || key == KEY_START) { creq.dtype = sel_type; ed_init(3, 120); go(SCR_PRIME); }
        break;
    case SCR_PRIME:
        if (key == KEY_STOP) { go(SCR_HOME); break; }
        if (ed_key(key)) {
            uint32_t pv = ed_value();
            if (pv < 25u || pv > 250u) { ed_init(3, 120); alarm_beep(300); break; }
            creq.prime_ml = (uint16_t)pv;
            go(SCR_CONFIRM);
        }
        break;
    case SCR_CONFIRM:
        if (key == KEY_STOP) go(SCR_HOME);
        else if (key == KEY_START) start_cycle();
        break;
    case SCR_REFUSED:
        if (key == KEY_ENTER || key == KEY_STOP) { alarm_ack(); go(SCR_HOME); }
        break;
    case SCR_CYCLE:
        if (cv->state == CY_STRIP) {
            if (key == KEY_ENTER) cycle_strip(true);
            else if (key == KEY_STOP) cycle_strip(false);
        } else if (key == KEY_STOP) cycle_stop();
        else if (key == KEY_ENTER) cycle_enter();
        break;
    case SCR_PIN:
        if (key == KEY_STOP) { go(SCR_HOME); break; }
        if (ed_key(key)) {
            if (ed_value() == store_settings()->service_pin) { menu_i = 0; go(SCR_MENU); }
            else { alarm_beep(500); go(SCR_HOME); }
        }
        break;
    case SCR_MENU:
        if (key == KEY_STOP) { go(SCR_HOME); break; }
        if (key == KEY_UP)   menu_i = (uint8_t)((menu_i + MENU_N - 1u) % MENU_N);
        if (key == KEY_DOWN) menu_i = (uint8_t)((menu_i + 1u) % MENU_N);
        if (key == KEY_ENTER) service_action();
        break;
    case SCR_SENS:
        if (key == KEY_STOP || key == KEY_ENTER) go(SCR_MENU);
        break;
    case SCR_VALVE:
        if (key == KEY_UP)   { out_all_off(); valve_i = (uint8_t)((valve_i + 11u) % 12u); }
        if (key == KEY_DOWN) { out_all_off(); valve_i = (uint8_t)((valve_i + 1u) % 12u); }
        if (key == KEY_ENTER) {   /* one output at a time (FW-07) */
            out_all_off();
            if (!out_rail_ready()) { alarm_beep(200); }
            else if (valve_i < 10u) (void)out_set((uint16_t)(1u << valve_i), OUT_ALLOW_TANK);
            else if (valve_i == 10u) out_pump(true);
            else (void)out_set(SPARE_OUT, 0);
            valve_on_t = now_ms;
        }
        if (key == KEY_STOP) { out_safe(); go(SCR_MENU); }
        break;
    case SCR_NUM:
        if (key == KEY_STOP) { go(SCR_MENU); break; }
        if (ed_key(key)) num_done();
        break;
    case SCR_MSG:
        go(SCR_MENU);
        break;
    default: go(SCR_HOME); break;
    }
}

static void render(void)
{
    char b[21]; sb_t s;
    const sensors_t *ss = sensors();
    const cycle_view_t *cv = cycle_view();
    for (uint8_t r = 0; r < 4u; r++) lcd_fmt(rows[r], "");
    if (kittest_active()) { kittest_lcd(rows); return; }

    switch (scr) {
    case SCR_SPLASH:
        line(0, "DIALA REPROCESSOR"); line(1, "FW " FW_VERSION_STR); line(2, HW_TARGET_STR);
        line(3, post_done() ? "Morphedo" : "Self test...");
        break;
    case SCR_POST:
    case SCR_POST_SVC: {
        uint16_t pr = post_result();
        line(0, (pr & POST_FAIL_MASK) ? "SELF TEST: FAULT" : "SELF TEST: PASS");
        uint8_t r = 1;
        sb_init(&s, b, sizeof b);
        for (uint16_t bit = 1u; bit <= 0x400u && r < 3u; bit <<= 1) {
            if (!(pr & bit)) continue;
            const char *nm = post_item_name(bit);
            if (s.len && s.len + 1u + strlen(nm) > 20u) { line(r++, b); sb_init(&s, b, sizeof b); }
            if (s.len) sb_char(&s, ' ');
            sb_str(&s, nm);
        }
        if (s.len && r < 3u) line(r, b);
        line(3, scr == SCR_POST ? "ENTER=continue" : "any key");
        break; }
    case SCR_SVC_CONFIRM:
        sb_init(&s, b, sizeof b); sb_str(&s, "RUN "); sb_str(&s, program_name(creq.prog)); line(0, b);
        if (creq.prog == PROG_SVC_DRAIN)      { line(1, "Empties the mix tank"); line(2, "(also pulls port A)"); }
        else if (creq.prog == PROG_SVC_FILL)  { line(1, "Fills tank to 400 g"); line(2, "Tank must be empty"); }
        else if (creq.prog == PROG_SVC_PRIME) { line(1, "Fit DIALYSER or"); line(2, "BYPASS first!"); }
        else                                  { line(1, "Cap ports A V DI DO"); line(2, "No dialyser fitted"); }
        line(3, "START=go STOP=back"); break;
    case SCR_HOME: {
        alarm_t a = alarm_active();
        if (a != ALM_NONE) { line(0, alarm_text(a)); line(1, alarm_hint(a)); line(3, "MUTE=quiet ENT=clear"); break; }
        uint16_t y; uint8_t mo, d, h, mi, se; date_from_secs(bsp_rtc_get(), &y, &mo, &d, &h, &mi, &se);
        sb_init(&s, b, sizeof b); sb_str(&s, "DIALA READY  "); sb_uint_w(&s, h, 2, '0'); sb_char(&s, ':'); sb_uint_w(&s, mi, 2, '0'); line(0, b);
        sb_init(&s, b, sizeof b); sb_str(&s, "Tank "); sb_int(&s, sensors_weight_g()); sb_str(&s, "g Sup "); sb_fixed1(&s, ss->p_supply_x10); line(1, b);
        sb_init(&s, b, sizeof b); sb_str(&s, "> "); sb_str(&s, program_name(sel_prog)); line(2, b);
        line(3, store_settings()->germicide_in_lines ? "Lines: GERMICIDE" : "UP/DN select, START");
        break; }
    case SCR_ID:   line(0, "DIALYSER ID"); ed_render(rows[1]); line(3, "UP/DN digit ENT=next"); break;
    case SCR_KNOWN:
        sb_init(&s, b, sizeof b); sb_str(&s, "ID "); sb_uint_w(&s, creq.dlz_id, 8, '0'); line(0, b);
        sb_init(&s, b, sizeof b); sb_str(&s, dtype_name(known.dtype)); sb_str(&s, " uses "); sb_uint_w(&s, known.uses, 0, ' '); line(1, b);
        sb_init(&s, b, sizeof b); sb_str(&s, "Base TCV "); sb_fixed1(&s, known.baseline_x10); sb_str(&s, "ml"); line(2, b);
        line(3, "ENT=next STOP=back"); break;
    case SCR_TYPE:
        line(0, "DIALYSER TYPE");
        sb_init(&s, b, sizeof b); sb_str(&s, "> "); sb_str(&s, dtype_name(sel_type)); sb_str(&s, " FLUX"); line(1, b);
        line(3, "UP/DN  ENT=next"); break;
    case SCR_PRIME: line(0, "PRIMING VOLUME ml"); ed_render(rows[1]); line(2, "range 25-250"); line(3, "UP/DN digit ENT=next"); break;
    case SCR_CONFIRM:
        sb_init(&s, b, sizeof b); sb_str(&s, "RUN "); sb_str(&s, program_name(creq.prog)); line(0, b);
        if (creq.prog <= PROG_PRECLEAN) {
            sb_init(&s, b, sizeof b); sb_str(&s, "ID "); sb_uint_w(&s, creq.dlz_id, 8, '0'); line(1, b);
            sb_init(&s, b, sizeof b); sb_str(&s, dtype_name(creq.dtype)); sb_str(&s, " prime "); sb_uint_w(&s, creq.prime_ml, 0, ' '); sb_str(&s, "ml"); line(2, b);
        } else if (creq.prog == PROG_DISINFECT) line(1, "Fit BYPASS connector");
        else {
            line(1, "Bypass fitted?");
            uint32_t left = cycle_disinfect_hold_left_s();
            if (left) { sb_init(&s, b, sizeof b); sb_str(&s, "HOLD LEFT "); sb_uint_w(&s, left / 3600u, 0, ' '); sb_char(&s, 'h');
                        sb_uint_w(&s, (left % 3600u) / 60u, 2, '0'); sb_char(&s, 'm'); line(2, b); }
        }
        line(3, "START=go STOP=back"); break;
    case SCR_REFUSED: line(0, "CANNOT START"); line(1, alarm_text(refused)); line(2, alarm_hint(refused)); line(3, "ENTER=back"); break;
    case SCR_CYCLE:
        if (cv->state == CY_IDLE) { go(SCR_HOME); render(); return; }
        if (cv->state == CY_RUN || cv->state == CY_RECOVER) {
            sb_init(&s, b, sizeof b); sb_char(&s, 'S'); sb_uint_w(&s, cv->stage, 0, ' '); sb_char(&s, ' '); sb_str(&s, cv->step_name ? cv->step_name : ""); line(0, b);
            sb_init(&s, b, sizeof b); sb_str(&s, "t "); sb_uint_w(&s, cv->step_elapsed_ms / 1000u, 0, ' '); sb_char(&s, '/'); sb_uint_w(&s, cv->step_max_ms / 1000u, 0, ' ');
            sb_str(&s, "s  "); sb_uint_w(&s, cv->cycle_elapsed_s / 60u, 2, '0'); sb_char(&s, ':'); sb_uint_w(&s, cv->cycle_elapsed_s % 60u, 2, '0'); line(1, b);
            sb_init(&s, b, sizeof b); sb_str(&s, "Tank "); sb_fixed1(&s, ss->weight_x10); sb_str(&s, " g"); line(2, b);
            sb_init(&s, b, sizeof b); sb_str(&s, "Bus "); sb_int(&s, ss->p_bus_x10 / 10); sb_str(&s, " Sup "); sb_fixed1(&s, ss->p_supply_x10); line(3, b);
        } else if (cv->state == CY_HOLD) {
            line(0, cv->pass ? "RESULT: PASS" : "RESULT: FAIL");
            sb_init(&s, b, sizeof b); sb_str(&s, "TCV "); sb_fixed1(&s, cv->tcv_x10); sb_str(&s, "ml ");
            if (cv->first_use) sb_str(&s, "BASE"); else { sb_fixed1(&s, cv->pct_x10); sb_char(&s, '%'); } line(1, b);
            sb_init(&s, b, sizeof b); sb_str(&s, "Leak "); sb_fixed1(&s, cv->leak_x10); sb_str(&s, cv->leak_ok ? " OK" : " FAIL"); line(2, b);
            line(3, "Venting. Remove dlz");
        } else if (cv->state == CY_DISINF_HOLD) {
            line(0, "MACHINE DISINFECTED");
            sb_init(&s, b, sizeof b); sb_str(&s, "Hold >= "); sb_uint_w(&s, DISINFECT_HOLD_H, 0, ' '); sb_str(&s, " h"); line(1, b);
            line(2, "Then run RINSE-OUT"); line(3, "ENTER=ok");
        } else if (cv->state == CY_STRIP) {
            line(0, "RINSE-OUT DONE"); line(1, "Test strip negative?"); line(3, "ENT=yes STOP=no");
        } else {   /* CY_DONE */
            if (cv->aborted) { line(0, alarm_text(cv->alarm)); line(1, alarm_hint(cv->alarm)); }
            else if (cv->prog == PROG_SVC_SEAL) {
                line(0, cv->pass ? "SEAL TEST: PASS" : "SEAL TEST: FAIL");
                sb_init(&s, b, sizeof b);
                for (uint8_t i = 0; i < cv->seal_n && i < 3u; i++) { sb_uint_w(&s, i + 1u, 0, ' '); sb_char(&s, ':'); sb_fixed1(&s, cv->seal_x10[i]); sb_char(&s, ' '); }
                line(1, b);
                if (!cv->pass) {
                    bool f1 = cv->seal_x10[0] > SEAL_LIMIT_X10, f2 = cv->seal_x10[1] > SEAL_LIMIT_X10, f3 = cv->seal_x10[2] > SEAL_LIMIT_X10;
                    line(2, f3 ? "Check V2 V5 DI cap" : f2 ? "Check V6 / A cap" : f1 ? "Check V7 V10 / V cap" : "");
                } else line(2, "mmHg per 20 s");
            } else {
                sb_init(&s, b, sizeof b); sb_str(&s, program_name(cv->prog)); sb_str(&s, " DONE"); line(0, b);
                uint32_t n; const step_rec_t *t = cycle_step_log(&n);
                if (PROG_IS_SERVICE(cv->prog) && n > 0u && t[n - 1u].ms > 0u) {
                    const step_rec_t *r = &t[n - 1u];
                    int32_t dw = r->w1_x10 - r->w0_x10; if (dw < 0) dw = -dw;
                    sb_init(&s, b, sizeof b); sb_str(&s, "Tank "); sb_fixed1(&s, r->w1_x10); sb_str(&s, " g"); line(1, b);
                    sb_init(&s, b, sizeof b); sb_fixed1(&s, (int32_t)((int64_t)dw * 1000 / (int64_t)r->ms)); sb_str(&s, " g/s  ");
                    sb_fixed1(&s, (int32_t)(r->ms / 100u)); sb_str(&s, " s"); line(2, b);
                }
            }
            line(3, "ENTER=clear");
        }
        break;
    case SCR_PIN: line(0, "SERVICE PIN"); ed_render(rows[1]); break;
    case SCR_MENU:
        line(0, "SERVICE MENU");
        sb_init(&s, b, sizeof b); sb_str(&s, "> "); sb_str(&s, menu_items[menu_i]); line(1, b);
        line(3, "UP/DN ENT=go STOP=x"); break;
    case SCR_SENS:
        sb_init(&s, b, sizeof b); sb_str(&s, "W "); sb_fixed1(&s, ss->weight_x10); sb_str(&s, " raw "); sb_int(&s, ss->scale_raw / 100); line(0, b);
        sb_init(&s, b, sizeof b); sb_str(&s, "Bus "); sb_fixed1(&s, ss->p_bus_x10); sb_str(&s, " Su "); sb_fixed1(&s, ss->p_supply_x10); line(1, b);
        sb_init(&s, b, sizeof b); sb_str(&s, "24V "); sb_fixed1(&s, (int32_t)(ss->v24_mv / 100u)); sb_str(&s, " P "); sb_uint_w(&s, ss->pump_ma, 0, ' '); sb_str(&s, "mA ");
        if (ss->temp_ok) { sb_fixed1(&s, ss->temp_x10); sb_char(&s, 'C'); } line(2, b);
        sb_init(&s, b, sizeof b); sb_str(&s, "F "); sb_fixed1(&s, ss->flow_x10); sb_str(&s, ss->liquid ? " LIQ" : " -"); sb_str(&s, ss->dialyser ? " DLZ" : " -");
        sb_str(&s, ss->conc_low ? " LOW" : " ok"); line(3, b);
        break;
    case SCR_VALVE:
        line(0, "VALVE TEST 1 AT TIME");
        sb_init(&s, b, sizeof b); sb_str(&s, "> ");
        if (valve_i < 10u) { sb_char(&s, 'V'); sb_uint_w(&s, valve_i + 1u, 0, ' '); } else sb_str(&s, valve_i == 10u ? "PUMP P1" : "SPARE");
        sb_str(&s, (out_valves() || out_pump_on()) ? "  ON" : "  off"); line(1, b);
        sb_init(&s, b, sizeof b); sb_str(&s, out_rail_ready() ? "rail " : "rail.. "); sb_uint_w(&s, ss->vrail_mv / 1000u, 0, ' ');
        sb_str(&s, "V "); sb_str(&s, bsp_valve_nfault() ? "nFLT LOW" : "nFLT ok"); line(2, b);
        line(3, "ENT=on STOP=exit"); break;
    case SCR_NUM:
        line(0, num_purpose == NUM_SPAN ? "REF WEIGHT g" : num_purpose == NUM_WF ? "WF ml x10" : "YYMMDDhhmm");
        ed_render(rows[1]); line(3, "UP/DN ENT=next"); break;
    case SCR_MSG: line(0, msg_text ? msg_text : ""); line(3, "any key"); break;
    }
}

void ui_tick(uint32_t now)
{
    now_ms = now;
    if (scr == SCR_SPLASH && post_done() && elapsed(now, scr_t) >= 1500u) go((post_result() & POST_FAIL_MASK) ? SCR_POST : SCR_HOME);
    if (scr == SCR_MSG && elapsed(now, scr_t) >= 2000u) go(SCR_MENU);
    if (scr == SCR_VALVE && (out_valves() || out_pump_on()) && elapsed(now, valve_on_t) >= SERVICE_VALVE_MAX_MS) out_all_off();
    if ((scr == SCR_HOME || scr == SCR_POST) && cycle_busy()) go(SCR_CYCLE);    /* e.g. started from the CLI */
    render();
    for (uint8_t r = 0; r < 4u; r++) bsp_lcd_row(r, rows[r]);
}

void ui_render(char out[4][20]) { render(); memcpy(out, rows, sizeof rows); }
