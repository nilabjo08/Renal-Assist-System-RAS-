/* STM32F407VET6 board support for the Diala main controller PCB RC4 (pin-identical to RC2/RC3).
 * Register-level (CMSIS device header only), no vendor HAL.
 *
 * RC4 note, clock: Y1 uses the 4-pad 3225 footprint but only pads 1 and 2 are wired (HSE_IN,
 * HSE_OUT). On an ABM8-type part pads 1/3 are the crystal and 2/4 the lid, so a fitted crystal
 * will normally not oscillate. The firmware therefore runs from the internal 16 MHz HSI when the
 * crystal does not start, and the result is reported by `diag` and on the self-test screen.
 * Nothing on this board needs the crystal (USB is handled by the CH340C, the RTC uses Y2). */
#include "stm32f4xx.h"
#include "board.h"
#include "../bsp.h"
#include "../../app/config.h"
#include "../../app/util.h"

uint32_t SystemCoreClock = 16000000u;
void SysTick_Handler(void);
void USART1_IRQHandler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void fault_common(const uint32_t *frame, uint32_t code);
void SystemInit(void);
static volatile uint32_t g_ms;
static uint32_t g_reset_flags;
static bool g_hse, g_lse, g_rail;
static uint16_t g_readback;

/* RTC backup registers (kept by the CR2032 while the board is off) */
#define BKP_RTC_MAGIC   0x32F2u        /* BKP0R: RTC initialised                          */
#define BKP_NOHSE_MAGIC 0xC55F00D1u    /* BKP1R: crystal disabled after a CSS event        */
#define BKP_FAULT_MAGIC 0xFA017EC0u    /* BKP4R: BKP2R = fault code, BKP3R = fault PC      */

/* ------------------------------------------------------------------ helpers */
#define MODE_IN 0u
#define MODE_OUT 1u
#define MODE_AF 2u
#define MODE_AN 3u
#define PULL_NONE 0u
#define PULL_UP 1u

static void pin_cfg(GPIO_TypeDef *p, uint32_t n, uint32_t mode, uint32_t af, bool od, uint32_t pull)
{
    p->MODER   = (p->MODER & ~(3u << (n * 2u))) | (mode << (n * 2u));
    p->OTYPER  = (p->OTYPER & ~(1u << n)) | ((od ? 1u : 0u) << n);
    p->OSPEEDR = (p->OSPEEDR & ~(3u << (n * 2u))) | (2u << (n * 2u));
    p->PUPDR   = (p->PUPDR & ~(3u << (n * 2u))) | (pull << (n * 2u));
    if (n < 8u) p->AFR[0] = (p->AFR[0] & ~(0xFu << (n * 4u))) | (af << (n * 4u));
    else        p->AFR[1] = (p->AFR[1] & ~(0xFu << ((n - 8u) * 4u))) | (af << ((n - 8u) * 4u));
}
static inline void pin_set(GPIO_TypeDef *p, uint32_t n, bool v) { p->BSRR = v ? (1u << n) : (1u << (n + 16u)); }
static inline bool pin_get(GPIO_TypeDef *p, uint32_t n) { return ((p->IDR >> n) & 1u) != 0u; }
#define OUT(pin)          pin_cfg(pin, MODE_OUT, 0u, false, PULL_NONE)
#define OUT_OD(pin)       pin_cfg(pin, MODE_OUT, 0u, true, PULL_NONE)
#define IN(pin, pull)     pin_cfg(pin, MODE_IN, 0u, false, pull)
#define AN(pin)           pin_cfg(pin, MODE_AN, 0u, false, PULL_NONE)
#define AF(pin, af)       pin_cfg(pin, MODE_AF, af, false, PULL_NONE)
#define SET(pin, v)       pin_set(pin, v)
#define GET(pin)          pin_get(pin)

static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT, cycles = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < cycles) { }
}

void SysTick_Handler(void) { g_ms++; }
uint32_t bsp_millis(void) { return g_ms; }
uint32_t bsp_reset_flags(void) { return g_reset_flags; }

/* ------------------------------------------------------------------ clock: HSE 8 MHz or HSI 16 MHz -> 168 MHz */
void SystemInit(void)
{
    SCB->CPACR |= (3uL << 20) | (3uL << 22);                  /* FPU full access */
}

