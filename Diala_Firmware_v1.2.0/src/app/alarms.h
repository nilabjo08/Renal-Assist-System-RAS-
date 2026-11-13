#ifndef ALARMS_H
#define ALARMS_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ALM_NONE = 0,
    ALM_LOW_SUPPLY,        /* supply < 15 psig for 3 s                     */
    ALM_VACUUM,            /* connector check: -100 mmHg not reached       */
    ALM_TCV_FAIL,          /* TCV < 80 % of baseline                       */
    ALM_LEAK_FAIL,         /* vacuum loss over limit, or -250 not reached  */
    ALM_ADD_CHEM,          /* concentrate low switch or dose not reached   */
    ALM_OVERFILL,          /* tank > 1,200 g                               */
    ALM_VALVE_FAULT,       /* DRV8860 nFAULT (open load / short / temp)    */
    ALM_TIMEOUT,           /* step exceeded its maximum time               */
    ALM_POWER_LOSS,        /* restart after power loss during a cycle      */
    ALM_SENSOR,            /* load cell or pressure sensor not valid       */
    ALM_POWER_RAIL,        /* 24 V / 5 V / valve rail out of range         */
    ALM_DLZ_REMOVED,       /* dialyser removed during a cycle              */
    ALM_DLZ_FITTED,        /* disinfection blocked: dialyser fitted (SAF-03) */
    ALM_NO_DIALYSER,       /* patient program started without a dialyser  */
    ALM_RINSE_NEEDED,      /* germicide in lines: run rinse-out (SAN-03)   */
    ALM_CAL_NEEDED,        /* calibration lost: defaults loaded            */
    ALM_STOPPED,           /* operator pressed STOP                        */
    ALM_DRV_SELFTEST,      /* driver fault flag would not clear at start   */
    ALM_STORAGE,           /* FRAM not answering / write failed            */
    ALM_PUMP_FAULT,        /* dosing pump on but no current (open wire)    */
    ALM_CPU_RESTART,       /* restarted after a CPU fault or clock failure */
    ALM_FW_IMAGE,          /* v1.2: firmware image CRC wrong: reflash      */
    ALM_COUNT
} alarm_t;

typedef enum { SEV_INFO, SEV_WARN, SEV_STOP } alarm_sev_t;

const char *alarm_text(alarm_t a);        /* <= 20 chars, for LCD line */
const char *alarm_hint(alarm_t a);        /* <= 20 chars, what to do   */
alarm_sev_t alarm_severity(alarm_t a);

void    alarm_raise(alarm_t a);           /* latches; highest severity wins */
void    alarm_ack(void);                  /* clears the latched alarm        */
void    alarm_mute(void);                 /* silences buzzer, keeps alarm    */
alarm_t alarm_active(void);
bool    alarm_muted(void);
void    alarm_tick(uint32_t now);         /* drives buzzer pattern and red LED */
void    alarm_beep(uint16_t ms);          /* short confirmation beep         */

#endif
