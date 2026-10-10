/* src/platform/stm32f1/hal_stubs.c */

#include "platform/platform.h"
#include <stdint.h>
#include <stddef.h>

/* --- Time & Delay Stubs --- */
static volatile uint32_t s_millis_counter = 0u;

void platform_delay_ms(uint32_t ms) {
    /* TODO: Implement real delay using SysTick or DWT cycle counter */
    for (uint32_t i = 0; i < ms * 1000u; ++i) {} 
}

uint32_t platform_millis(void) {
    /* In a real system, this would read a timer updated by SysTick IRQ */
    return s_millis_counter; 
}

void platform_poll(void) {
    /* Placeholder for background tasks if any */
}

void platform_wfi(void) {
    /* Wait For Interrupt instruction */
    __asm volatile ("wfi");
}

/* --- Init Stub --- */
/* Note: stm32f1_platform_init is likely already defined in platform_stm32f1.c 
   If it's missing there, define it here. Assuming it exists elsewhere for now.
   If linker complains about it specifically being undefined despite file inclusion,
   check if the function name matches exactly. */

/* --- Logger / IO Stubs --- */
/**
 * @brief Stream raw character byte block directly to the active hardware or host terminal.
 * 
 * Implementation Note: Currently a stub for MCU build.
 * TODO: Connect this to UART DMA transmit buffer when uart_driver is integrated.
 */
void platform_write(const char *data, size_t len)
{
    /* Suppress unused parameter warnings for now */
    (void)data;
    (void)len;
    
    /* In a real implementation, this would enqueue bytes into the UART TX ring buffer */
}

/* --- Flash Wear Leveling Stubs --- */
bool platform_flash_read_row(uint32_t offset, uint32_t *out_buf, size_t words_len) {
    /* TODO: Real implementation reading from internal Flash registers */
    (void)offset; (void)words_len;
    // Simulate empty flash (all 0xFF)
    for(size_t i=0; i<words_len; ++i) out_buf[i] = 0xFFFFFFFFUL;
    return true;
}

bool platform_flash_write_row(uint32_t offset, const uint32_t *src_buf, size_t words_len) {
    /* TODO: Real implementation writing to internal Flash */
    (void)offset; (void)src_buf; (void)words_len;
    return true; /* Pretend success */
}

bool platform_flash_sector_erase(void) {
    /* TODO: Real implementation erasing flash sector */
    return true; /* Pretend success */
}

/**
 * @brief Initialize STM32F1 hardware peripherals (Clocks, GPIO, NVIC groups).
 * 
 * TODO: Replace this stub with real register manipulation in platform_stm32f1.c
 * once the specific clock tree configuration is finalized for the board.
 */
void stm32f1_platform_init(void)
{
    /* Placeholder implementation.
     * In a production environment, this would configure:
     * 1. RCC registers to set up HSI/PLL and bus prescalers.
     * 2. Flash latency settings corresponding to the system clock frequency.
     * 3. SysTick timer initialization for os-tick generation.
     * 4. NVIC priority grouping if required by FreeRTOS or custom scheduler.
     */
}