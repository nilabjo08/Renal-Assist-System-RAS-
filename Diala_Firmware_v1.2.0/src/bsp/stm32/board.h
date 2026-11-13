/* Diala main controller PCB RC4 (pin-identical to RC2/RC3) - STM32F407VET6 (LQFP-100) pin map.
 * Each line carries "U6-<pin> <net>" exactly as on the fabricated RC4 board (hw/diala_main_rc4_pads.csv).
 * tools/check_pinmap.py verifies port/pin, U6 pin number and net name for every entry. */
#ifndef BOARD_H
#define BOARD_H

/*            name              port   pin      netlist                  */
#define PIN_BUZZER          GPIOE,  2u   /* U6-1  BUZZER         */
#define PIN_VALVE_EN_REQ    GPIOE,  3u   /* U6-2  VALVE_EN_REQ   */
#define PIN_WDG_KICK        GPIOE,  4u   /* U6-3  WDG_KICK       */
#define PIN_LED_GREEN       GPIOE,  5u   /* U6-4  LED_GREEN      */
#define PIN_LED_RED         GPIOE,  6u   /* U6-5  LED_RED        */
#define PIN_P_BUS           GPIOC,  0u   /* U6-15 P_BUS          ADC123_IN10 */
#define PIN_P_SUPPLY        GPIOC,  1u   /* U6-16 P_SUPPLY       ADC123_IN11 */
#define PIN_V24_MON         GPIOC,  2u   /* U6-17 V24_MON        ADC123_IN12 */
#define PIN_PH_IN           GPIOC,  3u   /* U6-18 PH_IN          ADC123_IN13 */
#define PIN_FLOW_PULSE      GPIOA,  0u   /* U6-23 FLOW_PULSE     TIM2_ETR    */
#define PIN_V5_MON          GPIOA,  1u   /* U6-24 V5_MON         ADC123_IN1  */
#define PIN_VRAIL_MON       GPIOC,  4u   /* U6-33 VRAIL_MON      ADC12_IN14  */
#define PIN_PUMP_ISENSE     GPIOB,  0u   /* U6-35 PUMP_ISENSE    ADC12_IN8   */
#define PIN_LCD_RS          GPIOE,  7u   /* U6-38 LCD_RS         */
#define PIN_LCD_E           GPIOE,  8u   /* U6-39 LCD_E          */
#define PIN_LCD_D4          GPIOE,  9u   /* U6-40 LCD_D4         */
#define PIN_LCD_D5          GPIOE, 10u   /* U6-41 LCD_D5         */
#define PIN_LCD_D6          GPIOE, 11u   /* U6-42 LCD_D6         */
#define PIN_LCD_D7          GPIOE, 12u   /* U6-43 LCD_D7         */
#define PIN_LCD_BL          GPIOE, 13u   /* U6-44 LCD_BL_PWM     */
#define PIN_LIQ_DET         GPIOE, 14u   /* U6-45 LIQ_DET        */
#define PIN_DLZ_PRESENT     GPIOE, 15u   /* U6-46 DLZ_PRESENT    */
#define PIN_CONC_LOW        GPIOB, 10u   /* U6-47 CONC_LOW       */
#define PIN_VLV_NFAULT      GPIOB, 11u   /* U6-48 VLV_nFAULT     */
#define PIN_VLV_LATCH       GPIOB, 12u   /* U6-51 VLV_LATCH      */
#define PIN_VLV_SCK         GPIOB, 13u   /* U6-52 VLV_SCK        */
#define PIN_VLV_MISO        GPIOB, 14u   /* U6-53 VLV_MISO       */
#define PIN_VLV_MOSI        GPIOB, 15u   /* U6-54 VLV_MOSI       */
#define PIN_PUMP_PWM        GPIOD, 12u   /* U6-59 PUMP_PWM       TIM4_CH1    */
#define PIN_KEY_START       GPIOC,  6u   /* U6-63 KEY_START      */
#define PIN_KEY_STOP        GPIOC,  7u   /* U6-64 KEY_STOP       */
#define PIN_KEY_MUTE        GPIOC,  8u   /* U6-65 KEY_MUTE       */
#define PIN_KEY_UP          GPIOC,  9u   /* U6-66 KEY_UP         */
#define PIN_UART_TX         GPIOA,  9u   /* U6-68 /MCU/UART_TX   USART1_TX   */
#define PIN_UART_RX         GPIOA, 10u   /* U6-69 /MCU/UART_RX   USART1_RX   */
#define PIN_KEY_DOWN        GPIOC, 10u   /* U6-78 KEY_DOWN       */
#define PIN_KEY_ENTER       GPIOC, 11u   /* U6-79 KEY_ENTER      */
#define PIN_I2C_SCL         GPIOB,  8u   /* U6-95 /MCU/I2C_SCL   FRAM U7     */
#define PIN_I2C_SDA         GPIOB,  9u   /* U6-96 /MCU/I2C_SDA   FRAM U7     */
#define PIN_HX_SCK          GPIOE,  0u   /* U6-97 HX_SCK         */
#define PIN_HX_DOUT         GPIOE,  1u   /* U6-98 HX_DOUT        */
#define PIN_TEMP_1W         GPIOA,  8u   /* U6-67 /MCU/SP_PA8    J4 pin 9: DS18B20 data, 4.7k to J4 pin 14 (3V3) */

/* ADC channel numbers for the analog pins above */
#define ADCCH_P_BUS       10u
#define ADCCH_P_SUPPLY    11u
#define ADCCH_V24_MON     12u
#define ADCCH_PH          13u
#define ADCCH_V5_MON       1u
#define ADCCH_VRAIL_MON   14u
#define ADCCH_PUMP_I       8u

/* Input polarities [verify on bench with the chosen sensors/switches] */
#define LIQ_ACTIVE_HIGH   1   /* S8 optical sensor output high = liquid in line   */
#define DLZ_ACTIVE_LOW    1   /* S6 dry contact to GND closed = dialyser fitted    */
#define CONC_ACTIVE_LOW   1   /* S7 dry contact to GND closed = concentrate low    */

/* DRV8860 daisy chain: MOSI -> U10 DIN, U10 DOUT -> U11 DIN, U11 DOUT -> MISO.
 * U10 OUT1..8 = V1..V8, U11 OUT1..2 = V9..V10, U11 OUT3 = spare (J20). */
#define DRV_CHAIN_LEN     2u
#define FRAM_I2C_ADDR     0x50u   /* A0-A2 tied low */

#endif
