/* Service command line on the USB-C port (CH340C, 115200 8N1). Type "help".
 * v1.1: output never blocks. Long listings (log, steps) are produced a few lines per main-loop
 * pass only while the UART ring has room, so a dump can run during a cycle without starving the
 * sequencer or the watchdogs. */
#include "cli.h"
#include "config.h"
#include "cycle.h"
#include "sensors.h"
#include "outputs.h"
#include "post.h"
#include "events.h"
#include "kittest.h"
#include "store.h"
#include "alarms.h"
#include "ui.h"
#include "util.h"
#include "../bsp/bsp.h"
#include <string.h>

#define CMD_ROOM   700u    /* free TX bytes needed before a command is executed  */
#define LINE_ROOM  200u    /* free TX bytes needed for one more listing line     */

static char line_buf[64];
static uint8_t line_len;
static bool unlocked;
static bool trace_on;
static uint32_t trace_t;
static uint32_t dropped;

typedef enum { JOB_NONE, JOB_LOG, JOB_STEPS, JOB_EVENTS, JOB_KT } job_kind_t;
static bool events_on;
static job_kind_t job;
static int32_t job_i;

static void out(const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (bsp_uart_write(s, n) != n) dropped++;
}
static void outln(const char *s) { out(s); out("\r\n"); }
void cli_line(const char *s) { outln(s); }

static const char hexd[] = "0123456789ABCDEF";
static void sb_hex(sb_t *s, uint32_t v, uint8_t digits)
{
    for (int8_t i = (int8_t)(digits - 1u); i >= 0; i--) sb_char(s, hexd[(v >> (4u * (uint8_t)i)) & 0xFu]);
}

static bool parse_u32(const char *s, uint32_t *v)
{
    uint32_t x = 0; bool any = false;
    while (*s >= '0' && *s <= '9' && x < 100000000u) { x = x * 10u + (uint32_t)(*s - '0'); s++; any = true; }
    *v = x;
    return any && *s == '\0';
}

static uint8_t split(char *s, char *argv[], uint8_t max)
{
    uint8_t n = 0;
    while (*s && n < max) {
        while (*s == ' ') *s++ = '\0';
        if (!*s) break;
        argv[n++] = s;
        while (*s && *s != ' ') s++;
    }
    return n;
}

static void print_time(sb_t *s, uint32_t t)
{
    uint16_t y; uint8_t mo, d, h, mi, se;
    date_from_secs(t, &y, &mo, &d, &h, &mi, &se);
    sb_uint_w(s, y, 4, '0'); sb_char(s, '-'); sb_uint_w(s, mo, 2, '0'); sb_char(s, '-'); sb_uint_w(s, d, 2, '0');
    sb_char(s, ' '); sb_uint_w(s, h, 2, '0'); sb_char(s, ':'); sb_uint_w(s, mi, 2, '0'); sb_char(s, ':'); sb_uint_w(s, se, 2, '0');
}

static void sb_valves(sb_t *s, uint16_t m)
{
    bool any = false;
    for (uint8_t i = 0; i < VALVE_COUNT; i++) if (m & (1u << i)) { if (any) sb_char(s, ' '); sb_char(s, 'V'); sb_uint_w(s, i + 1u, 0, ' '); any = true; }
    if (!any) sb_char(s, '-');
}

