/* Host simulator: implements bsp.h on a PC with a physical model of the Diala manifold,
 * mix tank, venturi, dialyser and sensors, so the unmodified application can be tested. */
#include "sim.h"
#include "../bsp/bsp.h"
#include "../app/config.h"
#include <string.h>
#include <stdio.h>

sim_plant_t P;
static uint8_t fram[NV_SIZE];
static char lcd[4][21];
static char uart_out[65536]; static uint32_t uart_out_len;
static char uart_in[512]; static uint32_t uart_in_head, uart_in_tail;
static uint32_t flow_count; static double flow_acc;
static double hx_next_ms;
static bool hx_ready;
static double node_p[9];
uint32_t sim_valve_writes;
double sim_cost_us;
static char tx_ring[UART_TX_BUF]; static uint32_t tx_n;     /* bytes waiting to leave at 115200 baud */

enum { N_W, N_BUS, N_SUC, N_A, N_V, N_DI, N_DO, N_TANK, N_AIR, N_COUNT };

void sim_defaults(void)
{
    memset(&P, 0, sizeof P);
    P.supply_psig = 25.0;
    P.tank_g = 0.0;
    P.conc_available = true;
    P.dialyser = true;
    P.tcv_ml = 120.0;
    P.wf_ml = 70.0;
    P.leak_mmhg_s = 0.2;            /* healthy membrane: ~4 mmHg loss in 20 s */
    P.v24 = 24.0; P.v5 = 5.0;
    P.blood_fill = 0.0;
    P.rtc_base = 845000000u;        /* 2026-10-10 */
    P.s8_force = -1;
    P.temp_c = 30.0;
    memset(node_p, 0, sizeof node_p);
    hx_next_ms = 100.0;
}

void sim_fram_clear(void) { memset(fram, 0xFF, sizeof fram); }
uint8_t *sim_fram(void) { return fram; }

/* ------------------------------------------------------------------ physics */
static int find(int *par, int x) { while (par[x] != x) x = par[x] = par[par[x]]; return x; }
static void unite(int *par, int a, int b) { a = find(par, a); b = find(par, b); if (a != b) par[a] = b; }
static bool open_v(uint16_t bit) { return (P.valves & bit) && P.rail; }

