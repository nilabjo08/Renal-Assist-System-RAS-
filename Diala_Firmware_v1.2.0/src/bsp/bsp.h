/* Board support interface. Implemented by bsp_stm32.c (target hardware) and bsp_sim.c
 * (host simulator). The application layer never touches registers directly. */
#ifndef BSP_H
#define BSP_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ADC_P_BUS = 0,   /* S2 bus pressure   PC0 / ADC123_IN10 */
    ADC_P_SUPPLY,    /* S3 supply         PC1 / ADC123_IN11 */
    ADC_V24_MON,     /* 24 V input        PC2 / ADC123_IN12 */
    ADC_PH,          /* S5 pH (option)    PC3 / ADC123_IN13 */
    ADC_V5_MON,      /* 5 V rail / 2      PA1 / ADC123_IN1  */
    ADC_VRAIL_MON,   /* valve rail        PC4 / ADC12_IN14  */
    ADC_PUMP_I,      /* pump shunt 1 ohm  PB0 / ADC12_IN8   */
    ADC_COUNT
} adc_ch_t;

#define KEY_START 0x01u
#define KEY_STOP  0x02u
#define KEY_MUTE  0x04u
#define KEY_UP    0x08u
#define KEY_DOWN  0x10u
#define KEY_ENTER 0x20u

#define RST_POWER_ON  0x01u
#define RST_IWDG      0x02u
#define RST_PIN       0x04u   /* NRST: reset key, SWD or external watchdog U4 */
#define RST_SOFT      0x08u   /* software reset (after a CPU fault or clock failure) */

void     bsp_init(void);
uint32_t bsp_millis(void);
uint32_t bsp_reset_flags(void);

/* outputs */
void     bsp_valve_rail(bool on);         /* VALVE_EN_REQ (ANDed in hardware with watchdog OK) */
void     bsp_valve_write(uint16_t mask);  /* bit0..9 = V1..V10, bit10 = spare output; ignored (lines held low) while the rail is off */
bool     bsp_valve_nfault(void);          /* true while DRV8860 nFAULT is low                  */
void     bsp_valve_fault_reset(void);     /* DRV8860 "Fault Register Reset" special command    */
void     bsp_pump(uint8_t duty_pct);
void     bsp_buzzer(bool on);
void     bsp_leds(bool green, bool red);
void     bsp_lcd_backlight(uint8_t pct);
void     bsp_wdg_kick(void);              /* toggles TPS3823 WDI and reloads the IWDG         */

/* inputs */
void     bsp_adc_read(uint16_t raw[ADC_COUNT]);
bool     bsp_hx711_read(int32_t *raw);    /* true when a new sample was read                  */
uint32_t bsp_flow_count(void);            /* free-running S4 pulse count                      */
uint8_t  bsp_keys_raw(void);              /* KEY_* bits, 1 = pressed                          */
bool     bsp_liquid_detect(void);         /* S8 liquid at V outlet                            */
bool     bsp_dialyser_present(void);      /* S6                                               */
bool     bsp_conc_low(void);              /* S7                                               */

/* 20 x 4 character LCD, row 0..3, exactly 20 characters */
void     bsp_lcd_row(uint8_t row, const char text[20]);

/* FRAM MB85RC512T, 64 KB */
#define NV_SIZE 65536u
bool     bsp_nv_read(uint32_t addr, void *buf, uint32_t len);
bool     bsp_nv_write(uint32_t addr, const void *buf, uint32_t len);

/* RTC: seconds since 2000-01-01 00:00:00 */
uint32_t bsp_rtc_get(void);
void     bsp_rtc_set(uint32_t secs);

/* service UART (CH340C, 115200 8N1). TX is interrupt driven from a ring buffer and never blocks:
 * bsp_uart_write() queues what fits and returns the number of bytes queued. */
#define UART_TX_BUF 2048u
int      bsp_uart_getc(void);             /* -1 when no byte waiting */
uint32_t bsp_uart_write(const char *s, uint32_t n);
uint32_t bsp_uart_tx_free(void);

/* DS18B20 temperature probe on J4 (PA8, 1-Wire), v1.2. Optional: absent = fetch returns false. */
void     bsp_temp_start(void);            /* start a 12-bit conversion (needs 750 ms)        */
bool     bsp_temp_fetch(int16_t *c_x10);  /* read the result; false = no probe or CRC error  */

/* firmware image check (v1.2): CRC-32 of the flash image against the value the build appended */
bool     bsp_image_check(uint32_t *crc);

/* board diagnostics (v1.1) */
typedef struct {
    bool     hse;             /* true: 8 MHz crystal running; false: internal 16 MHz HSI      */
    bool     hse_forced_off;  /* crystal disabled after a clock-security event (until cleared) */
    bool     lse;             /* true: 32.768 kHz crystal drives the RTC; false: LSI           */
    uint32_t fault_pc;        /* program counter of the last CPU fault (0 = none)              */
    uint32_t fault_code;      /* 1 HardFault 2 MemManage 3 BusFault 4 UsageFault 5 clock (CSS) */
} bsp_diag_t;
void     bsp_diag(bsp_diag_t *d);
void     bsp_diag_clear(void);            /* clears the fault record and re-allows the crystal */
uint16_t bsp_valve_readback(void);        /* 16 bits shifted out of the DRV8860 chain on the last write (raw) */
void     bsp_valve_control(uint8_t ctrl);  /* DRV8860 "Write Control Register" to both devices (energise / hold PWM) */
uint16_t bsp_valve_read_control(void);    /* DRV8860 "Read Control Register": U11 byte << 8 | U10 byte, raw */
void     bsp_lcd_reinit(void);            /* resynchronise the HD44780 (4-bit) and force a full redraw */

#endif