static void event_line(const event_t *e)
{
    char b[120]; sb_t s; sb_init(&s, b, sizeof b);
    sb_str(&s, "E,"); sb_uint_w(&s, e->ms / 1000u, 0, ' '); sb_char(&s, '.'); sb_uint_w(&s, e->ms % 1000u, 3, '0');
    sb_char(&s, ','); sb_str(&s, ev_name(e->type)); sb_char(&s, ',');
    switch (e->type) {
    case EV_ALARM:  sb_str(&s, alarm_text((alarm_t)e->a)); break;
    case EV_KEY:    { static const char *const kn[6] = { "START", "STOP", "MUTE", "UP", "DOWN", "ENTER" };
                      for (uint8_t i = 0; i < 6u; i++) if (e->a & (1u << i)) sb_str(&s, kn[i]); } break;
    case EV_INPUT:  sb_char(&s, 'S'); sb_uint_w(&s, e->a, 0, ' '); sb_str(&s, e->b ? " on" : " off"); break;
    case EV_RAIL:   sb_str(&s, e->a == 0u ? "off" : e->a == 1u ? "ready, driver check ok" : "ready, driver check FAILED");
                    if (e->a) { sb_char(&s, ','); sb_uint_w(&s, (uint32_t)e->b, 0, ' '); sb_str(&s, " mV"); } break;
    case EV_VALVES: sb_valves(&s, e->a); break;
    case EV_PUMP:   sb_str(&s, e->a ? "on" : "off"); break;
    case EV_PROG:   sb_str(&s, program_cli_name((program_t)e->a)); sb_str(&s, e->b ? " start" : " end"); break;
    case EV_STEP:   { uint32_t n; const step_rec_t *t = cycle_step_log(&n);
                      sb_uint_w(&s, e->a + 1u, 0, ' '); sb_char(&s, ' '); if (e->a < n && t[e->a].name) sb_str(&s, t[e->a].name);
                      sb_char(&s, ','); sb_str(&s, step_end_name((uint8_t)e->b)); } break;
    case EV_TEMP:   sb_fixed1(&s, e->b); sb_str(&s, " C"); break;
    case EV_KT:     { static const char *const st[5] = { "-", "PASS", "FAIL", "SKIP", "INFO" }; sb_str(&s, "item ");
                      sb_uint_w(&s, e->a, 0, ' '); sb_char(&s, ' '); sb_str(&s, e->b >= 0 && e->b < 5 ? st[e->b] : "?"); } break;
    case EV_POST:   sb_str(&s, (e->a & POST_FAIL_MASK) ? "FAIL 0x" : "PASS 0x"); sb_hex(&s, e->a, 4); break;
    case EV_BOOT:   sb_str(&s, "boot "); sb_uint_w(&s, (uint32_t)e->b, 0, ' '); sb_str(&s, " reset 0x"); sb_hex(&s, e->a, 2); break;
    default: sb_int(&s, (int32_t)e->a); sb_char(&s, ','); sb_int(&s, e->b); break;
    }
    outln(b);
}

static void on_event(const event_t *e)
{
    if ((events_on || kittest_active()) && bsp_uart_tx_free() >= LINE_ROOM) event_line(e);
}

static void cmd_status(void)
{
    char b[110]; sb_t s;
    const sensors_t *ss = sensors();
    const cycle_view_t *cv = cycle_view();
    sb_init(&s, b, sizeof b); sb_str(&s, "state="); sb_uint_w(&s, cv->state, 0, ' '); sb_str(&s, " prog="); sb_str(&s, program_cli_name(cv->prog));
    sb_str(&s, " stage="); sb_uint_w(&s, cv->stage, 0, ' ');
    sb_str(&s, " step="); sb_str(&s, cv->step_name ? cv->step_name : "-"); sb_str(&s, " alarm="); sb_str(&s, alarm_text(alarm_active())); outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, "weight_g="); sb_fixed1(&s, ss->weight_x10); sb_str(&s, " bus_mmHg="); sb_fixed1(&s, ss->p_bus_x10);
    sb_str(&s, " supply_psig="); sb_fixed1(&s, ss->p_supply_x10); sb_str(&s, " flow_lpm="); sb_fixed1(&s, ss->flow_x10); outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, "v24_mV="); sb_uint_w(&s, ss->v24_mv, 0, ' '); sb_str(&s, " v5_mV="); sb_uint_w(&s, ss->v5_mv, 0, ' ');
    sb_str(&s, " vrail_mV="); sb_uint_w(&s, ss->vrail_mv, 0, ' '); sb_str(&s, " pump_mA="); sb_uint_w(&s, ss->pump_ma, 0, ' '); outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, "valves=0x"); sb_hex(&s, out_valves(), 4); sb_str(&s, " (");
    sb_valves(&s, out_valves()); sb_char(&s, ')');
    sb_str(&s, ss->liquid ? " S8=liquid" : " S8=dry"); sb_str(&s, ss->dialyser ? " S6=fitted" : " S6=none");
    sb_str(&s, ss->conc_low ? " S7=LOW" : " S7=ok"); sb_str(&s, bsp_valve_nfault() ? " nFAULT=low" : " nFAULT=high"); outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, store_settings()->germicide_in_lines ? "lines=germicide" : "lines=clean");
    sb_str(&s, " tmo_pct="); sb_uint_w(&s, store_tmo_pct(), 0, ' ');
    uint32_t left = cycle_disinfect_hold_left_s();
    if (left) { sb_str(&s, " disinfect_hold_left_min="); sb_uint_w(&s, (left + 59u) / 60u, 0, ' '); }
    sb_str(&s, " driver_temp_C="); if (ss->temp_ok) sb_fixed1(&s, ss->temp_x10); else sb_str(&s, "none");
    outln(b);
}

