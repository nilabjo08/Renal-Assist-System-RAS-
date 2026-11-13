#include "events.h"
#include "../bsp/bsp.h"

static event_t ring[EV_CAPACITY];
static uint32_t total;
static ev_sink_t sink;

void ev_set_sink(ev_sink_t s) { sink = s; }
uint32_t ev_count(void) { return total; }

void ev_log(ev_type_t t, uint32_t a, int32_t b)
{
    event_t *e = &ring[total % EV_CAPACITY];
    e->ms = bsp_millis();
    e->type = (uint8_t)t;
    e->pad = 0;
    e->a = (uint16_t)a;
    e->b = b;
    total++;
    if (sink) sink(e);
}

bool ev_get(uint32_t newest_index, event_t *out)
{
    uint32_t held = total < EV_CAPACITY ? total : EV_CAPACITY;
    if (newest_index >= held) return false;
    *out = ring[(total - 1u - newest_index) % EV_CAPACITY];
    return true;
}

const char *ev_name(uint8_t type)
{
    static const char *const n[EV_COUNT] = { "BOOT", "SELFTEST", "ALARM", "KEY", "INPUT", "RAIL", "VALVES",
                                             "PUMP", "PROGRAM", "STEP", "TEMP", "KITTEST" };
    return type < EV_COUNT ? n[type] : "?";
}