static void clock_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_VOS;
    bool try_hse = RTC->BKP1R != BKP_NOHSE_MAGIC;              /* crystal failed in service before */
    bool hse = false;
    if (try_hse) {
        RCC->CR |= RCC_CR_HSEON;
        for (uint32_t t = 0; t < 400000u && !(RCC->CR & RCC_CR_HSERDY); t++) { }   /* ~100 ms on HSI */
        hse = (RCC->CR & RCC_CR_HSERDY) != 0u;
        if (!hse) RCC->CR &= ~RCC_CR_HSEON;
    }
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_5WS;
    uint32_t pllm = hse ? 8u : 16u;                            /* VCO input 1 MHz either way */
    RCC->PLLCFGR = pllm | (336u << RCC_PLLCFGR_PLLN_Pos) | (0u << RCC_PLLCFGR_PLLP_Pos) |
                   (7u << RCC_PLLCFGR_PLLQ_Pos) | (hse ? RCC_PLLCFGR_PLLSRC_HSE : 0u);
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    SystemCoreClock = 168000000u;
    if (hse) RCC->CR |= RCC_CR_CSSON;                          /* crystal stops later -> NMI */
    g_hse = hse;
}

/* ------------------------------------------------------------------ RTC (LSE + CR2032) */
static uint8_t bcd2(uint32_t v) { return (uint8_t)(((v >> 4) & 0xFu) * 10u + (v & 0xFu)); }
static uint32_t tobcd(uint32_t v) { return ((v / 10u) << 4) | (v % 10u); }

