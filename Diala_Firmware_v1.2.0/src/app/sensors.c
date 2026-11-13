#include "sensors.h"
#include "config.h"
#include "store.h"
#include "util.h"
#include "events.h"

static sensors_t s;
static uint32_t ema[ADC_COUNT];       /* raw x16 */
static bool ema_primed;
static int32_t hx_hist[3];
static uint8_t hx_idx;
static bool hx_primed;
static uint32_t hx_last_ms;
static uint32_t flow_last_count, flow_last_ms;
static int32_t bus_uncal, supply_uncal;
static uint32_t temp_t, temp_ok_t;
static bool temp_converting, temp_seen, inputs_primed;
static int16_t temp_band;

void sensors_init(void)
{
    ema_primed = false;
    hx_idx = 0;
    hx_primed = false;
    hx_last_ms = 0;
    flow_last_count = bsp_flow_count();
    temp_t = bsp_millis() - 1000u; temp_converting = false; temp_seen = false; temp_band = -1; inputs_primed = false;
    flow_last_ms = bsp_millis();
}

const sensors_t *sensors(void) { return &s; }

static int32_t adc_mv(adc_ch_t ch) { return (int32_t)((ema[ch] / 16u) * (uint32_t)ADC_VREF_MV / 4095u); }

/* 0.5-4.5 V ratiometric transducer behind the 10k/20k divider -> pascal */
static bool transducer_pa(adc_ch_t ch, int32_t kpa_min, int32_t kpa_max, int32_t *pa)
{
    int32_t vout = adc_mv(ch) * 3 / 2;
    *pa = (int32_t)(((int64_t)(vout - 500) * (kpa_max - kpa_min) * 1000) / 4000) + kpa_min * 1000;
    return vout >= 250 && vout <= 4750;      /* outside = open wire or failed sensor */
}

static int32_t median3(const int32_t *v)
{
    int32_t a = v[0], b = v[1], c = v[2];
    if (a > b) { int32_t t = a; a = b; b = t; }
    if (b > c) { b = c; }
    return a > b ? a : b;
}

void sensors_tick(uint32_t now)
{
    const cal_t *cal = store_cal();
    uint16_t raw[ADC_COUNT];
    bsp_adc_read(raw);
    for (uint32_t i = 0; i < ADC_COUNT; i++) {
        s.adc[i] = raw[i];
        uint32_t x = (uint32_t)raw[i] * 16u;
        ema[i] = ema_primed ? (ema[i] * 3u + x) / 4u : x;
    }
    ema_primed = true;

    int32_t pa;
    s.p_bus_ok = transducer_pa(ADC_P_BUS, S2_KPA_MIN, S2_KPA_MAX, &pa);
    bus_uncal = (int32_t)((int64_t)pa * 10000 / 133322);    /* mmHg x10 = Pa x 10 / 133.322 */
    s.p_bus_x10 = bus_uncal - cal->s2_zero_x10;
    s.p_supply_ok = transducer_pa(ADC_P_SUPPLY, S3_KPA_MIN, S3_KPA_MAX, &pa);
    supply_uncal = (int32_t)((int64_t)pa * 1000 / 689476);     /* psig x10 = Pa x 10 / 6894.76 */
    s.p_supply_x10 = supply_uncal - cal->s3_zero_x10;

    s.v24_mv   = (uint32_t)adc_mv(ADC_V24_MON) * 11u;
    s.vrail_mv = (uint32_t)adc_mv(ADC_VRAIL_MON) * 11u;
    s.v5_mv    = (uint32_t)adc_mv(ADC_V5_MON) * 2u;
    s.pump_ma  = (uint32_t)adc_mv(ADC_PUMP_I);            /* 1 ohm shunt: mV = mA */
    s.ph_x10   = (int32_t)((int64_t)adc_mv(ADC_PH) * 140 / 3000 * cal->ph_gain_x1000 / 1000) + cal->ph_off_x10;

    int32_t hx;
    if (bsp_hx711_read(&hx)) {
        if (!hx_primed) { hx_hist[0] = hx_hist[1] = hx_hist[2] = hx; hx_primed = true; }
        hx_hist[hx_idx] = hx;
        hx_idx = (uint8_t)((hx_idx + 1u) % 3u);
        s.scale_raw = median3(hx_hist);
        int32_t cpg = cal->scale_cpg_x100 > 0 ? cal->scale_cpg_x100 : 1;
        s.weight_x10 = (int32_t)(((int64_t)(s.scale_raw - cal->scale_zero) * 1000) / cpg);
        hx_last_ms = now;
    }
    s.weight_ok = hx_primed && elapsed(now, hx_last_ms) < 500u;

    if (elapsed(now, flow_last_ms) >= 1000u) {
        uint32_t c = bsp_flow_count();
        uint32_t hz_x100 = (c - flow_last_count) * 100000u / elapsed(now, flow_last_ms);
        uint32_t k = cal->flow_k_x10 ? cal->flow_k_x10 : FLOW_HZ_PER_LPM_X10;
        s.flow_x10 = (int32_t)(hz_x100 / k);
        flow_last_count = c;
        flow_last_ms = now;
    }

    bool liq = bsp_liquid_detect(), dlz = bsp_dialyser_present(), conc = bsp_conc_low();
    if (inputs_primed) {
        if (dlz != s.dialyser) ev_log(EV_INPUT, 6u, dlz);
        if (conc != s.conc_low) ev_log(EV_INPUT, 7u, conc);
        if (liq != s.liquid) ev_log(EV_INPUT, 8u, liq);
    }
    inputs_primed = true;
    s.liquid = liq;
    s.dialyser = dlz;
    s.conc_low = conc;

    /* DS18B20: start a conversion, read it 800 ms later, once a second */
    if (!temp_converting && elapsed(now, temp_t) >= 1000u) { temp_t = now; bsp_temp_start(); temp_converting = true; }
    else if (temp_converting && elapsed(now, temp_t) >= 800u) {
        int16_t t;
        temp_converting = false;
        if (bsp_temp_fetch(&t) && !(t == 850 && !temp_seen)) {   /* 85.0 before the first conversion = power-up value */
            s.temp_x10 = t; temp_ok_t = now; temp_seen = true;
            int16_t band = (int16_t)(t / 50);
            if (band != temp_band) { temp_band = band; ev_log(EV_TEMP, 0, t); }
        }
    }
    s.temp_ok = temp_seen && elapsed(now, temp_ok_t) < 2500u;
}

bool sensors_rails_ok(void)
{
    return s.v24_mv >= V24_MIN_MV && s.v24_mv <= V24_MAX_MV && s.v5_mv >= V5_MIN_MV && s.v5_mv <= V5_MAX_MV;
}

int32_t sensors_weight_g(void) { return (s.weight_x10 >= 0 ? s.weight_x10 + 5 : s.weight_x10 - 5) / 10; }
int32_t sensors_bus_uncal_x10(void) { return bus_uncal; }
int32_t sensors_supply_uncal_x10(void) { return supply_uncal; }
