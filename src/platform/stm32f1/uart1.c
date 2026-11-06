/* src/platform/stm32f1/uart1.c */
/*
 * Stage 3.0: USART1 Modbus RTU slave driver for STM32F103.
 *
 * Hardware assumptions:
 *   PA9  -> USART1_TX  -> MAX3485 DI / CH340G RXD
 *   PA10 <- USART1_RX  <- MAX3485 RO / CH340G TXD
 *   PA11 -> MAX3485 DE and /RE tied together
 *
 * Clock assumption:
 *   HSI 8 MHz, PCLK2 = 8 MHz.
 *
 * Baud:
 *   9600 8N1 for industrial bring-up.
 *   BRR = 0x341 at PCLK2 = 8 MHz.
 *
 * Important STM32F103 fact:
 *   USART1 global interrupt is IRQ 37.
 *   Therefore NVIC ISER1/ISPR1 bit is 5, not 7.
 */

#include "platform/platform.h"
#include "platform/uart1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void USART1_IRQHandler(void);

/* --- RCC --- */
#define REG_RCC_BASE          0x40021000UL
#define REG_RCC_CFGR          (*(volatile uint32_t *)(REG_RCC_BASE + 0x04UL))
#define REG_RCC_APB2ENR       (*(volatile uint32_t *)(REG_RCC_BASE + 0x18UL))
#define REG_RCC_APB2RSTR      (*(volatile uint32_t *)(REG_RCC_BASE + 0x0CUL))

#define RCC_AFIOEN            (1UL << 0u)
#define RCC_IOPAEN            (1UL << 2u)
#define RCC_USART1EN          (1UL << 14u)

#define RCC_USART1_RST        (1UL << 14u)

/* --- GPIOA --- */
#define REG_GPIOA_BASE        0x40010800UL
#define REG_GPIOA_CRH         (*(volatile uint32_t *)(REG_GPIOA_BASE + 0x04UL))
#define REG_GPIOA_BSRR        (*(volatile uint32_t *)(REG_GPIOA_BASE + 0x10UL))

/*
 * GPIO CRH nibble encoding for STM32F1:
 *   bits [3:2] = CNF
 *   bits [1:0] = MODE
 *
 * PA9  = USART1_TX:
 *   Alternate function push-pull, 10 MHz
 *   CNF = 10, MODE = 01 -> 0x9
 *
 * PA10 = USART1_RX:
 *   Input with pull-up / pull-down
 *   CNF = 10, MODE = 00 -> 0x8
 *
 * PA11 = MAX3485 DE / /RE direction control:
 *   General purpose output push-pull, 10 MHz
 *   CNF = 00, MODE = 01 -> 0x1
 */
#define TX_PIN_SHIFT          4u
#define RX_PIN_SHIFT          8u
#define DIR_PIN_SHIFT         12u

#define PIN_AF_PP_10MHZ       0x9UL
#define PIN_INPUT_PULL_UP     0x8UL
#define PIN_GP_PP_10MHZ       0x1UL

#define RX_PIN_SET_BIT        (1UL << 10u)
#define DIR_PIN_SET_BIT       (1UL << 11u)
#define DIR_PIN_RESET_BIT     (1UL << 27u)

/* --- USART1 --- */
#define REG_USART1_BASE       0x40013800UL
#define REG_USART1_SR         (*(volatile uint32_t *)(REG_USART1_BASE + 0x00UL))
#define REG_USART1_DR         (*(volatile uint32_t *)(REG_USART1_BASE + 0x04UL))
#define REG_USART1_BRR        (*(volatile uint32_t *)(REG_USART1_BASE + 0x08UL))
#define REG_USART1_CR1        (*(volatile uint32_t *)(REG_USART1_BASE + 0x0CUL))

#define USART_SR_PE           (1UL << 0u)
#define USART_SR_FE           (1UL << 1u)
#define USART_SR_NE           (1UL << 2u)
#define USART_SR_ORE          (1UL << 3u)
#define USART_SR_RXNE         (1UL << 5u)
#define USART_SR_TC           (1UL << 6u)
#define USART_SR_TXE          (1UL << 7u)

#define USART_CR1_RE          (1UL << 2u)
#define USART_CR1_TE          (1UL << 3u)
#define USART_CR1_RXNEIE      (1UL << 5u)
#define USART_CR1_UE          (1UL << 13u)