static void cmd_diag(void)
{
    char b[110]; sb_t s;
    bsp_diag_t d; bsp_diag(&d);
    uint16_t pr = post_result();
    sb_init(&s, b, sizeof b); sb_str(&s, FW_NAME " " FW_VERSION_STR " / " HW_TARGET_STR " / built " __DATE__); outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, "clock="); sb_str(&s, d.hse ? "HSE 8MHz crystal" : "HSI 16MHz internal");
    if (d.hse_forced_off) sb_str(&s, " (crystal disabled after clock failure; 'diag clear')");
    sb_str(&s, " rtc="); sb_str(&s, d.lse ? "LSE 32.768kHz" : "LSI (Y2 not running)"); outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, "reset=");
    uint32_t rf = bsp_reset_flags();
    if (rf & RST_POWER_ON) sb_str(&s, "power-on ");
    if (rf & RST_PIN) sb_str(&s, "pin/ext-watchdog ");
    if (rf & RST_IWDG) sb_str(&s, "IWDG ");
    if (rf & RST_SOFT) sb_str(&s, "software ");
    sb_str(&s, " last_fault=");
    if (d.fault_code) {
        static const char *const fc[6] = { "-", "HardFault", "MemManage", "BusFault", "UsageFault", "clock(CSS)" };
        sb_str(&s, d.fault_code < 6u ? fc[d.fault_code] : "?"); sb_str(&s, " pc=0x"); sb_hex(&s, d.fault_pc, 8);
    } else sb_str(&s, "none");
    outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, "self-test: ");
    if (!post_done()) sb_str(&s, "running");
    else if (!(pr & POST_FAIL_MASK)) sb_str(&s, "PASS");
    else { sb_str(&s, "FAIL"); for (uint16_t bit = 1u; bit <= 0x80u; bit <<= 1) if (pr & bit) { sb_char(&s, ' '); sb_str(&s, post_item_name(bit)); } }
    for (uint16_t bit = 0x100u; bit <= 0x400u; bit <<= 1) if (pr & bit) { sb_str(&s, " ["); sb_str(&s, post_item_name(bit)); sb_char(&s, ']'); }
    outln(b);
    sb_init(&s, b, sizeof b); sb_str(&s, "fram="); sb_str(&s, store_healthy() ? "ok" : "FAULT");
    sb_str(&s, " drv_check_failures="); sb_uint_w(&s, out_selftest_fail_count(), 0, ' ');
    sb_str(&s, " drv_monitor="); sb_str(&s, store_settings()->drv_fault_monitor ? "on" : "off");
    sb_str(&s, " pump_check="); sb_str(&s, store_settings()->pump_check_off ? "off" : "on");
    sb_str(&s, " tmo_pct="); sb_uint_w(&s, store_tmo_pct(), 0, ' ');
    sb_str(&s, " hold="); sb_uint_w(&s, store_settings()->drv_hold, 0, ' ');
    sb_str(&s, " log="); sb_uint_w(&s, store_log_count(), 0, ' '); outln(b);
    { uint32_t crc; bool ok = bsp_image_check(&crc);
      sb_init(&s, b, sizeof b); sb_str(&s, "image_crc=0x"); sb_hex(&s, crc, 8); sb_str(&s, ok ? " ok" : " MISMATCH: reflash");
      sb_str(&s, " serial="); sb_str(&s, store_settings()->serial[0] ? store_settings()->serial : "-");
      sb_str(&s, " boots="); sb_uint_w(&s, store_settings()->boot_count, 0, ' ');
      sb_str(&s, " temp_probe="); if (sensors()->temp_ok) { sb_fixed1(&s, sensors()->temp_x10); sb_str(&s, " C"); } else sb_str(&s, "none"); outln(b); }
    sb_init(&s, b, sizeof b); sb_str(&s, "tx_dropped="); sb_uint_w(&s, dropped, 0, ' '); sb_str(&s, " events="); sb_uint_w(&s, ev_count(), 0, ' '); outln(b);
}

