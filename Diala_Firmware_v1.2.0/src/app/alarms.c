#include "alarms.h"
#include "util.h"
#include "events.h"
#include "../bsp/bsp.h"

typedef struct { const char *text; const char *hint; alarm_sev_t sev; } alarm_def_t;

static const alarm_def_t defs[ALM_COUNT] = {
    [ALM_NONE]         = { "",                     "",                     SEV_INFO },
    [ALM_LOW_SUPPLY]   = { "LOW WATER PRESSURE",   "Check RO supply",      SEV_STOP },
    [ALM_VACUUM]       = { "CONNECTOR/VACUUM",     "Check 4 connectors",   SEV_STOP },
    [ALM_TCV_FAIL]     = { "TCV FAIL <80%",        "Discard dialyser",     SEV_STOP },
    [ALM_LEAK_FAIL]    = { "LEAK TEST FAIL",       "Discard dialyser",     SEV_STOP },
    [ALM_ADD_CHEM]     = { "ADD CHEMICAL",         "Refill concentrate",   SEV_STOP },
    [ALM_OVERFILL]     = { "TANK OVERFILL",        "Tank drained; check",  SEV_STOP },
    [ALM_VALVE_FAULT]  = { "VALVE FAULT",          "Check valve wiring",   SEV_STOP },
    [ALM_TIMEOUT]      = { "STEP TIMEOUT",         "Call service",         SEV_STOP },
    [ALM_POWER_LOSS]   = { "POWER WAS LOST",       "Vent before removing", SEV_WARN },
    [ALM_SENSOR]       = { "SENSOR FAULT",         "Call service",         SEV_STOP },
    [ALM_POWER_RAIL]   = { "SUPPLY VOLTAGE FAULT", "Call service",         SEV_STOP },
    [ALM_DLZ_REMOVED]  = { "DIALYSER REMOVED",     "Cycle aborted",        SEV_STOP },
    [ALM_DLZ_FITTED]   = { "REMOVE DIALYSER",      "Fit bypass connector", SEV_STOP },
    [ALM_NO_DIALYSER]  = { "NO DIALYSER FITTED",   "Fit dialyser",         SEV_STOP },
    [ALM_RINSE_NEEDED] = { "RINSE-OUT NEEDED",     "Run RINSE-OUT first",  SEV_STOP },
    [ALM_CAL_NEEDED]   = { "CALIBRATION LOST",     "Calibrate (service)",  SEV_WARN },
    [ALM_STOPPED]      = { "STOPPED BY USER",      "Cycle aborted",        SEV_STOP },
    [ALM_DRV_SELFTEST] = { "VALVE DRIVER CHECK",   "Plugs J10-J19? svc",   SEV_WARN },
    [ALM_STORAGE]      = { "MEMORY FAULT",         "Call service",         SEV_STOP },
    [ALM_PUMP_FAULT]   = { "DOSING PUMP FAULT",    "Check pump wiring",    SEV_STOP },
    [ALM_CPU_RESTART]  = { "CONTROLLER RESTARTED", "Vent; see diag",       SEV_WARN },
    [ALM_FW_IMAGE]     = { "FIRMWARE CHECK FAIL",  "Reflash firmware",     SEV_STOP },
};

static alarm_t active;
static bool muted;
static uint32_t beep_until;
static bool beep_req;
static uint16_t beep_ms;

const char *alarm_text(alarm_t a) { return a < ALM_COUNT ? defs[a].text : "?"; }
const char *alarm_hint(alarm_t a) { return a < ALM_COUNT ? defs[a].hint : ""; }
alarm_sev_t alarm_severity(alarm_t a) { return a < ALM_COUNT ? defs[a].sev : SEV_STOP; }

void alarm_raise(alarm_t a)
{
    if (a == ALM_NONE || a >= ALM_COUNT) return;
    ev_log(EV_ALARM, (uint32_t)a, 0);
    if (active == ALM_NONE || defs[a].sev >= defs[active].sev) { active = a; muted = false; }
}

void alarm_ack(void)  { active = ALM_NONE; muted = false; }
void alarm_mute(void) { muted = true; }
alarm_t alarm_active(void) { return active; }
bool alarm_muted(void) { return muted; }
void alarm_beep(uint16_t ms) { beep_req = true; beep_ms = ms; }

void alarm_tick(uint32_t now)
{
    if (beep_req) { beep_until = now + beep_ms; beep_req = false; }
    bool beeping = (int32_t)(beep_until - now) > 0;
    bool on = false;
    if (active != ALM_NONE && !muted) {
        uint32_t ph = now % 1000u;
        on = defs[active].sev == SEV_STOP ? (ph < 250u || (ph >= 500u && ph < 750u)) : ph < 150u;
    }
    bsp_buzzer(on || beeping);
}