/*
 * BRR @ 9600 baud, PCLK2 = 8 MHz HSI:
 * USARTDIV = 8e6 / (16 * 9600) = 52.0833
 * mantissa = 52 = 0x34
 * fraction = round(0.0833 * 16) = 1
 * BRR = (52 << 4) | 1 = 0x341
 */
#define USART1_BRR_9600_HSI8  0x341UL

/*
 * Reserved for later transition to 115200:
 * USARTDIV = 8e6 / (16 * 115200) = 4.3403
 * BRR = (4 << 4) | 5 = 0x45
 * Real baud ~= 115942, error ~= 0.64%.
 */
/* #define USART1_BRR_115200_HSI8 0x45UL */

/* --- NVIC --- */
#define REG_NVIC_BASE         0xE000E100UL
#define REG_NVIC_ISER1        (*(volatile uint32_t *)(REG_NVIC_BASE + 0x04UL))
#define REG_NVIC_ICPR1        (*(volatile uint32_t *)(REG_NVIC_BASE + 0x184UL))

/*
 * STM32F103 USART1 is IRQ 37.
 *
 * NVIC register layout:
 *   ISER0 / ISPR0 / ICPR0 cover IRQ 0..31
 *   ISER1 / ISPR1 / ICPR1 cover IRQ 32..63
 *
 * IRQ 37 => ISER1/ISPR1/ICPR1 bit (37 - 32) = bit 5.
 */
#define NVIC_USART1_BIT       (1UL << 5u)

/* --- RX ring buffer --- */
#define UART_RX_RING_SIZE     256u
#define UART_RX_RING_MASK     (UART_RX_RING_SIZE - 1u)

static volatile uint8_t g_uart_rx_ring[UART_RX_RING_SIZE];
static volatile size_t g_uart_rx_head = 0u;
static volatile size_t g_uart_rx_tail = 0u;

static void uart1_rx_push(uint8_t byte)
{
    size_t head;
    size_t next;

    head = g_uart_rx_head;
    next = (head + 1u) & UART_RX_RING_MASK;

    /*
     * Single-producer / single-consumer ring buffer.
     * If the buffer is full, drop the newest byte.
     * For Modbus RTU this invalidates the current frame, which is safer
     * than corrupting an already partially captured frame.
     */
    if (next != g_uart_rx_tail) {
        g_uart_rx_ring[head] = byte;
        g_uart_rx_head = next;
    }
}

static bool uart1_wait_sr_flag(uint32_t flag, uint32_t timeout_ms)
{
    uint32_t start_ms;

    start_ms = platform_millis();

    while ((REG_USART1_SR & flag) == 0u) {
        if ((platform_millis() - start_ms) >= timeout_ms) {
            return false;
        }
    }

    return true;
}

