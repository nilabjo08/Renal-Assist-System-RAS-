#include "post.h"
#include "config.h"
#include "alarms.h"
#include "outputs.h"
#include "sensors.h"
#include "store.h"
#include "util.h"
#include "events.h"
#include "../bsp/bsp.h"

typedef enum { PS_IDLE, PS_SENSORS, PS_RAIL, PS_DONE } post_state_t;

static post_state_t st;
static uint32_t t0;
static uint16_t res;

bool post_done(void) { return st == PS_DONE; }
uint16_t post_result(void) { return res; }

const char *post_item_name(uint16_t bit)
{
    switch (bit) {
    case POST_FRAM:  return "FRAM";
    case POST_RAILS: return "24V/5V";
    case POST_HX711: return "LOAD CELL";
    case POST_S2:    return "S2 BUS";
    case POST_S3:    return "S3 SUPPLY";
    case POST_VRAIL: return "VALVE RAIL";
    case POST_DRV:   return "VALVE DRV";
    case POST_INFO_HSI: return "CLOCK HSI";
    case POST_INFO_LSI: return "RTC LSI";
    case POST_INFO_CPU: return "RESTART";
    case POST_IMAGE: return "FW IMAGE";
    default:         return "?";
    }
}

void post_start(uint32_t now)
{
    bsp_diag_t d;
    res = 0;
    bsp_diag(&d);
    if (!d.hse) res |= POST_INFO_HSI;
    if (!d.lse) res |= POST_INFO_LSI;
    uint32_t rf = bsp_reset_flags();
    if ((rf & RST_IWDG) || ((rf & RST_SOFT) && d.fault_code != 0u)) res |= POST_INFO_CPU;
    if (!store_selftest()) res |= POST_FRAM;
    { uint32_t crc; if (!bsp_image_check(&crc)) res |= POST_IMAGE; }
    t0 = now;
    st = PS_SENSORS;
}

static void finish(void)
{
    out_safe();
    st = PS_DONE;
    if (res & POST_FRAM)  alarm_raise(ALM_STORAGE);
    if (res & (POST_RAILS | POST_VRAIL)) alarm_raise(ALM_POWER_RAIL);
    if (res & (POST_HX711 | POST_S2 | POST_S3)) alarm_raise(ALM_SENSOR);
    if (res & POST_DRV)   alarm_raise(ALM_DRV_SELFTEST);
    if (res & POST_INFO_CPU) alarm_raise(ALM_CPU_RESTART);
    if (res & POST_IMAGE) alarm_raise(ALM_FW_IMAGE);
    ev_log(EV_POST, res, 0);
}

void post_tick(uint32_t now)
{
    const sensors_t *s = sensors();
    uint32_t el = elapsed(now, t0);
    switch (st) {
    case PS_SENSORS:
        /* HX711 at 10 SPS needs up to ~0.5 s after power-up for its first settled sample */
        if ((s->weight_ok && el >= 200u) || el >= 1200u) {
            if (!s->weight_ok) res |= POST_HX711;
            if (!sensors_rails_ok()) res |= POST_RAILS;
            if (!s->p_bus_ok) res |= POST_S2;
            if (!s->p_supply_ok) res |= POST_S3;
            if (res & POST_RAILS) { finish(); break; }         /* never switch the rail on a bad supply */
            out_rail(true);
            t0 = now;
            st = PS_RAIL;
        }
        break;
    case PS_RAIL:
        if (out_rail_ready()) {
            if (out_selftest_failed()) res |= POST_DRV;
            finish();
        } else if (el >= 1000u) {
            res |= POST_VRAIL;
            finish();
        }
        break;
    case PS_IDLE:
    case PS_DONE:
    default:
        break;
    }
}