void sim_physics(double dt_ms)
{
    double dt = dt_ms / 1000.0;
    bool venturi = open_v(V1) && P.supply_psig > 5.0 && !P.venturi_broken;
    if (P.supply_fail_at_ms && P.ms >= P.supply_fail_at_ms) P.supply_psig = 8.0;

    int par[N_COUNT];
    for (int i = 0; i < N_COUNT; i++) par[i] = i;
    if (open_v(V2)) unite(par, N_W, N_BUS);
    if (open_v(V3)) unite(par, N_BUS, N_A);
    if (open_v(V4)) unite(par, N_BUS, N_V);
    if (open_v(V5) || P.v5_stuck) unite(par, N_BUS, N_TANK);
    if (open_v(V6)) unite(par, N_A, N_SUC);
    if (open_v(V7)) unite(par, N_V, N_SUC);
    if (open_v(V8)) unite(par, N_DO, N_SUC);
    if (open_v(V9)) unite(par, N_BUS, N_DI);
    if (open_v(V10)) unite(par, N_AIR, N_V);
    if (P.dialyser || P.bypass) { unite(par, N_A, N_V); unite(par, N_DI, N_DO); }

    /* pressure per connected group (mmHg gauge) */
    for (int c = 0; c < N_COUNT; c++) {
        bool has[N_COUNT] = { false };
        bool any = false;
        for (int n = 0; n < N_COUNT; n++) if (find(par, n) == c) { has[n] = true; any = true; }
        if (!any) continue;
        double target; double tau;
        bool atm = has[N_AIR] || has[N_TANK] || (has[N_SUC] && !venturi);   /* suction chamber vents to drain */
        if (has[N_W]) { target = P.supply_psig * 51.715; tau = 0.05; }
        else if (has[N_SUC] && venturi) {
            if (atm) { target = -30.0; tau = 0.3; }
            else {
                target = -420.0; tau = 2.0;
                if (P.connector_leak && (has[N_A] || has[N_V] || has[N_DI])) target = -60.0;
            }
        } else if (atm) { target = 0.0; tau = 0.2; }
        else { target = 1e9; tau = 0; }                 /* isolated: hold with leak */
        for (int n = 0; n < N_COUNT; n++) {
            if (!has[n]) continue;
            if (target < 1e8) node_p[n] += (target - node_p[n]) * (dt / (tau + dt));
            else if ((n == N_DI || n == N_DO || n == N_BUS || n == N_A || n == N_V) && node_p[n] < 0.0) {
                bool dial = has[N_DI] || has[N_DO];
                if (dial && (P.dialyser || P.bypass)) node_p[n] += P.leak_mmhg_s * dt;
                if (P.connector_leak) node_p[n] += 5.0 * dt;
                /* weeping closed valves: one side in this trapped group, the other side at air or water */
                static const struct { uint16_t v; int a, b; } seat[] = {
                    { V2, N_W, N_BUS }, { V3, N_BUS, N_A }, { V4, N_BUS, N_V }, { V5, N_TANK, N_BUS },
                    { V6, N_A, N_SUC }, { V7, N_V, N_SUC }, { V8, N_DO, N_SUC }, { V9, N_BUS, N_DI }, { V10, N_AIR, N_V } };
                for (unsigned k = 0; k < sizeof seat / sizeof seat[0]; k++) {
                    if (!(P.weep & seat[k].v) || open_v(seat[k].v)) continue;
                    bool ina = has[seat[k].a], inb = has[seat[k].b];
                    int other = ina ? seat[k].b : seat[k].a;
                    if ((ina ^ inb) && node_p[other] > node_p[n] + 20.0) node_p[n] += 3.0 * dt;
                }
                if (node_p[n] > 0.0) node_p[n] = 0.0;
            }
        }
    }
    P.p_bus = node_p[N_BUS];
    P.p_dial = node_p[N_DI];

    /* liquid: a wet membrane passes liquid between the blood and dialysate sides (not gas) */
    int lpar[N_COUNT];
    for (int i = 0; i < N_COUNT; i++) lpar[i] = find(par, i);
    if (P.dialyser) unite(lpar, N_A, N_DI);
    bool tank_to_suc = find(lpar, N_TANK) == find(lpar, N_SUC) && venturi;
    bool w_to_tank = find(par, N_W) == find(par, N_TANK) && P.supply_psig > 5.0;
    if (w_to_tank) P.tank_g += 40.0 * dt;          /* ~2.4 L/min through V2 [bench: measure] */
    if (tank_to_suc && P.tank_g > 0.0) {
        double rate = 8.0;
        bool via_blood_draw = open_v(V3) && open_v(V7) && !open_v(V6) && open_v(V5);   /* TCV draw / germicide to blood */
        if (via_blood_draw) {
            rate = 7.0;                                /* through the fibres [bench: measure] */
            P.blood_fill += rate * dt;
        }
        P.tank_g -= rate * dt;
        if (P.tank_g < 0.0) P.tank_g = 0.0;
    }
    if (P.pump_duty > 0 && P.rail && P.conc_available) P.tank_g += 0.6 * dt;
    if (open_v(V2) && (open_v(V3) || open_v(V4))) P.blood_fill = P.tcv_ml + P.wf_ml;       /* flushes leave blood side full */
    if (venturi && open_v(V10) && open_v(V6) && !open_v(V5)) { P.blood_fill -= 15.0 * dt; if (P.blood_fill < 0.0) P.blood_fill = 0.0; }
    if (P.blood_fill > P.tcv_ml + P.wf_ml + 20.0) P.blood_fill = P.tcv_ml + P.wf_ml + 20.0;

    /* flow sensor on the water inlet */
    double lpm = (open_v(V2) ? 4.0 : 0.0) + (open_v(V1) ? 3.0 : 0.0);
    flow_acc += P.flow_hz > 0.0 ? P.flow_hz * dt : lpm * 7.5 * dt;
    while (flow_acc >= 1.0) { flow_acc -= 1.0; flow_count++; }

    if ((double)P.ms >= hx_next_ms) { hx_ready = true; hx_next_ms += 100.0; }

    /* driver package temperature: 1.6 degC per energised large valve above 30 degC, tau 40 s */
    { int on = 0; for (int i = 0; i < 10; i++) if (P.rail && (P.valves & (1u << i))) on++;
      double target = 30.0 + 1.6 * on * (on > 3 ? 2.2 : 1.0);
      P.temp_c += (target - P.temp_c) * dt / 40.0; }
}

