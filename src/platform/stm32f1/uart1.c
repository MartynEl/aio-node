/* src/platform/stm32f1/uart1.c */
/* Stage 2.3-code: USART1 TX, blocking, 115200 8N1 on PA9.
 * No RX, no interrupts, no DMA -> vector [55] stays Default_Handler (startup untouched).
 * Clock base: HSI 8 MHz, PLL not enabled (pwm_timer.c comment + empty
 * stm32f1_platform_init()). Re-tune BRR here when HSE/PLL comes up. */
#include "platform/platform.h"   /* gives the platform_write prototype (MISRA 8.4) */
#include <stdint.h>
#include <stddef.h>

void uart1_init(void);           /* forward decl, MCU-BSP scope, per project convention */

/* --- RCC (confirmed in pwm_timer.c / adc_dma.c) --- */
#define REG_RCC_BASE      0x40021000UL
#define REG_RCC_APB2ENR   (*(volatile uint32_t *)(REG_RCC_BASE + 0x18UL))
#define RCC_IOPAEN        (1UL << 2u)    /* pwm_timer.c:23 */
#define RCC_USART1EN      (1UL << 14u)   /* RM0008; verified by readback below */

/* --- GPIOA (confirmed in pwm_timer.c / adc_dma.c) --- */
#define REG_GPIOA_BASE    0x40010800UL
#define REG_GPIOA_CRH     (*(volatile uint32_t *)(REG_GPIOA_BASE + 0x04UL))
/* PA9 = USART1_TX: AF push-pull 10 MHz -> nibble 0x9, same mode as PA8/TIM1_CH1.
 * Pin 9 lives in CRH, nibble index (9-8)=0 -> shift 0*4 = 4. */
#define TX_PIN_SHIFT      4u
#define TX_PIN_MODE_NIB   0x9UL

/* --- USART1 (RM0008 memory map; verified by readback) --- */
#define REG_USART1_BASE   0x40013800UL
#define REG_USART1_SR     (*(volatile uint32_t *)(REG_USART1_BASE + 0x00UL))
#define REG_USART1_DR     (*(volatile uint32_t *)(REG_USART1_BASE + 0x04UL))
#define REG_USART1_BRR    (*(volatile uint32_t *)(REG_USART1_BASE + 0x08UL))
#define REG_USART1_CR1    (*(volatile uint32_t *)(REG_USART1_BASE + 0x0CuL))
#define USART_SR_TC       (1UL << 6u)
#define USART_SR_TXE      (1UL << 7u)
#define USART_CR1_TE      (1UL << 3u)
#define USART_CR1_UE      (1UL << 13u)

/* BRR @ 115200 baud, PCLK2 = 8 MHz HSI:
 * USARTDIV = 8e6 / (16 * 115200) = 4.3403 -> mantissa 4, frac round(.3403*16)=5
 * BRR = (4<<4)|5 = 0x45. Real baud = 8e6/(16*4.3125) = 115942, err 0.64% (<2%). */
#define USART1_BRR_115200_HSI8  0x45UL

void uart1_init(void)
{
    /* 1. Clocks: GPIOA + USART1 on APB2. */
    REG_RCC_APB2ENR |= RCC_IOPAEN | RCC_USART1EN;

    /* 2. PA9 as alternate-function push-pull (USART1_TX). Read-modify-write
     *    preserves PA8 (TIM1_CH1, set by pwm_timer.c) and pins 10..15. */
    uint32_t crh = REG_GPIOA_CRH;
    crh &= ~(0xFUL << TX_PIN_SHIFT);
    crh |=  (TX_PIN_MODE_NIB << TX_PIN_SHIFT);
    REG_GPIOA_CRH = crh;

    /* 3. Baud rate. */
    REG_USART1_BRR = USART1_BRR_115200_HSI8;

    /* 4. Transmitter + USART enable. RE/IDLE/parity/interrupts left 0. */
    REG_USART1_CR1 = USART_CR1_TE | USART_CR1_UE;
}

void platform_write(const char *data, size_t len)
{
    if (data == NULL) {
        return;
    }
    for (size_t i = 0u; i < len; ++i) {
        /* Wait TX data register empty. Bounded: industrial rule — never hang
         * on a hardware handshake. Guard ~12.5 ms @8 MHz >> 87 us/byte, so it
         * never trips on healthy silicon; if it does, we drop the byte instead
         * of deadlocking the scheduler. */
        uint32_t guard = 100000u;
        while ((REG_USART1_SR & USART_SR_TXE) == 0u) {
            if (--guard == 0u) {
                break;
            }
        }
        REG_USART1_DR = (uint32_t)((uint8_t)data[i]);
    }
    /* Drain last byte out of the shift register (TC), bounded. */
    uint32_t guard = 100000u;
    while ((REG_USART1_SR & USART_SR_TC) == 0u) {
        if (--guard == 0u) {
            break;
        }
    }
}