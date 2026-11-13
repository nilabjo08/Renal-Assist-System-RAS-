/* Minimal startup for STM32F407xx: vector table, .data/.bss init, call main. */
#include <stdint.h>

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern int main(void);
extern void SystemInit(void);

void Reset_Handler(void);
void Default_Handler(void);
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void)  __attribute__((weak, alias("Default_Handler")));

#define IRQ_COUNT 82u
typedef void (*vec_t)(void);

__attribute__((section(".isr_vector"), used))
const vec_t vector_table[16u + IRQ_COUNT] = {
    [0]  = (vec_t)(uintptr_t)&_estack,
    [1]  = Reset_Handler,
    [2]  = NMI_Handler,
    [3]  = HardFault_Handler,
    [4]  = MemManage_Handler,
    [5]  = BusFault_Handler,
    [6]  = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 + 37] = USART1_IRQHandler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0u;
    SystemInit();
    (void)main();
    for (;;) { }
}

/* Any unexpected fault: stop here. Valve rail drops because the watchdogs are no longer kicked
 * (TPS3823 removes VALVE_ENABLE within 1.6 s, IWDG resets the MCU). */
void Default_Handler(void) { for (;;) { } }