static void log_line(int32_t i)
{
    char b[140]; sb_t s;
    log_rec_t r;
    sb_init(&s, b, sizeof b);
    if (!store_log_get((uint16_t)i, &r)) { outln("CRC-ERROR"); return; }
    sb_uint_w(&s, r.seq, 0, ' '); sb_char(&s, ','); print_time(&s, r.time); sb_char(&s, ',');
    sb_uint_w(&s, r.dlz_id, 8, '0'); sb_char(&s, ','); sb_str(&s, program_name((program_t)r.program)); sb_char(&s, ',');
    sb_str(&s, dtype_name(r.dtype)); sb_char(&s, ',');
    sb_str(&s, (r.result & LOG_ABORTED) ? "ABORTED" : (r.result & LOG_PASS) ? "PASS" : "FAIL");
    if (r.result & LOG_FIRST_USE) sb_str(&s, "+FIRST");
    sb_char(&s, ','); sb_fixed1(&s, r.tcv_x10); sb_char(&s, ','); sb_fixed1(&s, r.baseline_x10); sb_char(&s, ',');
    sb_fixed1(&s, r.pct_x10); sb_char(&s, ','); sb_fixed1(&s, r.leak_x10); sb_char(&s, ',');
    sb_str(&s, alarm_text((alarm_t)r.alarm)); sb_char(&s, ','); sb_uint_w(&s, r.stage, 0, ' '); sb_char(&s, ',');
    sb_uint_w(&s, r.duration_s, 0, ' '); sb_char(&s, ','); sb_uint_w(&s, r.uses, 0, ' ');
    outln(b);
}

static void step_line(const step_rec_t *r, uint32_t idx, bool live)
{
    char b[160]; sb_t s;
    sb_init(&s, b, sizeof b);
    if (live) sb_str(&s, "STEP,");
    sb_uint_w(&s, idx + 1u, 0, ' '); sb_char(&s, ','); sb_uint_w(&s, r->stage, 0, ' '); sb_char(&s, ',');
    sb_str(&s, r->name ? r->name : "?"); sb_char(&s, ','); sb_valves(&s, r->valves); sb_char(&s, ',');
    sb_str(&s, step_end_name(r->end)); sb_char(&s, ','); sb_fixed1(&s, (int32_t)(r->ms / 100u)); sb_char(&s, ',');
    sb_fixed1(&s, r->w0_x10); sb_char(&s, ','); sb_fixed1(&s, r->w1_x10); sb_char(&s, ',');
    sb_fixed1(&s, r->pmin_x10); sb_char(&s, ','); sb_fixed1(&s, r->pmax_x10); sb_char(&s, ',');
    sb_fixed1(&s, r->sup_avg_x10); sb_char(&s, ','); sb_fixed1(&s, r->flow_avg_x10); sb_char(&s, ',');
    if (r->tmax_x10 > -999) sb_fixed1(&s, r->tmax_x10);
    outln(b);
}
#define STEP_HDR "n,stage,step,valves,end,time_s,tank_start_g,tank_end_g,bus_min_mmHg,bus_max_mmHg,supply_avg_psig,flow_avg_lpm,driver_max_C"

static void on_step(const step_rec_t *r, uint32_t index)
{
    if (trace_on) step_line(r, index, true);
}

