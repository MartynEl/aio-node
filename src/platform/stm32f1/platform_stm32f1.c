#include "platform/platform.h"
#include "platform/adc_port.h"
#include "platform/pwm_port.h"
// #include "core/controller.h"
/* TODO: Future integration of high-level controller state machine hooks if needed */
#include "app/app.h"
#include <stddef.h>

/* --- Hardware Register Layout Addresses (STM32F103) --- */
#define REG_RCC_BASE         0x40021000UL
#define REG_RCC_APB1ENR      (*(volatile uint32_t *)(REG_RCC_BASE + 0x1CUL))
#define REG_RCC_APB2ENR      (*(volatile uint32_t *)(REG_RCC_BASE + 0x18UL))
#define REG_RCC_CSR          (*(volatile uint32_t *)(REG_RCC_BASE + 0x24UL))

#define REG_PWR_BASE         0x40007000UL
#define REG_PWR_CR           (*(volatile uint32_t *)(REG_PWR_BASE + 0x00UL))
#define PWR_CR_DBP_BIT       (1UL << 8u)   /* Disable Backup Domain write protection */
#define PWR_CR_PVDE_BIT      (1UL << 4u)   /* Programmable Voltage Detector enable */
#define PWR_CR_PLS_2V9       (5UL << 5u)   /* PVD Level Selection = 2.9V threshold */

#define REG_BKP_BASE         0x40006C00UL
#define REG_BKP_DR1          (*(volatile uint32_t *)(REG_BKP_BASE + 0x04UL)) /* Backup Data Register 1 */

#define REG_EXTI_BASE        0x40010400UL
#define REG_EXTI_IMR         (*(volatile uint32_t *)(REG_EXTI_BASE + 0x00UL))
#define REG_EXTI_RTSR        (*(volatile uint32_t *)(REG_EXTI_BASE + 0x08UL))
#define REG_EXTI_PR          (*(volatile uint32_t *)(REG_EXTI_BASE + 0x14UL))
#define EXTI_LINE_16_PVD     (1UL << 16u)  /* PVD internal trigger line */

#define REG_NVIC_BASE        0xE000E100UL
#define REG_NVIC_ISER0       (*(volatile uint32_t *)(REG_NVIC_BASE + 0x00UL))
#define NVIC_IRQ_1_PVD       (1UL << 1u)   /* PVD interrupt position vector mask */

#define REG_IWDG_BASE        0x40003000UL
#define REG_IWDG_KR          (*(volatile uint32_t *)(REG_IWDG_BASE + 0x00UL))
#define REG_IWDG_PR          (*(volatile uint32_t *)(REG_IWDG_BASE + 0x04UL))
#define REG_IWDG_RLR         (*(volatile uint32_t *)(REG_IWDG_BASE + 0x08UL))
#define REG_IWDG_SR          (*(volatile uint32_t *)(REG_IWDG_BASE + 0x0CUL))

#define IWDG_KEY_UNLOCK      0x5555UL
#define IWDG_KEY_FEED        0xAAAAUL
#define IWDG_KEY_START       0xCCCCUL

/* Reference the global structured application context root */
extern app_t g_io_node;

reset_cause_t platform_reset_cause_get(void)
{
    uint32_t csr_val = REG_RCC_CSR;

    /* MISRA compliance: clear flags immediately using structural bit mask write */
    REG_RCC_CSR |= (1UL << 24u); /* RMVF: Remove Reset Flags bit */

    reset_cause_t cause;
    if ((csr_val & (1UL << 29u)) != 0u) {
        cause = RESET_CAUSE_WATCHDOG;
    } else if ((csr_val & (1UL << 28u)) != 0u) {
        cause = RESET_CAUSE_SOFTWARE;
    } else if ((csr_val & (1UL << 26u)) != 0u) {
        cause = RESET_CAUSE_EXTERNAL;
    } else if ((csr_val & (1UL << 27u)) != 0u) {
        cause = RESET_CAUSE_POWER_ON;
    } else {
        cause = RESET_CAUSE_UNKNOWN;
    }

    return cause;
}

