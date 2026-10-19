/* src/platform/stm32f1/sys_tick.c */
/* Stage 2.2: real 1 ms time-base from the Cortex-M3 SysTick timer.
 * Replaces the dead stub counter that lived in hal_stubs.c.
 * Clock assumption: HSI 8 MHz, PLL not yet enabled (see pwm_timer.c comment
 * "Timer clock = 8 MHz HSI clock" and the empty stm32f1_platform_init()).
 * LOAD = 8000 - 1 => exactly 1 ms. Re-tune LOAD here when PLL/HSE comes up. */
#include "platform/platform.h"

/* MISRA C:2012 Rule 8.4: declare external-linkage functions before defining. */
void platform_systick_init(void);
void SysTick_Handler(void);

/* Cortex-M3 SysTick registers (fixed ARM addresses, not STM32-specific). */
#define REG_SYSTICK_CTRL  (*(volatile uint32_t *)0xE000E010UL)
#define REG_SYSTICK_LOAD  (*(volatile uint32_t *)0xE000E014UL)
#define REG_SYSTICK_VAL   (*(volatile uint32_t *)0xE000E018UL)

#define SYSTICK_CTRL_ENABLE     (1UL << 0u)
#define SYSTICK_CTRL_TICKINT    (1UL << 1u)
#define SYSTICK_CTRL_CLKSOURCE  (1UL << 2u)   /* 1 = processor clock (8 MHz HSI) */
#define SYSTICK_RELOAD_1MS_HSI8 7999u

static volatile uint32_t s_millis_counter = 0u;

void platform_systick_init(void)
{
    REG_SYSTICK_CTRL = 0u;                            /* disable while loading */
    REG_SYSTICK_LOAD = SYSTICK_RELOAD_1MS_HSI8;
    REG_SYSTICK_VAL  = 0u;                            /* clear value + COUNTFLAG */
    REG_SYSTICK_CTRL = SYSTICK_CTRL_ENABLE
                     | SYSTICK_CTRL_TICKINT
                     | SYSTICK_CTRL_CLKSOURCE;
    /* NOTE: SysTick is a core exception (vector 15), NOT an external IRQ.
     * It does NOT require NVIC ISER enabling — unlike DMA1_Channel1 in adc_dma.c. */
}

void SysTick_Handler(void)
{
    s_millis_counter++;
}

uint32_t platform_millis(void)
{
    return s_millis_counter;
}

void platform_delay_ms(uint32_t ms)
{
    uint32_t start = s_millis_counter;
    while ((s_millis_counter - start) < ms) {
        /* busy-wait on the real tick counter */
    }
}