static void trace_tick(void)
{
    if (!trace_on) return;
    uint32_t now = bsp_millis();
    if (elapsed(now, trace_t) < 1000u) return;
    trace_t = now;
    if (!cycle_busy() || bsp_uart_tx_free() < LINE_ROOM) return;
    const sensors_t *ss = sensors();
    const cycle_view_t *cv = cycle_view();
    char b[120]; sb_t s; sb_init(&s, b, sizeof b);
    sb_str(&s, "T,"); sb_uint_w(&s, cv->cycle_elapsed_s, 0, ' '); sb_char(&s, ','); sb_str(&s, cv->step_name ? cv->step_name : "-");
    sb_char(&s, ','); sb_fixed1(&s, ss->weight_x10); sb_char(&s, ','); sb_fixed1(&s, ss->p_bus_x10); sb_char(&s, ',');
    sb_fixed1(&s, ss->p_supply_x10); sb_char(&s, ','); sb_fixed1(&s, ss->flow_x10); sb_char(&s, ','); sb_valves(&s, out_valves());
    outln(b);
}

static void job_tick(void)
{
    for (uint8_t k = 0; k < 4u && job != JOB_NONE && bsp_uart_tx_free() >= LINE_ROOM; k++) {
        if (job == JOB_LOG) {
            if (job_i < 0) { job = JOB_NONE; outln("end"); break; }
            log_line(job_i--);
        } else if (job == JOB_EVENTS) {
            event_t e;
            if (job_i < 0 || !ev_get((uint32_t)job_i, &e)) { job = JOB_NONE; outln("end"); break; }
            event_line(&e); job_i--;
        } else if (job == JOB_KT) {
            char b[170];
            while ((uint32_t)job_i < kittest_items() && !kittest_report_line((uint32_t)job_i, b, sizeof b)) job_i++;
            if ((uint32_t)job_i >= kittest_items()) { kittest_summary(b, sizeof b); outln(b); job = JOB_NONE; outln("end"); break; }
            outln(b); job_i++;
        } else {
            uint32_t n; const step_rec_t *t = cycle_step_log(&n);
            if ((uint32_t)job_i >= n) { job = JOB_NONE; outln("end"); break; }
            step_line(&t[job_i], (uint32_t)job_i, false);
            job_i++;
        }
    }
}

static void cmd_time(uint8_t argc, char *argv[])
{
    char b[40]; sb_t s;
    if (argc == 3) {   /* time YYYY-MM-DD HH:MM:SS */
        const char *d = argv[1], *t = argv[2];
        if (strlen(d) == 10u && strlen(t) == 8u) {
            uint16_t y = (uint16_t)((d[0]-'0')*1000 + (d[1]-'0')*100 + (d[2]-'0')*10 + (d[3]-'0'));
            uint8_t mo = (uint8_t)((d[5]-'0')*10 + (d[6]-'0')), dd = (uint8_t)((d[8]-'0')*10 + (d[9]-'0'));
            uint8_t h = (uint8_t)((t[0]-'0')*10 + (t[1]-'0')), mi = (uint8_t)((t[3]-'0')*10 + (t[4]-'0')), se = (uint8_t)((t[6]-'0')*10 + (t[7]-'0'));
            if (y >= 2000u && y < 2100u && mo >= 1u && mo <= 12u && dd >= 1u && dd <= 31u && h < 24u && mi < 60u && se < 60u)
                bsp_rtc_set(secs_from_date(y, mo, dd, h, mi, se));
            else { outln("ERR date"); return; }
        } else { outln("ERR use: time YYYY-MM-DD HH:MM:SS"); return; }
    }
    sb_init(&s, b, sizeof b); print_time(&s, bsp_rtc_get()); outln(b);
}

static program_t prog_from(const char *s)
{
    for (uint32_t i = 0; i < PROG_COUNT; i++) if (strcmp(s, program_cli_name((program_t)i)) == 0) return (program_t)i;
    return PROG_COUNT;
}

