#ifndef SIM_H
#define SIM_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t ms;
    /* plant */
    double supply_psig;       /* static RO supply pressure        */
    uint32_t supply_fail_at_ms;
    double tank_g;
    bool   conc_available;    /* concentrate drum has liquid      */
    bool   conc_switch_low;   /* S7 says low                      */
    bool   dialyser;          /* S6                               */
    bool   bypass;            /* disinfection bypass connector fitted */
    double tcv_ml, wf_ml;     /* true dialyser and line volumes   */
    double blood_fill;
    double leak_mmhg_s;       /* dialysate-side vacuum decay      */
    bool   connector_leak;
    bool   venturi_broken;
    bool   v5_stuck;          /* tank valve stuck open            */
    uint16_t weep;            /* closed valves that leak air/water across the seat (seal test) */
    bool   pump_open;         /* pump wire broken: no current      */
    bool   fram_dead;         /* FRAM not answering                */
    /* v1.2 kit-test rig: */
    bool   temp_present; double temp_c;   /* DS18B20 on the driver; heats with valves on */
    uint16_t open_load;       /* outputs with no load connected (open-load fault while off) */
    bool   kit_pots; double s2_wiper_v, s3_wiper_v;   /* S2/S3 driven by potentiometers, 0-5 V */
    double flow_hz;           /* >0: pulse generator instead of the plant flow */
    int    s8_force;          /* -1 plant, 0 dry, 1 liquid */
    bool   image_bad;
    bool   s2_broken, s8_dead, hx_dead;
    int32_t hx_offset;
    double v24, v5;
    double p_bus, p_dial;
    /* actuators as seen by the plant */
    bool   rail;
    uint16_t valves;
    uint8_t pump_duty;
    bool   buzzer, led_g, led_r;
    bool   nfault, nfault_stuck_after_reset;
    uint32_t fault_resets;
    bool   illegal_write, v1_v2_together;
    uint8_t keys;
    uint32_t rtc_base;
    uint32_t last_kick_ms, kicks;
} sim_plant_t;

extern sim_plant_t P;
extern uint32_t sim_valve_writes;
extern double sim_cost_us;      /* CPU time the last app_poll() spent in blocking I/O (FRAM, LCD, ADC...) */
void sim_uart_drain(uint32_t ms);
extern uint32_t sim_uart_lines; /* lines that actually left the UART */
extern uint32_t sim_prompts;    /* kit-test PROMPT lines seen          */
extern char sim_last_prompt[200];

void sim_defaults(void);
void sim_fram_clear(void);
uint8_t *sim_fram(void);
void sim_physics(double dt_ms);
const char *sim_lcd_row(uint8_t r);
void sim_uart_send(const char *s);
const char *sim_uart_output(void);
void sim_uart_clear(void);

#endif