/* ------------------------------------------------------------------ BSP implementation */
void bsp_init(void) { }
void bsp_diag(bsp_diag_t *d) { d->hse = false; d->hse_forced_off = false; d->lse = true; d->fault_pc = 0; d->fault_code = 0; }
void bsp_diag_clear(void) { }
static bool temp_started;
void bsp_temp_start(void) { temp_started = P.temp_present; }
bool bsp_temp_fetch(int16_t *c_x10) { if (!temp_started || !P.temp_present) return false; *c_x10 = (int16_t)(P.temp_c * 10.0 + (P.temp_c >= 0 ? 0.5 : -0.5)); sim_cost_us += 7000.0; return true; }
bool bsp_image_check(uint32_t *crc) { *crc = 0x1234ABCDu; sim_cost_us += 9000.0; return !P.image_bad; }
uint16_t bsp_valve_readback(void) { return 0; }
static uint8_t drv_ctrl;
void bsp_valve_control(uint8_t ctrl) { if (P.rail) drv_ctrl = ctrl; }
uint16_t bsp_valve_read_control(void) { return P.rail ? (uint16_t)((drv_ctrl << 8) | drv_ctrl) : 0u; }
void bsp_lcd_reinit(void) { sim_cost_us += 6000.0; memset(lcd, 0, sizeof lcd); }
uint32_t bsp_millis(void) { return P.ms; }
uint32_t bsp_reset_flags(void) { return RST_POWER_ON; }

void bsp_valve_rail(bool on) { P.rail = on; if (!on) { P.valves = 0; drv_ctrl = 0; } }   /* VM off: DRV8860 logic resets */
void bsp_valve_write(uint16_t mask)
{
    sim_cost_us += 60.0;
    if (!P.rail) return;                                        /* lines held low while unpowered */
    sim_valve_writes++;
    P.valves = P.rail ? mask : 0u;
    if (P.valves & ~(VALVE_ALL_MASK | SPARE_OUT)) P.illegal_write = true;
    if (P.rail && (mask & V1) && (mask & V2)) P.v1_v2_together = true;
}
bool bsp_valve_nfault(void) { return P.rail && (P.nfault || P.nfault_stuck_after_reset || (P.open_load & (uint16_t)~P.valves)); }
void bsp_valve_fault_reset(void) { P.fault_resets++; if (!P.nfault_stuck_after_reset) P.nfault = false; }
void bsp_pump(uint8_t pct) { P.pump_duty = P.rail ? pct : 0u; }
void bsp_buzzer(bool on) { P.buzzer = on; }
void bsp_leds(bool g, bool r) { P.led_g = g; P.led_r = r; }
void bsp_lcd_backlight(uint8_t pct) { (void)pct; }
void bsp_wdg_kick(void) { P.last_kick_ms = P.ms; P.kicks++; }

static uint16_t mv_to_adc(double mv) { double r = mv * 4095.0 / ADC_VREF_MV; return (uint16_t)(r < 0 ? 0 : r > 4095 ? 4095 : r + 0.5); }
static double transducer_pin_mv(double kpa, double kmin, double kmax) { double vout = 500.0 + (kpa - kmin) * 4000.0 / (kmax - kmin); return vout * 2.0 / 3.0; }

void bsp_adc_read(uint16_t raw[ADC_COUNT])
{
    sim_cost_us += 250.0;
    double supply = P.supply_psig - (((P.valves & (V1 | V2)) && P.rail) ? 3.0 : 0.0);
    raw[ADC_P_BUS] = P.s2_broken ? 0 : mv_to_adc(transducer_pin_mv(P.p_bus * 0.133322, S2_KPA_MIN, S2_KPA_MAX));
    raw[ADC_P_SUPPLY] = mv_to_adc(transducer_pin_mv(supply * 6.89476, S3_KPA_MIN, S3_KPA_MAX));
    if (P.kit_pots) { raw[ADC_P_BUS] = mv_to_adc(P.s2_wiper_v * 1000.0 * 2.0 / 3.0); raw[ADC_P_SUPPLY] = mv_to_adc(P.s3_wiper_v * 1000.0 * 2.0 / 3.0); }
    raw[ADC_V24_MON] = mv_to_adc(P.v24 * 1000.0 / 11.0);
    raw[ADC_PH] = mv_to_adc(1500.0);
    raw[ADC_V5_MON] = mv_to_adc(P.v5 * 1000.0 / 2.0);
    raw[ADC_VRAIL_MON] = mv_to_adc(P.rail ? P.v24 * 1000.0 / 11.0 : 0.0);
    raw[ADC_PUMP_I] = mv_to_adc((P.pump_duty && P.rail && !P.pump_open) ? 280.0 : 0.0);
}

