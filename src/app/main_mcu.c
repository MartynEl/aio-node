#include "app/app.h"
#include "app/scheduler.h"
#include "platform/platform.h"
#include "platform/adc_port.h"
#include "platform/pwm_port.h"
#include "service/logger.h"
#include "service/modbus.h"
#include "service/wear_leveling.h"
#include <stddef.h>

/* Global static application context root object instance */
app_t g_io_node;

/* Global pointer referencing the main scheduler loop inside interrupts */
static scheduler_t g_mcu_scheduler;

/* --------------------------------------------------------------------------- */
/* 🛠️ Asynchronous Hardware Interrupt Callbacks (Zero-Copy)                     */
/* --------------------------------------------------------------------------- */

/**
 * @brief Asynchronous callback triggered directly by hardware ADC+DMA ISR.
 * Processes 16 oversampled rows atomically using zero-copy memory mapping.
 */
static void mcu_adc_dma_callback(const adc_raw_t *buffer_ptr, size_t samples_count, adc_event_t event)
{
    if ((buffer_ptr == NULL) || (event == ADC_EVENT_HARDWARE_ERROR)) {
        g_io_node.registers.system_fault_mask |= FAULT_BIT_ADC_DMA;
        g_io_node.registers.device_state = (uint16_t)DEVICE_STATE_SAFE_MODE;
        pwm_hardware_emergency_disable();
        return;
    }

    /* Process oversampled data block rows directly inside the DMA memory block */
    uint32_t accum_ch0 = 0u;
    uint32_t accum_ch1 = 0u;
    uint32_t accum_ch2 = 0u;
    
    size_t rows_to_parse = samples_count / ADC_CHANNELS_COUNT;

    for (size_t i = 0u; i < rows_to_parse; ++i) {
        size_t base = i * ADC_CHANNELS_COUNT;
        accum_ch0 += (uint32_t)buffer_ptr[base + 0u];
        accum_ch1 += (uint32_t)buffer_ptr[base + 1u];
        accum_ch2 += (uint32_t)buffer_ptr[base + 2u];
    }

    /* Extract mean average from oversampled hardware block and update input registers */
    g_io_node.registers.adc_ch0_raw = (adc_raw_t)(accum_ch0 / rows_to_parse);
    g_io_node.registers.adc_ch1_raw = (adc_raw_t)(accum_ch1 / rows_to_parse);
    g_io_node.registers.adc_ch2_raw = (adc_raw_t)(accum_ch2 / rows_to_parse);
}

/**
 * @brief Asynchronous network event interceptor called by Modbus engine.
 * Dispatched instantly when a valid incoming wire request frame is fully captured.
 */
void modbus_server_on_request_ready(const modbus_rx_request_t *req)
{
    if (req == NULL) {
        return;
    }

    uint8_t tx_serialization_buf[260];
    size_t tx_bytes_written = 0u;

    if (req->function_code == 0x03u) { /* Read Holding Registers */
        uint16_t start_reg = ((uint16_t)req->payload[0] << 8u) | req->payload[1];
        uint16_t reg_qty = ((uint16_t)req->payload[2] << 8u) | req->payload[3];

        uint8_t data_payload[250];
        if (mb_regs_read(&g_io_node.registers, start_reg, reg_qty, data_payload, sizeof(data_payload))) {
            tx_bytes_written = modbus_build_response(
                req->slave_addr, req->function_code, data_payload, (size_t)reg_qty * 2u, 
                tx_serialization_buf, sizeof(tx_serialization_buf)
            );
        } else {
            tx_bytes_written = modbus_build_exception(
                req->slave_addr, req->function_code, MODBUS_EX_ILLEGAL_DATA_ADDR, 
                tx_serialization_buf, sizeof(tx_serialization_buf)
            );
        }
    } else if (req->function_code == 0x06u) { /* Write Single Register */
        uint16_t target_reg = ((uint16_t)req->payload[0] << 8u) | req->payload[1];
        uint16_t write_val = ((uint16_t)req->payload[2] << 8u) | req->payload[3];

        bool cfg_changed = false;
        if (mb_regs_write_single(&g_io_node.registers, target_reg, write_val, &cfg_changed)) {
            tx_bytes_written = modbus_build_response(
                req->slave_addr, req->function_code, req->payload, req->payload_len, 
                tx_serialization_buf, sizeof(tx_serialization_buf)
            );
            if (cfg_changed) {
                app_sync_runtime_configuration(&g_io_node);
            }
        } else {
            tx_bytes_written = modbus_build_exception(
                req->slave_addr, req->function_code, MODBUS_EX_ILLEGAL_DATA_VAL, 
                tx_serialization_buf, sizeof(tx_serialization_buf)
            );
        }
    } else {
        tx_bytes_written = modbus_build_exception(
            req->slave_addr, req->function_code, MODBUS_EX_ILLEGAL_FUNC, 
            tx_serialization_buf, sizeof(tx_serialization_buf)
        );
    }

    if (tx_bytes_written > 0u) {
        /* Route serialized package byte stream directly to the UART DMA hardware transmission pipeline */
        // extern bool uart1_tx_dma_async(const uint8_t *data, size_t len);
        // (void)uart1_tx_dma_async(tx_serialization_buf, tx_bytes_written);
    }
}

/* --------------------------------------------------------------------------- */
/* ⏰ Periodic Task Handlers                                                    */
/* --------------------------------------------------------------------------- */

static void task_mcu_control_loop(void *ctx)
{
    app_t *app = (app_t *)ctx;
    uint32_t now = platform_millis();

    /* Process DSP conditioning filtering and step the PID output calculation */
    app_task_process_control(app, now);

    /* Write final decision duty cycle directly into hardware timer CCR registers */
    pwm_hardware_set_duty(app->registers.pwm_current_duty);
}

