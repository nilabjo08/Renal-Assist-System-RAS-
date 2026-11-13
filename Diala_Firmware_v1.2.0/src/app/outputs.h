/* Output layer. All valve and pump commands go through here so the interlocks
 * (SAF-01, SAF-02, PCB-03) are enforced in one place. */
#ifndef OUTPUTS_H
#define OUTPUTS_H
#include <stdint.h>
#include <stdbool.h>

#define OUT_ALLOW_TANK   0x01u   /* step may open V5 (tank) */

void     out_init(void);
void     out_tick(uint32_t now);          /* every 10 ms: refresh, rail settle, fault monitor */
void     out_rail(bool on);
bool     out_rail_ready(void);            /* rail on, settled, fault flags cleared */
bool     out_set(uint16_t mask, uint8_t flags);   /* false = refused by an interlock */
void     out_pump(bool on);
void     out_all_off(void);               /* valves and pump off, rail stays */
void     out_safe(void);                  /* everything off including the rail */
uint16_t out_valves(void);
bool     out_pump_on(void);
bool     out_fault(void);                 /* driver fault confirmed while monitoring */
bool     out_selftest_failed(void);       /* nFAULT stayed low after the fault reset at the latest rail-up */
uint32_t out_selftest_fail_count(void);   /* increments on every failed rail-up check */
bool     out_rail_on(void);               /* rail requested (settling or ready) */
uint8_t  out_hold_ctrl(bool *verified);   /* control byte written at the last rail-up, 0 = 100 % on */
void     out_monitor_enable(bool en);

#endif