bool bsp_hx711_read(int32_t *out)
{
    if (!hx_ready || P.hx_dead) return false;
    hx_ready = false;
    sim_cost_us += 60.0;
    *out = (int32_t)(P.tank_g * 613.0) + P.hx_offset;
    return true;
}

uint32_t bsp_flow_count(void) { return flow_count; }
uint8_t bsp_keys_raw(void) { return P.keys; }
bool bsp_liquid_detect(void) { if (P.s8_force >= 0) return P.s8_force != 0; return P.blood_fill >= P.tcv_ml + P.wf_ml && !P.s8_dead; }
bool bsp_dialyser_present(void) { return P.dialyser; }
bool bsp_conc_low(void) { return P.conc_switch_low; }

void bsp_lcd_row(uint8_t row, const char text[20])
{
    if (row >= 4u || !memcmp(lcd[row], text, 20)) return;
    sim_cost_us += 20.0 * 50.0;                                /* 20 characters at ~50 us each */
    memcpy(lcd[row], text, 20); lcd[row][20] = '\0';
}
const char *sim_lcd_row(uint8_t r) { return lcd[r]; }

/* bit-banged I2C on the target: ~65 us per byte including the 4 address/control bytes */
bool bsp_nv_read(uint32_t addr, void *buf, uint32_t len) { sim_cost_us += (len + 4u) * 65.0; if (P.fram_dead || addr + len > NV_SIZE) return false; memcpy(buf, fram + addr, len); return true; }
bool bsp_nv_write(uint32_t addr, const void *buf, uint32_t len) { sim_cost_us += (len + 3u) * 65.0; if (P.fram_dead || addr + len > NV_SIZE) return false; memcpy(fram + addr, buf, len); return true; }

uint32_t bsp_rtc_get(void) { return P.rtc_base + P.ms / 1000u; }
void bsp_rtc_set(uint32_t secs) { P.rtc_base = secs - P.ms / 1000u; }

int bsp_uart_getc(void) { if (uart_in_head == uart_in_tail) return -1; int c = (unsigned char)uart_in[uart_in_tail]; uart_in_tail = (uart_in_tail + 1u) % sizeof uart_in; return c; }
/* TX ring as on the target: bytes leave at 11.52 per ms; the capture keeps the last 64 KB sent */
uint32_t bsp_uart_tx_free(void) { return UART_TX_BUF - 1u - tx_n; }
uint32_t bsp_uart_write(const char *s, uint32_t n)
{
    uint32_t k = 0;
    for (; k < n && tx_n < UART_TX_BUF - 1u; k++) tx_ring[tx_n++] = s[k];
    return k;
}
uint32_t sim_uart_lines, sim_prompts;
char sim_last_prompt[200];
static char cap_line[200]; static uint32_t cap_n;
static void uart_capture(char c)
{
    if (c == '\n') {
        sim_uart_lines++;
        cap_line[cap_n] = '\0';
        if (!strncmp(cap_line, "PROMPT,", 7)) { strcpy(sim_last_prompt, cap_line); sim_prompts++; }
        cap_n = 0;
    } else if (c != '\r' && cap_n + 1u < sizeof cap_line) cap_line[cap_n++] = c;
    if (uart_out_len + 1u >= sizeof uart_out) { memmove(uart_out, uart_out + 32768, uart_out_len - 32768u); uart_out_len -= 32768u; }
    uart_out[uart_out_len++] = c; uart_out[uart_out_len] = '\0';
}
void sim_uart_drain(uint32_t ms)
{
    static double credit;
    credit += 11.52 * ms;
    uint32_t n = (uint32_t)credit;
    if (n > tx_n) n = tx_n;
    credit -= n;
    if (tx_n == 0) credit = 0;
    for (uint32_t i = 0; i < n; i++) uart_capture(tx_ring[i]);
    memmove(tx_ring, tx_ring + n, tx_n - n);
    tx_n -= n;
}
void sim_uart_send(const char *s) { while (*s) { uart_in[uart_in_head] = *s++; uart_in_head = (uart_in_head + 1u) % sizeof uart_in; } }
const char *sim_uart_output(void) { return uart_out; }
void sim_uart_clear(void) { uart_out_len = 0; uart_out[0] = '\0'; }
