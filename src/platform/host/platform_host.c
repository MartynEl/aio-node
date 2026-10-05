#include "platform/platform.h"
#include "platform/adc_port.h"
#include "platform/pwm_port.h"
#include <stddef.h>
#include <string.h>

/* Simulating emulated hardware memory blocks */
static uint32_t g_host_now_ms = 0u;
static pwm_duty_t g_host_pwm_duty = 0u;
static adc_raw_t g_host_dma_buffer[ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL * 2u];
static adc_callback_fn g_adc_host_callback = NULL;

/* Emulated non-volatile Backup Registers space for tests */
static uint32_t g_host_bkp_dr1 = 0u;

/* Simulated thermodynamics state object variable */
static double g_virtual_process_variable = 1500.0; /* Starting coordinate in raw ADC units */

/* MISRA Compliance: Forward declaration of linkage wrapper */
extern int32_t char_output_stub(const char *data, size_t len);

uint32_t platform_millis(void)
{
    return g_host_now_ms;
}

void platform_wfi(void)
{
    /* Host sleep simulation: No-op */
}

void platform_wdg_init(uint32_t timeout_ms)
{
    (void)timeout_ms;
}

void platform_wdg_feed(void)
{
    /* Host Watchdog feeding: No-op */
}

reset_cause_t platform_reset_cause_get(void)
{
    /* Simulate a clean Power-On Reset on PC platform execution */
    return RESET_CAUSE_POWER_ON;
}

void platform_pvd_init(void)
{
    /* Power supervisor simulation: No-op */
}

void platform_emergency_backup_save(const void *data, size_t len)
{
    if ((data != NULL) && (len > 0u)) {
        const uint8_t *src = (const uint8_t *)data;
        g_host_bkp_dr1 = (uint32_t)(*src);
    }
}

bool platform_emergency_backup_load(void *out_data, size_t len)
{
    if ((out_data == NULL) || (len == 0u)) {
        return false;
    }
    uint8_t *dst = (uint8_t *)out_data;
    *dst = (uint8_t)(g_host_bkp_dr1 & 0xFFu);
    return true;
}

/* --------------------------------------------------------------------------- */
/* 🛠️ Hardware PWM Emulator Implementation                                    */
/* --------------------------------------------------------------------------- */
bool pwm_hardware_init(void)
{
    g_host_pwm_duty = 0u;
    return true;
}

void pwm_hardware_set_duty(pwm_duty_t duty)
{
    g_host_pwm_duty = (duty > 10000u) ? 10000u : duty;
}

pwm_duty_t pwm_hardware_get_duty(void)
{
    return g_host_pwm_duty;
}

void pwm_hardware_emergency_disable(void)
{
    g_host_pwm_duty = 0u;
}

/* --------------------------------------------------------------------------- */
/* 📡 Hardware ADC + DMA Pipeline Emulator with Thermodynamic Engine           */
/* --------------------------------------------------------------------------- */
bool adc_hardware_init(adc_callback_fn callback)
{
    g_adc_host_callback = callback;
    (void)memset(g_host_dma_buffer, 0, sizeof(g_host_dma_buffer));
    return true;
}

void adc_hardware_stop(void)
{
    g_adc_host_callback = NULL;
}

/**
 * @brief High-level orchestration routine driving the SiL simulation.
 * Must be explicitly advanced inside main_host.c clock steps.
 */
void platform_host_advance_simulation(uint32_t step_ms)
{
    g_host_now_ms += step_ms;

    /* 
     * Simple Linear Thermodynamic Model Engine:
     * - PWM Load shifts the value UP (Heating / Pressure buildup)
     * - Environmental structural losses shift the value DOWN (Cooling / Leaks)
     */
    double heat_input = ((double)g_host_pwm_duty / 10000.0) * 8.5;
    double thermal_loss = (g_virtual_process_variable - 1200.0) * 0.003;
    
    g_virtual_process_variable += heat_input - thermal_loss;

    /* Hard clamp to physical 12-bit instrumentation boundaries */
    if (g_virtual_process_variable > 4095.0) {
        g_virtual_process_variable = 4095.0;
    }
    if (g_virtual_process_variable < 0.0) {
        g_virtual_process_variable = 0.0;
    }

    /* Pseudo-random deterministic algorithm generating white noise */
    static uint32_t lcg_seed = 123456789u;
    lcg_seed = (lcg_seed * 1103515245u) + 12345u;
    int32_t white_noise = (int32_t)(lcg_seed % 15u) - 7; /* Noise band: +/- 7 raw ADC ticks */

    /* Pack data and mimic a hardware DMA chunk interrupt trigger */
    if (g_adc_host_callback != NULL) {
        size_t chunk_size = ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL;
        
        for (size_t s = 0u; s < ADC_SAMPLES_PER_CHANNEL; ++s) {
            size_t base = s * ADC_CHANNELS_COUNT;
            
            /* Channel 0: Feed simulated process tracking variable with white noise */
            g_host_dma_buffer[base + 0u] = (adc_raw_t)((int32_t)g_virtual_process_variable + white_noise);
            /* Channel 1: Static environment layout value simulation */
            g_host_dma_buffer[base + 1u] = 2100u;
            /* Channel 2: Controlled target potentiometer simulation value */
            g_host_dma_buffer[base + 2u] = 2048u;
        }

        /* Continuous circular streaming execution: toggle between Half and Full steps */
        static bool toggle_half = true;
        if (toggle_half) {
            g_adc_host_callback(&g_host_dma_buffer[0u], chunk_size, ADC_EVENT_HALF_COMPLETE);
            toggle_half = false;
        } else {
            g_adc_host_callback(&g_host_dma_buffer[chunk_size], chunk_size, ADC_EVENT_FULL_COMPLETE);
            toggle_half = true;
        }
    }
}

void platform_write(const char *data, size_t len)
{
    /* Direct write pipeline to Linux standard output stream */
    if ((data != NULL) && (len > 0u)) {
        (void)char_output_stub(data, len); /* Handled downstream by host link wrappers */
    }
}

void platform_poll(void)
{
    /* Host polling loop: No-op */
}