static void rtc_init(void)
{
    PWR->CR |= PWR_CR_DBP;
    if (RTC->BKP0R == BKP_RTC_MAGIC && (RCC->BDCR & RCC_BDCR_RTCEN)) {     /* already running on VBAT */
        g_lse = (RCC->BDCR & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_0;
        return;
    }
    RCC->BDCR |= RCC_BDCR_LSEON;
    uint32_t t = 0;
    while (!(RCC->BDCR & RCC_BDCR_LSERDY) && t < 4000000u) t++;
    g_lse = (RCC->BDCR & RCC_BDCR_LSERDY) != 0u;
    if (g_lse) RCC->BDCR |= RCC_BDCR_RTCSEL_0;
    else { RCC->CSR |= RCC_CSR_LSION; while (!(RCC->CSR & RCC_CSR_LSIRDY)) { } RCC->BDCR |= RCC_BDCR_RTCSEL_1; }
    RCC->BDCR |= RCC_BDCR_RTCEN;
    bsp_rtc_set(secs_from_date(2026, 1, 1, 0, 0, 0));
    RTC->BKP0R = BKP_RTC_MAGIC;
}

void bsp_rtc_set(uint32_t secs)
{
    uint16_t y; uint8_t mo, d, h, mi, s;
    date_from_secs(secs, &y, &mo, &d, &h, &mi, &s);
    PWR->CR |= PWR_CR_DBP;
    RTC->WPR = 0xCAu; RTC->WPR = 0x53u;
    RTC->ISR |= RTC_ISR_INIT;
    uint32_t t = 0;
    while (!(RTC->ISR & RTC_ISR_INITF) && t < 1000000u) t++;
    RTC->PRER = (127u << 16) | (g_lse ? 255u : 249u);          /* 32768 Hz LSE or ~32 kHz LSI -> 1 Hz */
    RTC->TR = (tobcd(h) << 16) | (tobcd(mi) << 8) | tobcd(s);
    RTC->DR = (tobcd((uint32_t)(y % 100u)) << 16) | (1u << 13) | (tobcd(mo) << 8) | tobcd(d);
    RTC->CR &= ~RTC_CR_FMT;
    RTC->ISR &= ~RTC_ISR_INIT;
    RTC->WPR = 0xFFu;
}

uint32_t bsp_rtc_get(void)
{
    uint32_t tr = RTC->TR, dr = RTC->DR;
    return secs_from_date((uint16_t)(2000u + bcd2((dr >> 16) & 0xFFu)), bcd2((dr >> 8) & 0x1Fu), bcd2(dr & 0x3Fu),
                          bcd2((tr >> 16) & 0x3Fu), bcd2((tr >> 8) & 0x7Fu), bcd2(tr & 0x7Fu));
}

/* ------------------------------------------------------------------ UART1 (CH340C), RX and TX interrupt driven */
static volatile uint8_t rx_buf[256];
static volatile uint8_t rx_head, rx_tail;
static volatile char tx_buf[UART_TX_BUF];
static volatile uint16_t tx_head, tx_tail;

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t c = (uint8_t)USART1->DR;
        uint8_t nh = (uint8_t)(rx_head + 1u);
        if (nh != rx_tail) { rx_buf[rx_head] = c; rx_head = nh; }
    }
    if ((sr & USART_SR_TXE) && (USART1->CR1 & USART_CR1_TXEIE)) {
        if (tx_tail != tx_head) {
            USART1->DR = (uint8_t)tx_buf[tx_tail];
            tx_tail = (uint16_t)((tx_tail + 1u) % UART_TX_BUF);
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

int bsp_uart_getc(void)
{
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (uint8_t)(rx_tail + 1u);
    return c;
}

uint32_t bsp_uart_tx_free(void)
{
    uint32_t used = ((uint32_t)tx_head + UART_TX_BUF - tx_tail) % UART_TX_BUF;
    return UART_TX_BUF - 1u - used;
}

uint32_t bsp_uart_write(const char *s, uint32_t n)
{
    uint32_t i = 0;
    for (; i < n; i++) {
        uint16_t nh = (uint16_t)((tx_head + 1u) % UART_TX_BUF);
        if (nh == tx_tail) break;                               /* full: caller retries later */
        tx_buf[tx_head] = s[i];
        tx_head = nh;
    }
    if (i) USART1->CR1 |= USART_CR1_TXEIE;
    return i;
}

/* ------------------------------------------------------------------ ADC1 */
static uint16_t adc_one(uint32_t ch)
{
    ADC1->SQR3 = ch;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t t = 0;
    while (!(ADC1->SR & ADC_SR_EOC) && t < 20000u) t++;
    return (uint16_t)ADC1->DR;
}

void bsp_adc_read(uint16_t raw[ADC_COUNT])
{
    static const uint8_t ch[ADC_COUNT] = { ADCCH_P_BUS, ADCCH_P_SUPPLY, ADCCH_V24_MON, ADCCH_PH, ADCCH_V5_MON, ADCCH_VRAIL_MON, ADCCH_PUMP_I };
    for (uint32_t i = 0; i < ADC_COUNT; i++) {
        uint32_t sum = 0;
        for (uint32_t k = 0; k < 4u; k++) sum += adc_one(ch[i]);
        raw[i] = (uint16_t)(sum / 4u);
    }
}

/* ------------------------------------------------------------------ HX711 load cell (10 SPS, gain 128, ch A) */
bool bsp_hx711_read(int32_t *out)
{
    if (GET(PIN_HX_DOUT)) return false;                        /* not ready */
    uint32_t v = 0;
    __disable_irq();                                           /* SCK high > 60 us powers the HX711 down */
    for (uint32_t i = 0; i < 24u; i++) {
        SET(PIN_HX_SCK, true); delay_us(1);
        v = (v << 1) | (GET(PIN_HX_DOUT) ? 1u : 0u);
        SET(PIN_HX_SCK, false); delay_us(1);
    }
    SET(PIN_HX_SCK, true); delay_us(1); SET(PIN_HX_SCK, false); /* 25th pulse: next = ch A, gain 128 */
    __enable_irq();
    if (v & 0x800000u) v |= 0xFF000000u;
    *out = (int32_t)v;
    return true;
}

/* ------------------------------------------------------------------ DRV8860 x2 daisy chain (bit-banged, timing per SLRS065E 7.6) */
static void drv_clk(bool bit)
{
    SET(PIN_VLV_MOSI, bit); delay_us(1);
    SET(PIN_VLV_SCK, true); delay_us(1);
    SET(PIN_VLV_SCK, false); delay_us(1);
}

static void drv_lines_idle(void)
{
    SET(PIN_VLV_MOSI, false); SET(PIN_VLV_SCK, false); SET(PIN_VLV_LATCH, false);
}

void bsp_valve_write(uint16_t mask)
{
    if (!g_rail) { drv_lines_idle(); return; }                 /* DRV8860s unpowered: never back-feed them */
    uint16_t rb = 0;
    uint8_t u10 = (uint8_t)(mask & 0xFFu);                                 /* V1..V8  -> U10 OUT1..OUT8 */
    uint8_t u11 = (uint8_t)(((mask >> 8) & 0x03u) | ((mask & SPARE_OUT) ? 0x04u : 0u)); /* V9, V10, spare */
    const uint8_t bytes[DRV_CHAIN_LEN] = { u11, u10 };                    /* last device in chain first */
    SET(PIN_VLV_LATCH, false); delay_us(2);
    for (uint32_t b = 0; b < DRV_CHAIN_LEN; b++)
        for (int8_t i = 7; i >= 0; i--) {
            rb = (uint16_t)((rb << 1) | (GET(PIN_VLV_MISO) ? 1u : 0u));      /* raw DOUT, diagnostics only */
            drv_clk(((bytes[b] >> i) & 1u) != 0u);                          /* D8 first */
        }
    g_readback = rb;
    delay_us(1);
    SET(PIN_VLV_LATCH, true); delay_us(3);                                 /* data latched on the rising edge */
    SET(PIN_VLV_LATCH, false); delay_us(2);
}

/* Special commands (SLRS065E 8.5.1.3): CLK cycles in four LATCH segments; the segment start level is
 * DRV_SPECIAL_START_HIGH. TI gives the waveform only as a figure [bench item B1: scope it]. The same
 * routine serves Fault Register Reset (1,2,4,3), Write Control (1,2,2,3) and Read Control (1,4,2,3),
 * so once nFAULT is seen to release after the reset, the control-register commands use a proven waveform. */
#define DRV_SPECIAL_START_HIGH 1
static void drv_special(const uint8_t parts[4])
{
    bool latch = DRV_SPECIAL_START_HIGH != 0;
    for (uint32_t p = 0; p < 4u; p++) {
        SET(PIN_VLV_LATCH, latch); delay_us(2);
        for (uint32_t c = 0; c < parts[p]; c++) drv_clk(false);
        latch = !latch;
    }
    SET(PIN_VLV_LATCH, false); delay_us(2);
}

void bsp_valve_fault_reset(void)
{
    static const uint8_t parts[4] = { 1u, 2u, 4u, 3u };
    if (!g_rail) { drv_lines_idle(); return; }
    drv_special(parts);
}

void bsp_valve_control(uint8_t ctrl)
{
    static const uint8_t parts[4] = { 1u, 2u, 2u, 3u };
    if (!g_rail) { drv_lines_idle(); return; }
    drv_special(parts);
    for (uint32_t b = 0; b < DRV_CHAIN_LEN; b++)
        for (int8_t i = 7; i >= 0; i--) drv_clk(((ctrl >> i) & 1u) != 0u);
    delay_us(1);
    SET(PIN_VLV_LATCH, true); delay_us(3);
    SET(PIN_VLV_LATCH, false); delay_us(2);
}

uint16_t bsp_valve_read_control(void)
{
    static const uint8_t parts[4] = { 1u, 4u, 2u, 3u };
    if (!g_rail) { drv_lines_idle(); return 0u; }
    drv_special(parts);
    uint16_t v = 0;
    for (uint32_t i = 0; i < 8u * DRV_CHAIN_LEN; i++) {
        v = (uint16_t)((v << 1) | (GET(PIN_VLV_MISO) ? 1u : 0u));
        drv_clk(false);                                        /* falling edge shifts the next bit out */
    }
    return v;
}

bool bsp_valve_nfault(void) { return g_rail && !GET(PIN_VLV_NFAULT); }
uint16_t bsp_valve_readback(void) { return g_readback; }
void bsp_valve_rail(bool on)
{
    if (!on) drv_lines_idle();
    g_rail = on;
    SET(PIN_VALVE_EN_REQ, on);
}

/* ------------------------------------------------------------------ pump PWM: TIM4_CH1 on PD12, 20 kHz */
#define PUMP_ARR 4199u
void bsp_pump(uint8_t pct) { TIM4->CCR1 = (uint32_t)(pct > 100u ? 100u : pct) * (PUMP_ARR + 1u) / 100u; }

/* ------------------------------------------------------------------ misc outputs and inputs */
void bsp_buzzer(bool on) { SET(PIN_BUZZER, on); }
void bsp_leds(bool g, bool r) { SET(PIN_LED_GREEN, g); SET(PIN_LED_RED, r); }
void bsp_lcd_backlight(uint8_t pct) { SET(PIN_LCD_BL, pct > 0u); }

void bsp_wdg_kick(void)
{
    static bool lvl;
    lvl = !lvl;
    SET(PIN_WDG_KICK, lvl);          /* TPS3823 WDI: any edge restarts its 1.6 s timer */
    IWDG->KR = 0xAAAAu;
}

uint32_t bsp_flow_count(void) { return TIM2->CNT; }

uint8_t bsp_keys_raw(void)            /* membrane keys to GND, internal pull-ups */
{
    uint8_t k = 0;
    if (!GET(PIN_KEY_START)) k |= KEY_START;
    if (!GET(PIN_KEY_STOP))  k |= KEY_STOP;
    if (!GET(PIN_KEY_MUTE))  k |= KEY_MUTE;
    if (!GET(PIN_KEY_UP))    k |= KEY_UP;
    if (!GET(PIN_KEY_DOWN))  k |= KEY_DOWN;
    if (!GET(PIN_KEY_ENTER)) k |= KEY_ENTER;
    return k;
}

bool bsp_liquid_detect(void)    { return GET(PIN_LIQ_DET) == (LIQ_ACTIVE_HIGH != 0); }
bool bsp_dialyser_present(void) { return GET(PIN_DLZ_PRESENT) != (DLZ_ACTIVE_LOW != 0); }
bool bsp_conc_low(void)         { return GET(PIN_CONC_LOW) != (CONC_ACTIVE_LOW != 0); }

/* ------------------------------------------------------------------ LCD HD44780 20x4, 4-bit, write-only (RW tied low) */
static void lcd_nibble(uint8_t n)
{
    SET(PIN_LCD_D4, n & 1u); SET(PIN_LCD_D5, n & 2u); SET(PIN_LCD_D6, n & 4u); SET(PIN_LCD_D7, n & 8u);
    delay_us(1); SET(PIN_LCD_E, true); delay_us(1); SET(PIN_LCD_E, false); delay_us(1);
}
static void lcd_byte(bool rs, uint8_t b)
{
    SET(PIN_LCD_RS, rs);
    lcd_nibble((uint8_t)(b >> 4)); lcd_nibble((uint8_t)(b & 0xFu));
    delay_us(45);
}
static char lcd_cache[4][20];

static void lcd_init(void)
{
    delay_us(50000);
    SET(PIN_LCD_RS, false);
    lcd_nibble(3); delay_us(4500); lcd_nibble(3); delay_us(150); lcd_nibble(3); delay_us(150); lcd_nibble(2); delay_us(150);
    lcd_byte(false, 0x28); lcd_byte(false, 0x0C); lcd_byte(false, 0x01); delay_us(2000); lcd_byte(false, 0x06);
    for (uint32_t r = 0; r < 4u; r++) for (uint32_t c = 0; c < 20u; c++) lcd_cache[r][c] = ' ';
}

void bsp_lcd_reinit(void)
{
    SET(PIN_LCD_RS, false);
    lcd_nibble(3); delay_us(4500); lcd_nibble(3); delay_us(150); lcd_nibble(3); delay_us(150); lcd_nibble(2); delay_us(150);
    lcd_byte(false, 0x28); lcd_byte(false, 0x0C); lcd_byte(false, 0x06);
    for (uint32_t r = 0; r < 4u; r++) for (uint32_t c = 0; c < 20u; c++) lcd_cache[r][c] = '\0';   /* force redraw */
}

void bsp_lcd_row(uint8_t row, const char text[20])
{
    static const uint8_t base[4] = { 0x00, 0x40, 0x14, 0x54 };
    if (row > 3u) return;
    bool same = true;
    for (uint32_t c = 0; c < 20u; c++) if (lcd_cache[row][c] != text[c]) { same = false; break; }
    if (same) return;
    lcd_byte(false, (uint8_t)(0x80u | base[row]));
    for (uint32_t c = 0; c < 20u; c++) { lcd_byte(true, (uint8_t)text[c]); lcd_cache[row][c] = text[c]; }
}

/* ------------------------------------------------------------------ FRAM MB85RC512T on bit-banged I2C (PB8/PB9, 4.7k pull-ups) */
static void i2c_d(void) { delay_us(2); }
static void scl(bool v) { SET(PIN_I2C_SCL, v); i2c_d(); }
static void sda(bool v) { SET(PIN_I2C_SDA, v); i2c_d(); }
static void i2c_start(void) { sda(true); scl(true); sda(false); scl(false); }
static void i2c_stop(void)  { sda(false); scl(true); sda(true); }
static bool i2c_wr(uint8_t b)
{
    for (int8_t i = 7; i >= 0; i--) { sda(((b >> i) & 1u) != 0u); scl(true); scl(false); }
    sda(true); scl(true);
    bool ack = !GET(PIN_I2C_SDA);
    scl(false);
    return ack;
}
static uint8_t i2c_rd(bool ack)
{
    uint8_t b = 0;
    sda(true);
    for (uint32_t i = 0; i < 8u; i++) { scl(true); b = (uint8_t)((b << 1) | (GET(PIN_I2C_SDA) ? 1u : 0u)); scl(false); }
    sda(!ack); scl(true); scl(false); sda(true);
    return b;
}
static bool fram_addr(uint32_t addr)
{
    i2c_start();
    return i2c_wr((uint8_t)(FRAM_I2C_ADDR << 1)) && i2c_wr((uint8_t)(addr >> 8)) && i2c_wr((uint8_t)addr);
}

bool bsp_nv_write(uint32_t addr, const void *buf, uint32_t len)
{
    if (addr + len > NV_SIZE) return false;
    const uint8_t *p = (const uint8_t *)buf;
    bool ok = fram_addr(addr);
    for (uint32_t i = 0; ok && i < len; i++) ok = i2c_wr(p[i]);
    i2c_stop();
    return ok;
}

bool bsp_nv_read(uint32_t addr, void *buf, uint32_t len)
{
    if (addr + len > NV_SIZE || len == 0u) return false;
    uint8_t *p = (uint8_t *)buf;
    bool ok = fram_addr(addr);
    if (ok) { i2c_start(); ok = i2c_wr((uint8_t)((FRAM_I2C_ADDR << 1) | 1u)); }
    for (uint32_t i = 0; ok && i < len; i++) p[i] = i2c_rd(i + 1u < len);
    i2c_stop();
    return ok;
}

/* ------------------------------------------------------------------ DS18B20 on J4 pin 9 (PA8), 1-Wire, external 4.7k pull-up
 * Bit timing per the Maxim/Analog Devices DS18B20 data sheet (standard speed). Each time slot runs with
 * interrupts off (<= 70 us); a full read of the scratchpad takes about 7 ms of main-loop time. */
static void ow_low(void)     { SET(PIN_TEMP_1W, false); }
static void ow_release(void) { SET(PIN_TEMP_1W, true); }        /* open drain: pull-up takes the line high */

static bool ow_reset(void)
{
    ow_low(); delay_us(480);
    __disable_irq();
    ow_release(); delay_us(70);
    bool present = !GET(PIN_TEMP_1W);
    __enable_irq();
    delay_us(410);
    return present;
}

static void ow_write_bit(bool b)
{
    __disable_irq();
    ow_low(); delay_us(b ? 6u : 60u);
    ow_release(); delay_us(b ? 64u : 10u);
    __enable_irq();
}

static bool ow_read_bit(void)
{
    __disable_irq();
    ow_low(); delay_us(3);
    ow_release(); delay_us(10);
    bool b = GET(PIN_TEMP_1W);
    __enable_irq();
    delay_us(53);
    return b;
}

static void ow_write(uint8_t v) { for (uint32_t i = 0; i < 8u; i++) ow_write_bit(((v >> i) & 1u) != 0u); }
static uint8_t ow_read(void) { uint8_t v = 0; for (uint32_t i = 0; i < 8u; i++) if (ow_read_bit()) v |= (uint8_t)(1u << i); return v; }

static uint8_t crc8_maxim(const uint8_t *p, uint32_t n)
{
    uint8_t c = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = p[i];
        for (uint32_t k = 0; k < 8u; k++) { uint8_t mix = (uint8_t)((c ^ b) & 1u); c >>= 1; if (mix) c ^= 0x8Cu; b >>= 1; }
    }
    return c;
}

static bool g_temp_started;
void bsp_temp_start(void)
{
    g_temp_started = false;
    if (!ow_reset()) return;
    ow_write(0xCC);                /* skip ROM: one probe on the bus */
    ow_write(0x44);                /* convert T                      */
    g_temp_started = true;
}

bool bsp_temp_fetch(int16_t *c_x10)
{
    if (!g_temp_started || !ow_reset()) return false;
    uint8_t sp[9];
    ow_write(0xCC);
    ow_write(0xBE);                /* read scratchpad */
    for (uint32_t i = 0; i < 9u; i++) sp[i] = ow_read();
    if (crc8_maxim(sp, 8) != sp[8]) return false;
    bool all_ff = true;
    for (uint32_t i = 0; i < 9u; i++) if (sp[i] != 0xFFu) all_ff = false;
    if (all_ff) return false;
    int16_t raw = (int16_t)((uint16_t)sp[0] | ((uint16_t)sp[1] << 8));  /* 1/16 degC */
    *c_x10 = (int16_t)((raw * 10) / 16);
    return true;
}

/* ------------------------------------------------------------------ firmware image CRC-32 (zlib polynomial), v1.2 */
extern const uint32_t _fw_crc;
bool bsp_image_check(uint32_t *crc_out)
{
    const uint8_t *p = (const uint8_t *)FLASH_BASE;
    const uint8_t *end = (const uint8_t *)&_fw_crc;
    uint32_t c = 0xFFFFFFFFu;
    while (p < end) {
        c ^= *p++;
        for (uint32_t k = 0; k < 8u; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    c = ~c;
    *crc_out = c;
    return c == _fw_crc;
}

/* ------------------------------------------------------------------ diagnostics, CPU fault and clock-failure handlers */
void bsp_diag(bsp_diag_t *d)
{
    d->hse = g_hse;
    d->hse_forced_off = RTC->BKP1R == BKP_NOHSE_MAGIC;
    d->lse = g_lse;
    bool rec = RTC->BKP4R == BKP_FAULT_MAGIC;
    d->fault_code = rec ? RTC->BKP2R : 0u;
    d->fault_pc = rec ? RTC->BKP3R : 0u;
}

void bsp_diag_clear(void)
{
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP1R = 0u; RTC->BKP2R = 0u; RTC->BKP3R = 0u; RTC->BKP4R = 0u;
}

static void fault_outputs_off(void)
{
    pin_set(PIN_VALVE_EN_REQ, false);                          /* valve rail off at once */
    TIM4->CCR1 = 0u;                                           /* dosing pump off        */
}

/* Any CPU fault: outputs off immediately, record where it happened, restart. The restart is
 * logged as a power-loss abort by the application (run-state record in FRAM). */
__attribute__((used)) void fault_common(const uint32_t *frame, uint32_t code)
{
    fault_outputs_off();
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP2R = code;
    RTC->BKP3R = frame ? frame[6] : 0u;                        /* stacked PC */
    RTC->BKP4R = BKP_FAULT_MAGIC;
    NVIC_SystemReset();
}

#define FAULT_ENTRY(name, code) \
    __attribute__((naked)) void name(void) { \
        __asm volatile ("tst lr, #4 \n ite eq \n mrseq r0, msp \n mrsne r0, psp \n movs r1, #" #code " \n b fault_common \n"); }
FAULT_ENTRY(HardFault_Handler, 1)
FAULT_ENTRY(MemManage_Handler, 2)
FAULT_ENTRY(BusFault_Handler, 3)
FAULT_ENTRY(UsageFault_Handler, 4)

/* Clock security system: the crystal stopped. Hardware has already switched to HSI. Outputs off,
 * stop using the crystal (until `diag clear`), restart on HSI. */
void NMI_Handler(void)
{
    if (RCC->CIR & RCC_CIR_CSSF) {
        RCC->CIR |= RCC_CIR_CSSC;
        fault_outputs_off();
        PWR->CR |= PWR_CR_DBP;
        RTC->BKP1R = BKP_NOHSE_MAGIC;
        RTC->BKP2R = 5u; RTC->BKP3R = 0u; RTC->BKP4R = BKP_FAULT_MAGIC;
        NVIC_SystemReset();
    }
}

/* ------------------------------------------------------------------ init */
void bsp_init(void)
{
    uint32_t csr = RCC->CSR;
    if (csr & RCC_CSR_PORRSTF) g_reset_flags |= RST_POWER_ON;
    if (csr & RCC_CSR_IWDGRSTF) g_reset_flags |= RST_IWDG;
    if ((csr & RCC_CSR_PINRSTF) && !(csr & RCC_CSR_PORRSTF) && !(csr & RCC_CSR_SFTRSTF) && !(csr & RCC_CSR_IWDGRSTF))
        g_reset_flags |= RST_PIN;
    if (csr & RCC_CSR_SFTRSTF) g_reset_flags |= RST_SOFT;
    RCC->CSR |= RCC_CSR_RMVF;

    clock_init();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    SysTick_Config(SystemCoreClock / 1000u);

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM4EN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN | RCC_APB2ENR_USART1EN;

    /* safe levels before any output is enabled */
    SET(PIN_VALVE_EN_REQ, false); OUT(PIN_VALVE_EN_REQ);
    SET(PIN_BUZZER, false); OUT(PIN_BUZZER);
    OUT(PIN_WDG_KICK); OUT(PIN_LED_GREEN); OUT(PIN_LED_RED); OUT(PIN_LCD_BL);
    OUT(PIN_LCD_RS); OUT(PIN_LCD_E); OUT(PIN_LCD_D4); OUT(PIN_LCD_D5); OUT(PIN_LCD_D6); OUT(PIN_LCD_D7);
    SET(PIN_VLV_LATCH, false); SET(PIN_VLV_SCK, false); SET(PIN_VLV_MOSI, false);
    OUT(PIN_VLV_LATCH); OUT(PIN_VLV_SCK); OUT(PIN_VLV_MOSI);
    IN(PIN_VLV_MISO, PULL_NONE); IN(PIN_VLV_NFAULT, PULL_NONE);
    SET(PIN_HX_SCK, false); OUT(PIN_HX_SCK); IN(PIN_HX_DOUT, PULL_NONE);
    SET(PIN_TEMP_1W, true); OUT_OD(PIN_TEMP_1W);               /* 1-Wire bus idle high (external 4.7k) */
    IN(PIN_LIQ_DET, PULL_NONE); IN(PIN_DLZ_PRESENT, PULL_NONE); IN(PIN_CONC_LOW, PULL_NONE);
    IN(PIN_KEY_START, PULL_UP); IN(PIN_KEY_STOP, PULL_UP); IN(PIN_KEY_MUTE, PULL_UP);
    IN(PIN_KEY_UP, PULL_UP); IN(PIN_KEY_DOWN, PULL_UP); IN(PIN_KEY_ENTER, PULL_UP);
    AN(PIN_P_BUS); AN(PIN_P_SUPPLY); AN(PIN_V24_MON); AN(PIN_PH_IN); AN(PIN_V5_MON); AN(PIN_VRAIL_MON); AN(PIN_PUMP_ISENSE);
    SET(PIN_I2C_SCL, true); SET(PIN_I2C_SDA, true); OUT_OD(PIN_I2C_SCL); OUT_OD(PIN_I2C_SDA);
    for (uint32_t i = 0; i < 9u; i++) { scl(false); scl(true); }  /* free a stuck FRAM */
    i2c_stop();
    bsp_valve_write(0);

    /* flow counter: TIM2 external clock mode 2 on ETR (PA0, AF1), digital filter */
    AF(PIN_FLOW_PULSE, 1u);
    TIM2->SMCR = TIM_SMCR_ECE | (0xFu << TIM_SMCR_ETF_Pos);
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->CR1 = TIM_CR1_CEN;

    /* pump PWM: TIM4_CH1 on PD12 (AF2), 84 MHz / 4200 = 20 kHz */
    AF(PIN_PUMP_PWM, 2u);
    TIM4->PSC = 0; TIM4->ARR = PUMP_ARR; TIM4->CCR1 = 0;
    TIM4->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM4->CCER = TIM_CCER_CC1E;
    TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    /* ADC1: PCLK2/4 = 21 MHz, 144-cycle sampling on all used channels */
    ADC->CCR = ADC_CCR_ADCPRE_0;
    ADC1->SMPR1 = (6u << (3u * (10u - 10u))) | (6u << (3u * (11u - 10u))) | (6u << (3u * (12u - 10u))) |
                  (6u << (3u * (13u - 10u))) | (6u << (3u * (14u - 10u)));
    ADC1->SMPR2 = (6u << (3u * 1u)) | (6u << (3u * 8u));
    ADC1->SQR1 = 0;
    ADC1->CR2 = ADC_CR2_ADON;

    /* USART1 115200 8N1 (APB2 84 MHz) */
    AF(PIN_UART_TX, 7u); AF(PIN_UART_RX, 7u);
    USART1->BRR = 0x2D9u;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART1_IRQn, 5);
    NVIC_EnableIRQ(USART1_IRQn);

    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
    SET(PIN_WDG_KICK, true);                                   /* first WDI edge for the TPS3823 */
    rtc_init();
    SET(PIN_WDG_KICK, false);
    lcd_init();
    SET(PIN_LCD_BL, true);

    /* independent watchdog ~1.5 s, frozen while halted by the debugger */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    IWDG->KR = 0x5555u; IWDG->PR = 3u; IWDG->RLR = 1500u; IWDG->KR = 0xCCCCu;
}