static void cmd_drv(void)
{
    char b[96]; sb_t s; sb_init(&s, b, sizeof b);
    sb_str(&s, "rail="); sb_str(&s, out_rail_ready() ? "ready" : out_rail_on() ? "starting" : "off");
    sb_str(&s, " nFAULT="); sb_str(&s, bsp_valve_nfault() ? "LOW" : "high");
    sb_str(&s, " last_check="); sb_str(&s, out_selftest_failed() ? "FAILED" : "ok");
    sb_str(&s, " dout_raw=0x"); sb_hex(&s, bsp_valve_readback(), 4);
    outln(b);
    bool ver; uint8_t cw = out_hold_ctrl(&ver);
    sb_init(&s, b, sizeof b); sb_str(&s, "hold=");
    if (cw) { sb_uint_w(&s, (uint32_t)((cw >> 4) & 7u) * 125u / 10u, 0, ' '); sb_str(&s, "% after 110 ms ctrl=0x"); sb_hex(&s, cw, 2);
              sb_str(&s, ver ? " read-back ok" : " READ-BACK MISMATCH (check B1 waveform)"); }
    else sb_str(&s, "off (100 % on)");
    outln(b);
    outln("dout_raw: 16 bits seen on DOUT while the last data word was shifted in (U11 then U10), for bench decoding (B1)");
}

