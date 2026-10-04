#include "platform/pwm_port.h"
#include <stddef.h>

/* --- Hardware Timer 1 Registers Map Addresses (STM32F103) --- */
#define REG_RCC_BASE         0x40021000UL
#define REG_RCC_APB2ENR      (*(volatile uint32_t *)(REG_RCC_BASE + 0x18UL))

#define REG_GPIOA_BASE       0x40010800UL
#define REG_GPIOA_CRH        (*(volatile uint32_t *)(REG_GPIOA_BASE + 0x04UL))

#define REG_TIM1_BASE        0x40012C00UL
#define REG_TIM1_CR1         (*(volatile uint32_t *)(REG_TIM1_BASE + 0x00UL))
#define REG_TIM1_CCMR1       (*(volatile uint32_t *)(REG_TIM1_BASE + 0x18UL))
#define REG_TIM1_CCER        (*(volatile uint32_t *)(REG_TIM1_BASE + 0x20UL))
#define REG_TIM1_PSC         (*(volatile uint32_t *)(REG_TIM1_BASE + 0x28UL))
#define REG_TIM1_ARR         (*(volatile uint32_t *)(REG_TIM1_BASE + 0x2CUL))
#define REG_TIM1_CCR1        (*(volatile uint32_t *)(REG_TIM1_BASE + 0x34UL))
#define REG_TIM1_BDTR        (*(volatile uint32_t *)(REG_TIM1_BASE + 0x44UL))

bool pwm_hardware_init(void)
{
    /* 1. Turn on peripheral clock domains for GPIOA and Advanced Timer TIM1 */
    REG_RCC_APB2ENR |= (1UL << 2u) | (1UL << 11u); /* IOPAEN and TIM1EN bits */

    /* 2. Configure Pin PA8 (TIM1_CH1) as Alternate Function Push-Pull 10MHz (CNF=10, MODE=01 -> 0x9) */
    uint32_t gpio_crh = REG_GPIOA_CRH;
    gpio_crh &= ~0x0000000FUL; /* Wipe configuration register bits for pin 8 */
    gpio_crh |= 0x00000009UL;  /* Set Alternate Function Push-Pull mode */
    REG_GPIOA_CRH = gpio_crh;

    /* 3. Configure Timer 1 internal timebase frequency counter */
    REG_TIM1_PSC = 0u; /* Prescaler = 0 -> Timer clock = 8 MHz HSI clock */
    
    /* Set Auto-Reload register to 10000 to map 0.01% duty cycle steps directly to hardware */
    REG_TIM1_ARR = 10000u; 
    REG_TIM1_CCR1 = 0u;    /* Initialize with safe hardware cutoff state (0.00% duty) */

    /* 4. Configure Channel 1 Output Compare Mode options */
    /* CCMR1: OC1M = 110 (PWM Mode 1: active until counter matches CCR1), OC1PE = 1 (Preload enable) */
    REG_TIM1_CCMR1 &= ~(7UL << 4u);
    REG_TIM1_CCMR1 |= (6UL << 4u) | (1UL << 3u);

    /* 5. Activate Channel 1 and configure main output execution blocks */
    REG_TIM1_CCER |= (1UL << 0u);  /* CC1E: Enable hardware channel 1 signal routing */
    
    /* BDTR: MOE = 1 (Main Output Enable - critical security flag mandatory for Advanced Timers) */
    REG_TIM1_BDTR |= (1UL << 15u); 

    /* CR1: ARPE = 1 (Auto-reload preload enable), CEN = 1 (Fire up the timer counter) */
    REG_TIM1_CR1 |= (1UL << 7u) | (1UL << 0u);

    return true;
}

void pwm_hardware_set_duty(pwm_duty_t duty)
{
    /* Enforce rigid hardware boundary protection constraints */
    pwm_duty_t bounded_duty = duty;
    if (bounded_duty > 10000u) {
        bounded_duty = 10000u;
    }

    /* Stream duty cycle directly into the active hardware Capture/Compare register */
    REG_TIM1_CCR1 = (uint32_t)bounded_duty;
}

pwm_duty_t pwm_hardware_get_duty(void)
{
    return (pwm_duty_t)(REG_TIM1_CCR1 & 0xFFFFu);
}

void pwm_hardware_emergency_disable(void)
{
    /* Instantaneous silicon-level override: drop Main Output Enable flag */
    REG_TIM1_BDTR &= ~(1UL << 15u); /* Clear MOE bit: forces Hi-Z state on all power pins */
    REG_TIM1_CCR1 = 0u;             /* Hard reset duty register value to zero */
}