void uart1_init(void)
{
    uint32_t crh;

    /* Ensure PCLK2 is not divided: SYSCLK = HSI 8 MHz => PCLK2 = 8 MHz. */
    REG_RCC_CFGR &= ~(7UL << 11u);

    /*
     * Reset only USART1.
     * Do not reset whole GPIOA here: GPIOA is shared with ADC/PWM pins
     * and resetting it from UART driver is an unnecessary side effect.
     */
    REG_RCC_APB2RSTR |= RCC_USART1_RST;
    REG_RCC_APB2RSTR &= ~RCC_USART1_RST;

    /* Enable AFIO, GPIOA and USART1 clocks on APB2. */
    REG_RCC_APB2ENR |= (RCC_AFIOEN | RCC_IOPAEN | RCC_USART1EN);

    /*
     * GPIO configuration:
     *   PA9  = alternate function push-pull, 10 MHz  -> USART1_TX
     *   PA10 = input pull-up                          -> USART1_RX
     *   PA11 = general purpose push-pull, 10 MHz      -> MAX3485 DE / /RE
     */
    crh = REG_GPIOA_CRH;
    crh &= ~((0xFUL << TX_PIN_SHIFT) |
             (0xFUL << RX_PIN_SHIFT) |
             (0xFUL << DIR_PIN_SHIFT));

    crh |= ((PIN_AF_PP_10MHZ   << TX_PIN_SHIFT)  |
            (PIN_INPUT_PULL_UP  << RX_PIN_SHIFT)  |
            (PIN_GP_PP_10MHZ    << DIR_PIN_SHIFT));

    REG_GPIOA_CRH = crh;

    /*
     * PA10 pull-up enable: set ODR bit 10.
     * PA11 low: MAX3485 in receive mode by default.
     */
    REG_GPIOA_BSRR = (RX_PIN_SET_BIT | DIR_PIN_RESET_BIT);

    /* 9600 8N1. */
    REG_USART1_BRR = USART1_BRR_9600_HSI8;

    /*
     * Receiver enable + transmitter enable + RX not empty interrupt + USART enable.
     * CR1 = 0x202C.
     */
    REG_USART1_CR1 = (USART_CR1_RE |
                      USART_CR1_TE |
                      USART_CR1_RXNEIE |
                      USART_CR1_UE);

    /*
     * Clear any pending USART status flags.
     * On STM32F1 reading SR then DR clears ORE/NE/FE/PE and RXNE.
     */
    (void)REG_USART1_SR;
    (void)REG_USART1_DR;

    /*
     * Clear stale NVIC pending bit for USART1 before enabling the interrupt.
     * USART1 = IRQ 37 => ICPR1 bit 5.
     */
    REG_NVIC_ICPR1 = NVIC_USART1_BIT;

    /* Enable USART1 interrupt in NVIC: IRQ 37 => ISER1 bit 5. */
    REG_NVIC_ISER1 |= NVIC_USART1_BIT;
}

void uart1_write_raw(const uint8_t *data, size_t len)
{
    bool tx_ok;
    size_t i;
    uint32_t tc_timeout_ms;

    if ((data == NULL) || (len == 0u)) {
        return;
    }

    tx_ok = true;

    /* MAX3485 -> transmit mode. */
    REG_GPIOA_BSRR = DIR_PIN_SET_BIT;

    for (i = 0u; i < len; ++i) {
        if (!uart1_wait_sr_flag(USART_SR_TXE, 10u)) {
            tx_ok = false;
            break;
        }

        REG_USART1_DR = (uint32_t)data[i];
    }

    if (tx_ok) {
        /*
         * Wait until the last stop bit is physically out of the shift register.
         * At 9600 8N1 one byte is ~1.04 ms.
         * 2 ms per byte gives comfortable margin for scheduler/ISR jitter.
         */
        tc_timeout_ms = 10u + ((uint32_t)len * 2u);
        (void)uart1_wait_sr_flag(USART_SR_TC, tc_timeout_ms);
    }

    /* MAX3485 -> receive mode. */
    REG_GPIOA_BSRR = DIR_PIN_RESET_BIT;
}

void platform_write(const char *data, size_t len)
{
    uart1_write_raw((const uint8_t *)data, len);
}

size_t uart1_rx_read(uint8_t *dst, size_t max_len)
{
    size_t count;
    size_t tail;
    size_t head;

    if ((dst == NULL) || (max_len == 0u)) {
        return 0u;
    }

    count = 0u;
    tail = g_uart_rx_tail;
    head = g_uart_rx_head;

    while ((count < max_len) && (tail != head)) {
        dst[count] = g_uart_rx_ring[tail];
        ++count;

        tail = (tail + 1u) & UART_RX_RING_MASK;

        /*
         * Update tail immediately to release slots for the ISR producer.
         * This is safe because only the consumer modifies tail.
         */
        g_uart_rx_tail = tail;
    }

    return count;
}

void USART1_IRQHandler(void)
{
    uint32_t sr;
    uint32_t events;
    uint32_t dr;

    sr = REG_USART1_SR;

    events = sr & (USART_SR_RXNE |
                   USART_SR_ORE |
                   USART_SR_NE  |
                   USART_SR_FE  |
                   USART_SR_PE);

    if (events != 0u) {
        /*
         * STM32F1 error clearing sequence:
         * read SR, then read DR.
         * This clears ORE/NE/FE/PE and RXNE.
         */
        dr = REG_USART1_DR;
        (void)dr;

        if ((sr & USART_SR_RXNE) != 0u) {
            uart1_rx_push((uint8_t)(dr & 0xFFu));
        }
    }
}