#ifndef SENSORS_H
#define SENSORS_H
#include <stdint.h>
#include <stdbool.h>
#include "../bsp/bsp.h"

typedef struct {
    int32_t  weight_x10;       /* tank, g x10                          */
    bool     weight_ok;        /* fresh HX711 sample within 500 ms     */
    int32_t  scale_raw;
    int32_t  p_bus_x10;        /* S2, mmHg x10 (gauge)                 */
    bool     p_bus_ok;
    int32_t  p_supply_x10;     /* S3, psig x10                         */
    bool     p_supply_ok;
    int32_t  flow_x10;         /* S4, L/min x10                        */
    int32_t  ph_x10;           /* S5 option                            */
    uint32_t v24_mv, v5_mv, vrail_mv, pump_ma;
    bool     liquid;           /* S8                                   */
    bool     dialyser;         /* S6                                   */
    bool     conc_low;         /* S7                                   */
    int16_t  temp_x10;         /* v1.2: DS18B20 on J4 (valve driver), degC x10 */
    bool     temp_ok;          /* fresh reading within 2.5 s           */
    uint16_t adc[ADC_COUNT];
} sensors_t;

void sensors_init(void);
void sensors_tick(uint32_t now_ms);           /* call every 10 ms */
const sensors_t *sensors(void);
bool sensors_rails_ok(void);                  /* 24 V and 5 V inside limits */
int32_t sensors_weight_g(void);               /* rounded grams */

/* test hooks used by calibration */
int32_t sensors_bus_uncal_x10(void);
int32_t sensors_supply_uncal_x10(void);

#endif
