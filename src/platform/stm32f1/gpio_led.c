/* src/platform/stm32f1/gpio_led.c */
/* Stage 2.2: onboard Blue Pill LED heartbeat on PC13 (active-low).
 * Native register access, same style as pwm_timer.c / adc_dma.c. */
#include <stdint.h>

void platform_led_init(void);
void platform_led_toggle(void);

#define REG_RCC_BASE      0x40021000UL
#define REG_RCC_APB2ENR   (*(volatile uint32_t *)(REG_RCC_BASE + 0x18UL))

#define REG_GPIOC_BASE    0x40011000UL
#define REG_GPIOC_CRH     (*(volatile uint32_t *)(REG_GPIOC_BASE + 0x04UL))
#define REG_GPIOC_ODR     (*(volatile uint32_t *)(REG_GPIOC_BASE + 0x0CUL))
#define REG_GPIOC_BSRR    (*(volatile uint32_t *)(REG_GPIOC_BASE + 0x10UL))

#define LED_PIN           13u
#define RCC_IOPCEN_BIT    4u          /* RM0008 APB2ENR bit 4 = IOPCEN */
/* PC13 -> CRH nibble (13-8)=5 -> shift 5*4=20.
 * Output push-pull 2 MHz: MODE=10, CNF=00 -> nibble 0x2 (RM0008 Table 20). */
#define LED_CRH_SHIFT     20u
#define LED_CRH_NIBBLE    0x2UL

void platform_led_init(void)
{
    REG_RCC_APB2ENR |= (1UL << RCC_IOPCEN_BIT);
    REG_GPIOC_CRH = (REG_GPIOC_CRH & ~(0xFUL << LED_CRH_SHIFT))
                  | (LED_CRH_NIBBLE << LED_CRH_SHIFT);
    REG_GPIOC_BSRR = (1UL << LED_PIN);   /* set high = LED OFF (active-low) */
}

void platform_led_toggle(void)
{
    REG_GPIOC_ODR ^= (1UL << LED_PIN);   /* single context (main loop) => safe */
}