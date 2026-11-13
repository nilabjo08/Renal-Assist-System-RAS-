/* Event log (v1.2). Everything the machine does that a test needs to see, with a millisecond
 * time stamp: resets, self-test results, alarms, keys, S6/S7/S8 changes, valve-rail checks, valve
 * and pump commands, program start/end, step ends, temperature thresholds, kit-test results.
 * The last EV_CAPACITY events are kept in RAM (`events [n]` prints them); with `events on`, or
 * while a kit test runs, each event is also sent over USB the moment it happens. */
#ifndef EVENTS_H
#define EVENTS_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    EV_BOOT = 0,   /* a = reset flags, b = boot count            */
    EV_POST,       /* a = result bits                            */
    EV_ALARM,      /* a = alarm_t                                */
    EV_KEY,        /* a = KEY_* bit                              */
    EV_INPUT,      /* a = 6/7/8 (S6 dialyser, S7 conc, S8 liquid), b = new state */
    EV_RAIL,       /* a = 0 off, 1 ready, 2 ready but driver check FAILED, b = rail mV */
    EV_VALVES,     /* a = valve mask now                         */
    EV_PUMP,       /* a = 0/1                                    */
    EV_PROG,       /* a = program, b = 1 start, 0 end            */
    EV_STEP,       /* a = step index (see `steps`), b = end code */
    EV_TEMP,       /* b = driver temperature x10 (crossing a 5 degC band) */
    EV_KT,         /* a = kit-test item, b = status              */
    EV_COUNT
} ev_type_t;

typedef struct { uint32_t ms; uint8_t type; uint8_t pad; uint16_t a; int32_t b; } event_t;
#define EV_CAPACITY 256u

typedef void (*ev_sink_t)(const event_t *e);
void ev_log(ev_type_t t, uint32_t a, int32_t b);
void ev_set_sink(ev_sink_t s);
uint32_t ev_count(void);                          /* total logged since boot */
bool ev_get(uint32_t newest_index, event_t *out); /* 0 = newest              */
const char *ev_name(uint8_t type);

#endif