static void task_mcu_logger_flush(void *ctx)
{
    (void)ctx;
    logger_process_flush_task();
    platform_poll(); /* Flush low-level hardware background tasks */
}

static void task_mcu_modbus_network(void *ctx)
{
    app_t *app = (app_t *)ctx;
    
    /* TODO: Implement full DMA-based Modbus frame reception and parsing here.
     * Currently, we only mark the watchdog ticket to prove the task is alive.
     * The 'now' timestamp will be required for inter-frame gap detection 
     * in future iterations of the protocol stack. */
     
    uint32_t now = platform_millis();
    (void)now; /* Suppress -Wunused-variable warning until logic is implemented */
    
    app_watchdog_mark_ticket(app, APP_TICKET_MODBUS);
}

static void task_mcu_adc_log(void *ctx)
{
    app_t *app = (app_t *)ctx;

    if (app == NULL) {
        return;
    }

    /* Stage 2.4.1: prove DMA -> callback -> registers -> logger -> UART chain.
     *
     * This clone does not latch ADC_CONT, so we log the result of the previous
     * software-kicked single scan and then request the next one.
     */
    logger_write_str("ADC0 r=");
    logger_write_u32_padded((uint32_t)app->registers.adc_ch0_raw, 4u);
    logger_write_str(" f=");
    logger_write_u32_padded((uint32_t)app->registers.adc_ch0_filtered, 4u);
    logger_new_line();

    /* Arm one new 3-channel ADC DMA scan for the next logging period. */
    adc_hardware_kick_scan();
}

/* --------------------------------------------------------------------------- */
/* 🏁 Hardware Execution Entry Point                                           */
/* --------------------------------------------------------------------------- */
int main(void)
{
    /* 1. Initialize low-level system platform clocks, GPIO ports, and PVD block */
    // Note: stm32f1_platform_init() is declared inside platform_stm32f1.h configuration wrappers
    extern void stm32f1_platform_init(void);
    stm32f1_platform_init();

    /* Stage 2.2: real time-base + LED heartbeat (MCU BSP, extern per convention). */
    extern void platform_systick_init(void);
    extern void platform_led_init(void);
    extern void platform_led_toggle(void);
    platform_systick_init();
    platform_led_init();

    /* 2. Read hardware reset cause triggers immediately upon early boot sequence */
    reset_cause_t boot_cause = platform_reset_cause_get();

    /* 3. Initialize Independent Watchdog (IWDG) security lock with a 2-second timeout window */
    platform_wdg_init(2000u);

    /* 3.5 Stage 2.3: bring up UART TX + logger EARLY (after WDG, before app init),
     * so any later fault during app_init/wl/adc/pwm is observable. Blocking TX is
     * fine here: it runs once at boot, before the scheduler exists. */
    extern void uart1_init(void);
    uart1_init();
    logger_init();
    logger_write_str("AIO-NODE boot");
    logger_new_line();
    logger_write_str("reset cause: ");
    logger_write_u32((uint32_t)boot_cause);
    logger_new_line();
    logger_write_str("Stage 2.3 UART TX ready @115200 (HSI 8MHz)");
    logger_new_line();
    logger_flush_blocking();

    /* 4. Boot application composite root layers */
    app_init(&g_io_node, boot_cause);

    /* 5. Recover or synchronize persistent variables inside Flash via Wear Leveling */
    (void)wl_service_init(&g_io_node.registers);

    /* 6. Fire up hardware ADC scanning pipelines backed by continuous DMA streams */
    (void)adc_hardware_init(mcu_adc_dma_callback);

    /* 7. Initialize hardware timer peripheral for PWM regulation load control */
    (void)pwm_hardware_init();

    /* 8. Construct tasks allocation arrays inside background static manager */
    static sched_task_t mcu_tasks_buffer[4u];
    sched_init(&g_mcu_scheduler, mcu_tasks_buffer, 4u);

    (void)sched_task_register(&g_mcu_scheduler, 10u, task_mcu_control_loop, &g_io_node, "control");
    (void)sched_task_register(&g_mcu_scheduler, 1u,  task_mcu_logger_flush,  NULL,       "logger");
    (void)sched_task_register(&g_mcu_scheduler, 10u, task_mcu_modbus_network, &g_io_node, "modbus");
    (void)sched_task_register(&g_mcu_scheduler, 1000u, task_mcu_adc_log, &g_io_node, "adclog");

    /* 9. Core Hardware Execution Loop (Ultra low-power deterministic sleep pattern) */
    uint32_t last_beat_ms = 0u;   /* Stage 2.2 heartbeat edge tracker */
    for (;;) {
        /* Clean active checkpoint tickets before computing the new orchestrator iteration */
        app_watchdog_clear_tickets(&g_io_node);
        uint32_t current_time = platform_millis();
        /* Stage 2.2 heartbeat: unconditional proof-of-life toggle every 500 ms.
         * Placed BEFORE scheduler/watchdog so the blink proves CPU + SysTick +
         * GPIO independently of task-ticket health. */
        if ((current_time - last_beat_ms) >= 500u) {
            last_beat_ms = current_time;
            platform_led_toggle();
        }

        (void)sched_execute_orchestrator(&g_mcu_scheduler, current_time);

        /* Security Guard: feed Watchdog ONLY if all execution tracks report complete health status */
        if (app_watchdog_can_feed(&g_io_node)) {
            platform_wdg_feed();
        }

        /* Force central CPU core to sleep until the next hardware interrupt (DMA/SysTick) occurs */
        platform_wfi();
    }

    return 0;
}