void platform_pvd_init(void)
{
    /* Enable power interfaces clocking domains inside APB1 bus */
    REG_RCC_APB1ENR |= (1UL << 28u) | (1UL << 27u); /* PWREN and BKPEN bits */

    /* Configure comparator threshold level to 2.9V and activate the PVD hardware block */
    REG_PWR_CR = PWR_CR_PLS_2V9 | PWR_CR_PVDE_BIT;

    /* Route PVD analog output events to internal interrupt controller line 16 */
    REG_EXTI_IMR |= EXTI_LINE_16_PVD;
    REG_EXTI_RTSR |= EXTI_LINE_16_PVD; /* Trigger interrupt on falling voltage rail edge */

    /* Commit PVD vector slot execution permission inside Cortex-M NVIC ISER0 table */
    REG_NVIC_ISER0 |= NVIC_IRQ_1_PVD;
}

void platform_emergency_backup_save(const void *data, size_t len)
{
    if ((data != NULL) && (len > 0u)) {
        /* Remove hardware domain protection locks */
        REG_PWR_CR |= PWR_CR_DBP_BIT;

        const uint8_t *byte_ptr = (const uint8_t *)data;
        REG_BKP_DR1 = (uint32_t)(*byte_ptr);
    }
}

bool platform_emergency_backup_load(void *out_data, size_t len)
{
    if ((out_data == NULL) || (len == 0u)) {
        return false;
    }

    uint8_t *byte_ptr = (uint8_t *)out_data;
    *byte_ptr = (uint8_t)(REG_BKP_DR1 & 0xFFu);
    return true;
}

void platform_wdg_init(uint32_t timeout_ms)
{
    REG_IWDG_KR = IWDG_KEY_UNLOCK;
    REG_IWDG_PR = 4u; /* Prescaler division factor = 64 (1 tick ≈ 1.6 ms @ LSI 40 kHz) */

    /* Convert time units constraint into raw reload tick values.
     * Done BEFORE START: PR/RLR are write-protected once IWDG is enabled. */
    uint32_t reload_ticks = (timeout_ms * 5u) / 8u;
    if (reload_ticks > 4095u) {
        reload_ticks = 4095u;
    }
    if (reload_ticks < 1u) {
        reload_ticks = 1u;
    }
    REG_IWDG_RLR = reload_ticks;

    /* START BEFORE the waits: the LSI oscillator is turned on by hardware only
     * when 0xCCCC is written (RM0008). PVU/RVU clear on LSI clock edges, so
     * waiting for them before START hangs forever while LSI is off. The PR/RLR
     * values written above are latched into the counter when LSI starts. */
    REG_IWDG_KR = IWDG_KEY_START;

    /* Bounded waits — industrial hygiene: never hang on a hardware handshake.
     * Guard ≈ 5 ms at 8 MHz HSI; PVU/RVU normally clear within 1-2 LSI edges
     * (~50 µs each), so the guard never trips in healthy silicon. If it does
     * trip, we proceed rather than deadlock: watchdog runs with possibly stale
     * PR/RLR, which is a degraded-but-recoverable state, not a frozen board. */
    uint32_t guard = 10000u;
    while ((REG_IWDG_SR & (1UL << 0u)) != 0u) { /* PVU */
        if (--guard == 0u) { break; }
    }

    guard = 10000u;
    while ((REG_IWDG_SR & (1UL << 1u)) != 0u) { /* RVU */
        if (--guard == 0u) { break; }
    }
}

void platform_wdg_feed(void)
{
    REG_IWDG_KR = IWDG_KEY_FEED;
}

/**
 * @brief High-priority Hardware Interrupt Vector Routine (PVD_IRQHandler).
 * Dispatched instantly when supply voltage breaches the 2.9V boundary line.
 */
void PVD_IRQHandler(void) __attribute__((interrupt));
void PVD_IRQHandler(void)
{
    /* Microsecond safe isolation: immediately shutdown power load lines at silicon level */
    pwm_hardware_emergency_disable();
    g_io_node.registers.device_state = (uint16_t)DEVICE_STATE_SAFE_MODE;

    /* Clear pending hardware interrupt bit flag */
    REG_EXTI_PR = EXTI_LINE_16_PVD;
}