static void execute(char *ln)
{
    char *argv[6];
    uint8_t argc = split(ln, argv, 6);
    if (argc == 0u) return;
    const char *c = argv[0];
    uint32_t a = 0, b2 = 0;

    if (!strcmp(c, "help")) {
        outln("help ver status diag log [n] steps events [n] time [YYYY-MM-DD HH:MM:SS] dlz <id> stop enter unlock <pin> lock");
        outln("kit test (after unlock): kittest [1-5|all] [quick|long]  ok | no | skip  kittest abort  kittest report  events on|off");
        outln("service (after unlock): valve <1-10|pump|spare> <on|off>   drv   trace <on|off>   diag clear");
        outln("  cal zero | cal span <g> | cal pzero | cal wf <ml_x10>");
        outln("  start <low|mid|high|preclean|disinfect|rinseout|drain|fill|prime|seal> [type 1-3] [prime_ml] [id]");
        outln("  strip <neg|pos>   set tmo <100-300>   set pumpcheck <on|off>   set monitor <on|off>   set pin <nnnn>");
        outln("  set hold <0-7>   (0 = valves 100 % on; n = hold PWM n x 12.5 % after 110 ms; bench item B8)   set serial <max 11 chars>");
    } else if (!strcmp(c, "ver")) {
        outln(FW_NAME " firmware " FW_VERSION_STR " for " HW_TARGET_STR);
    } else if (!strcmp(c, "status")) {
        cmd_status();
    } else if (!strcmp(c, "diag") && argc == 1) {
        cmd_diag();
    } else if (!strcmp(c, "log")) {
        if (argc >= 2) (void)parse_u32(argv[1], &a);
        uint16_t cnt = store_log_count();
        if (a == 0u || a > cnt) a = cnt;
        outln("seq,date,dialyser_id,program,type,result,tcv_ml,baseline_ml,tcv_pct,leak_mmHg,alarm,stage,duration_s,uses");
        job = JOB_LOG; job_i = (int32_t)a - 1;
    } else if (!strcmp(c, "events") && argc == 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) {
        events_on = !strcmp(argv[1], "on"); outln(events_on ? "events streaming on" : "events streaming off");
    } else if (!strcmp(c, "events")) {
        a = EV_CAPACITY;
        if (argc >= 2) (void)parse_u32(argv[1], &a);
        uint32_t held = ev_count() < EV_CAPACITY ? ev_count() : EV_CAPACITY;
        if (a > held) a = held;
        outln("E,time_s,event,detail");
        job = JOB_EVENTS; job_i = (int32_t)a - 1;
    } else if (!strcmp(c, "ok") || !strcmp(c, "no") || !strcmp(c, "skip")) {
        if (!kittest_active()) { outln("ERR no kit test running"); return; }
        kittest_answer(!strcmp(c, "ok") ? KA_OK : !strcmp(c, "no") ? KA_NO : KA_SKIP);
    } else if (!strcmp(c, "kittest") && argc >= 2 && !strcmp(argv[1], "report")) {
        outln("K,item,name,status,value,limit,note");
        job = JOB_KT; job_i = 0;
    } else if (!strcmp(c, "steps")) {
        outln(STEP_HDR);
        job = JOB_STEPS; job_i = 0;
    } else if (!strcmp(c, "time")) {
        cmd_time(argc, argv);
    } else if (!strcmp(c, "dlz") && argc == 2 && parse_u32(argv[1], &a)) {
        dlz_entry_t e; char bb[64]; sb_t s; sb_init(&s, bb, sizeof bb);
        if (store_dlz_find(a, &e)) { sb_str(&s, "type="); sb_str(&s, dtype_name(e.dtype)); sb_str(&s, " baseline_ml="); sb_fixed1(&s, e.baseline_x10);
            sb_str(&s, " uses="); sb_uint_w(&s, e.uses, 0, ' '); outln(bb); }
        else outln("unknown dialyser");
    } else if (!strcmp(c, "stop")) {
        cycle_stop(); outln("OK");
    } else if (!strcmp(c, "enter")) {
        cycle_enter(); outln("OK");
    } else if (!strcmp(c, "unlock") && argc == 2 && parse_u32(argv[1], &a)) {
        unlocked = (a == store_settings()->service_pin); outln(unlocked ? "unlocked" : "ERR pin");
    } else if (!strcmp(c, "lock")) {
        unlocked = false; outln("locked");
    } else if (!unlocked) {
        outln("ERR unknown or locked command (help)");
    } else if (!strcmp(c, "kittest")) {
        if (argc >= 2 && !strcmp(argv[1], "abort")) { kittest_abort(); return; }
        uint8_t stage = 0; bool quick = false, longer = false;
        for (uint8_t i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "quick")) quick = true;
            else if (!strcmp(argv[i], "long")) longer = true;
            else if (!strcmp(argv[i], "all")) stage = 0;
            else if (parse_u32(argv[i], &a) && a >= 1u && a <= 5u) stage = (uint8_t)a;
            else { outln("ERR use: kittest [1-5|all] [quick|long] | kittest abort | kittest report"); return; }
        }
        char why[40];
        if (!kittest_start(stage, quick, longer, why, sizeof why)) { out("ERR "); outln(why); }
    } else if (!strcmp(c, "diag") && argc == 2 && !strcmp(argv[1], "clear")) {
        bsp_diag_clear(); outln("fault record cleared; crystal allowed again at next reset");
    } else if (!strcmp(c, "drv")) {
        cmd_drv();
    } else if (!strcmp(c, "trace") && argc == 2) {
        trace_on = !strcmp(argv[1], "on");
        store_settings()->trace = trace_on ? 1u : 0u; (void)store_save_settings();
        outln(trace_on ? "trace on: STEP lines at each step end, T lines every 1 s while running" : "trace off");
    } else if (!strcmp(c, "valve") && argc == 3) {
        if (cycle_busy() || !post_done() || kittest_active()) { outln("ERR busy"); return; }
        bool on = !strcmp(argv[2], "on");
        out_rail(true);
        if (!out_rail_ready()) { outln("rail starting, repeat in 1 s"); return; }
        out_all_off();
        if (!strcmp(argv[1], "pump")) out_pump(on);
        else if (!strcmp(argv[1], "spare")) { if (on) (void)out_set(SPARE_OUT, 0); }
        else if (parse_u32(argv[1], &a) && a >= 1u && a <= 10u) { if (on) (void)out_set((uint16_t)(1u << (a - 1u)), OUT_ALLOW_TANK); }
        else { outln("ERR valve"); return; }
        outln("OK (one output at a time; 'valve 1 off' clears; auto-off after 10 s on the LCD menu only)");
    } else if (!strcmp(c, "cal") && argc >= 2) {
        cal_t *cal = store_cal();
        if (!strcmp(argv[1], "zero")) cal->scale_zero = sensors()->scale_raw;
        else if (!strcmp(argv[1], "span") && argc == 3 && parse_u32(argv[2], &a) && a > 0u) {
            int32_t counts = sensors()->scale_raw - cal->scale_zero;
            if (counts <= 0) { outln("ERR no load"); return; }
            cal->scale_cpg_x100 = (int32_t)((int64_t)counts * 100 / (int64_t)a);
        } else if (!strcmp(argv[1], "pzero")) {
            cal->s2_zero_x10 = (int16_t)sensors_bus_uncal_x10(); cal->s3_zero_x10 = (int16_t)sensors_supply_uncal_x10();
        } else if (!strcmp(argv[1], "wf") && argc == 3 && parse_u32(argv[2], &a)) cal->wf_x10 = (int16_t)clamp_i32((int32_t)a, 0, 3000);
        else { outln("ERR cal"); return; }
        outln(store_save_cal() ? "saved" : "ERR save");
    } else if (!strcmp(c, "set") && argc == 3) {
        settings_t *st = store_settings();
        if (!strcmp(argv[1], "tmo") && parse_u32(argv[2], &a) && a >= TMO_PCT_MIN && a <= TMO_PCT_MAX) st->tmo_pct = (uint16_t)a;
        else if (!strcmp(argv[1], "pumpcheck")) st->pump_check_off = strcmp(argv[2], "on") ? 1u : 0u;
        else if (!strcmp(argv[1], "monitor")) { st->drv_fault_monitor = strcmp(argv[2], "on") ? 0u : 1u; out_monitor_enable(st->drv_fault_monitor != 0u); }
        else if (!strcmp(argv[1], "pin") && strlen(argv[2]) == 4u && parse_u32(argv[2], &a)) st->service_pin = (uint16_t)a;
        else if (!strcmp(argv[1], "hold") && parse_u32(argv[2], &a) && a <= 7u) st->drv_hold = (uint8_t)a;  /* takes effect at next rail-up */
        else if (!strcmp(argv[1], "serial") && strlen(argv[2]) < sizeof st->serial) { memset(st->serial, 0, sizeof st->serial); memcpy(st->serial, argv[2], strlen(argv[2])); }
        else { outln("ERR set"); return; }
        outln(store_save_settings() ? "saved" : "ERR save");
    } else if (!strcmp(c, "start") && argc >= 2) {
        if (!post_done()) { outln("ERR self-test running"); return; }
        if (kittest_active()) { outln("ERR kit test running"); return; }
        cycle_req_t r; memset(&r, 0, sizeof r);
        r.prog = prog_from(argv[1]);
        if (r.prog >= PROG_COUNT) { outln("ERR program"); return; }
        r.dtype = program_dtype(r.prog);
        if (argc >= 3 && parse_u32(argv[2], &a) && a >= 1u && a <= 3u) r.dtype = (uint8_t)a;
        if (r.prog == PROG_PRECLEAN && r.dtype == 0u) r.dtype = DT_LOW;
        r.prime_ml = 120;
        if (argc >= 4 && parse_u32(argv[3], &b2)) r.prime_ml = (uint16_t)b2;
        if (argc >= 5 && parse_u32(argv[4], &a)) r.dlz_id = a;
        alarm_t e = cycle_start(&r);
        outln(e == ALM_NONE ? "started" : alarm_text(e));
    } else if (!strcmp(c, "strip") && argc == 2) {
        cycle_strip(!strcmp(argv[1], "neg")); outln("OK");
    } else {
        outln("ERR unknown command (help)");
    }
}

void cli_init(void)
{
    line_len = 0; unlocked = false; job = JOB_NONE; dropped = 0;
    trace_on = store_settings()->trace != 0u;
    trace_t = bsp_millis();
    cycle_set_step_hook(on_step);
    ev_set_sink(on_event);
    outln(FW_NAME " " FW_VERSION_STR " ready. Type help.");
}

void cli_poll(void)
{
    job_tick();
    trace_tick();
    if (job != JOB_NONE || bsp_uart_tx_free() < CMD_ROOM) return;   /* finish output before taking input */
    int ch;
    uint8_t budget = 32;
    while (budget-- && (ch = bsp_uart_getc()) >= 0) {
        if (ch == '\r' || ch == '\n') {
            if (line_len) { line_buf[line_len] = '\0'; out("\r\n"); execute(line_buf); line_len = 0; break; }
        } else if (ch == 8 || ch == 127) {
            if (line_len) line_len--;
        } else if (ch >= 32 && ch < 127 && line_len + 1u < sizeof line_buf) {
            line_buf[line_len++] = (char)ch;
        }
    }
}
