/* src/app/main_mcu.c */
#include "app/app.h"
#include "app/scheduler.h"
#include "platform/platform.h"
#include "platform/adc_port.h"
#include "platform/pwm_port.h"
#include "platform/uart1.h"
#include "service/logger.h"
#include "service/modbus.h"
#include "service/wear_leveling.h"

#include <stddef.h>

/* Global static application context root object instance */
app_t g_io_node;

/* Global scheduler context */
static scheduler_t g_mcu_scheduler;

/* Static scheduler task pool. Size 4 keeps room for future Stage tasks. */
static sched_task_t g_mcu_tasks_buffer[4u];

/* BSP / platform entry points implemented in STM32 platform layer. */
extern void stm32f1_platform_init(void);
extern void platform_systick_init(void);
extern void platform_led_init(void);
extern void platform_led_toggle(void);

/* --------------------------------------------------------------------------- */
/* Asynchronous Hardware Interrupt Callbacks (Zero-Copy)                       */
/* --------------------------------------------------------------------------- */

/**
 * @brief Asynchronous callback triggered directly by hardware ADC+DMA ISR.
 * Stores the latest single-scan snapshot into the Modbus register mirror.
 */
static void mcu_adc_dma_callback(const adc_raw_t *buffer_ptr,
                                 size_t samples_count,
                                 adc_event_t event)
{
    if ((buffer_ptr == NULL) ||
        (event == ADC_EVENT_HARDWARE_ERROR) ||
        (samples_count < 3u)) {
        g_io_node.registers.system_fault_mask |= FAULT_BIT_ADC_DMA;
        g_io_node.registers.device_state = (uint16_t)DEVICE_STATE_SAFE_MODE;
        pwm_hardware_emergency_disable();
        return;
    }

    /* Atomic snapshot of one regular ADC scan across three channels. */
    g_io_node.registers.adc_ch0_raw = buffer_ptr[0u];
    g_io_node.registers.adc_ch1_raw = buffer_ptr[1u];
    g_io_node.registers.adc_ch2_raw = buffer_ptr[2u];
}

/**
 * @brief Modbus RTU server-side request dispatcher.
 *
 * This function is called only after a complete frame has been captured by
 * the UART RX ring buffer and validated by the Modbus parser.
 *
 * Supported function codes:
 *   0x03 — Read Holding Registers
 *   0x06 — Write Single Register
 *
 * Broadcast address 0x00 is accepted by the parser but never answered,
 * as required by Modbus RTU specification.
 */
void modbus_server_on_request_ready(const modbus_rx_request_t *req)
{
    uint8_t tx_serialization_buf[260];
    size_t tx_bytes_written = 0u;
    bool respond;

    if ((req == NULL) || (req->payload == NULL)) {
        return;
    }

    /* Modbus RTU slaves must not reply to broadcast address 0. */
    respond = (req->slave_addr != 0u);

    if (req->function_code == 0x03u) {
        /* Read Holding Registers. */
        if (req->payload_len != 4u) {
            tx_bytes_written = modbus_build_exception(
                req->slave_addr,
                req->function_code,
                MODBUS_EX_ILLEGAL_DATA_VAL,
                tx_serialization_buf,
                sizeof(tx_serialization_buf)
            );
        } else {
            uint16_t start_reg;
            uint16_t reg_qty;
            uint16_t data_bytes;
            uint16_t crc;
            size_t frame_no_crc;

            start_reg = (uint16_t)(((uint16_t)req->payload[0] << 8u) |
                                   (uint16_t)req->payload[1]);

            reg_qty = (uint16_t)(((uint16_t)req->payload[2] << 8u) |
                                 (uint16_t)req->payload[3]);

            if ((reg_qty == 0u) || (reg_qty > 125u)) {
                tx_bytes_written = modbus_build_exception(
                    req->slave_addr,
                    req->function_code,
                    MODBUS_EX_ILLEGAL_DATA_VAL,
                    tx_serialization_buf,
                    sizeof(tx_serialization_buf)
                );
            } else {
                data_bytes = (uint16_t)((uint32_t)reg_qty * 2u);

                if (((size_t)data_bytes + 5u) > sizeof(tx_serialization_buf)) {
                    tx_bytes_written = modbus_build_exception(
                        req->slave_addr,
                        req->function_code,
                        MODBUS_EX_ILLEGAL_DATA_VAL,
                        tx_serialization_buf,
                        sizeof(tx_serialization_buf)
                    );
                } else if (mb_regs_read(&g_io_node.registers,
                                        start_reg,
                                        reg_qty,
                                        &tx_serialization_buf[3u],
                                        (size_t)data_bytes)) {
                    /*
                     * Correct FC=03 response layout:
                     *
                     *   [0] slave_addr
                     *   [1] function_code = 0x03
                     *   [2] byte_count = 2 * quantity
                     *   [3..] register data, Big-Endian
                     *   [3 + byte_count] CRC_L
                     *   [4 + byte_count] CRC_H
                     */
                    tx_serialization_buf[0u] = req->slave_addr;
                    tx_serialization_buf[1u] = req->function_code;
                    tx_serialization_buf[2u] = (uint8_t)data_bytes;

                    frame_no_crc = (size_t)3u + (size_t)data_bytes;

                    crc = modbus_crc16(tx_serialization_buf, frame_no_crc);

                    tx_serialization_buf[frame_no_crc] =
                        (uint8_t)(crc & 0xFFu);

                    tx_serialization_buf[frame_no_crc + 1u] =
                        (uint8_t)((crc >> 8u) & 0xFFu);

                    tx_bytes_written = frame_no_crc + 2u;
                } else {
                    tx_bytes_written = modbus_build_exception(
                        req->slave_addr,
                        req->function_code,
                        MODBUS_EX_ILLEGAL_DATA_ADDR,
                        tx_serialization_buf,
                        sizeof(tx_serialization_buf)
                    );
                }
            }
        }
    } else if (req->function_code == 0x06u) {
        /* Write Single Register. */
        if (req->payload_len != 4u) {
            tx_bytes_written = modbus_build_exception(
                req->slave_addr,
                req->function_code,
                MODBUS_EX_ILLEGAL_DATA_VAL,
                tx_serialization_buf,
                sizeof(tx_serialization_buf)
            );
        } else {
            uint16_t target_reg;
            uint16_t write_val;
            bool cfg_changed = false;

            target_reg = (uint16_t)(((uint16_t)req->payload[0] << 8u) |
                                    (uint16_t)req->payload[1]);

            write_val = (uint16_t)(((uint16_t)req->payload[2] << 8u) |
                                   (uint16_t)req->payload[3]);

            if (mb_regs_write_single(&g_io_node.registers,
                                     target_reg,
                                     write_val,
                                     &cfg_changed)) {
                tx_bytes_written = modbus_build_response(
                    req->slave_addr,
                    req->function_code,
                    req->payload,
                    req->payload_len,
                    tx_serialization_buf,
                    sizeof(tx_serialization_buf)
                );

                if (cfg_changed) {
                    app_sync_runtime_configuration(&g_io_node);
                }
            } else {
                tx_bytes_written = modbus_build_exception(
                    req->slave_addr,
                    req->function_code,
                    MODBUS_EX_ILLEGAL_DATA_VAL,
                    tx_serialization_buf,
                    sizeof(tx_serialization_buf)
                );
            }
        }
    } else {
        tx_bytes_written = modbus_build_exception(
            req->slave_addr,
            req->function_code,
            MODBUS_EX_ILLEGAL_FUNC,
            tx_serialization_buf,
            sizeof(tx_serialization_buf)
        );
    }

    if (respond && (tx_bytes_written > 0u)) {
        uart1_write_raw(tx_serialization_buf, tx_bytes_written);
    }
}

/* --------------------------------------------------------------------------- */
/* Periodic Task Handlers                                                       */
/* --------------------------------------------------------------------------- */

static void task_mcu_control_loop(void *ctx)
{
    app_t *app;
    uint32_t now;
    static uint32_t soft_accum_ch0 = 0u;
    static uint32_t soft_samples_count = 0u;

    app = (app_t *)ctx;
    if (app == NULL) {
        return;
    }

    now = platform_millis();

    /*
     * Software oversampling accumulator.
     * Hardware ADC remains in stable single-conversion mode 1u.
     * Every 10 ms task quantum adds one fresh DMA snapshot.
     * After 16 samples (~160 ms) the averaged value is fed into DSP/PID.
     */
    soft_accum_ch0 += (uint32_t)app->registers.adc_ch0_raw;
    ++soft_samples_count;

    if (soft_samples_count >= 16u) {
        app->registers.adc_ch0_raw = (adc_raw_t)(soft_accum_ch0 / 16u);
        soft_accum_ch0 = 0u;
        soft_samples_count = 0u;
    }

    /* DSP cascade + PID calculation + watchdog tickets DSP/PID. */
    app_task_process_control(app, now);

    /* Apply final duty cycle to TIM1 hardware register. */
    pwm_hardware_set_duty(app->registers.pwm_current_duty);

    /* Kick next single ADC scan for the next control quantum. */
    adc_hardware_kick_scan();
}

static void task_mcu_logger_flush(void *ctx)
{
    (void)ctx;

    logger_process_flush_task();
    platform_poll();
}

static void task_mcu_modbus_network(void *ctx)
{
    app_t *app;
    uint32_t now;
    static uint8_t rx_frame[260];
    static size_t rx_len = 0u;
    static uint32_t last_rx_time = 0u;

    app = (app_t *)ctx;
    if (app == NULL) {
        return;
    }

    now = platform_millis();

    /*
     * Drain all bytes accumulated by USART1 ISR since the previous task run.
     * The ISR never calls Modbus logic; it only pushes bytes into the ring.
     */
    if (rx_len < sizeof(rx_frame)) {
        size_t got = uart1_rx_read(&rx_frame[rx_len],
                                   sizeof(rx_frame) - rx_len);

        if (got > 0u) {
            rx_len += got;
            last_rx_time = now;
        }
    } else {
        /*
         * Frame buffer overflow. Drop and resynchronize.
         * This should not happen for legal Modbus RTU frames <= 256 bytes.
         */
        rx_len = 0u;
    }

    /*
     * Modbus RTU end-of-frame detection by inter-frame silence.
     *
     * At 9600 8N1:
     *   1 character = 10 bits = ~1.041 ms
     *   t3.5        = ~3.644 ms
     *
     * 5 ms is a conservative debug threshold.
     * Scheduler period is 10 ms, so detection granularity is bounded by
     * the task period, but no bytes are lost because RX is ISR-driven.
     */
    if ((rx_len > 0u) && ((now - last_rx_time) > 5u)) {
        modbus_rx_request_t req;
        modbus_parse_err_t err;

        err = modbus_parse_request(rx_frame,
                                   rx_len,
                                   (uint8_t)app->registers.cfg_slave_address,
                                   &req);

        if (err == MODBUS_PARSE_OK) {
            modbus_server_on_request_ready(&req);
        }

        rx_len = 0u;
    }

    app_watchdog_mark_ticket(app, APP_TICKET_MODBUS);
}

/* --------------------------------------------------------------------------- */
/* Hardware Execution Entry Point                                               */
/* --------------------------------------------------------------------------- */

int main(void)
{
    reset_cause_t boot_cause;
    uint32_t last_beat_ms;

    /* 1. Low-level platform init: clocks, GPIO base, PVD placeholders. */
    stm32f1_platform_init();

    /* 2. Real 1 ms time base. */
    platform_systick_init();

    /* 3. Heartbeat LED. */
    platform_led_init();

    /* 4. Reset cause must be read before any flag clearing side effects. */
    boot_cause = platform_reset_cause_get();

    /* 5. Independent watchdog: 2 s window. */
    platform_wdg_init(2000u);

    /*
     * 6. UART1 bring-up.
     *
     * Stage 3.0: UART1 is reserved for Modbus RTU.
     * Boot banner is disabled by default to avoid unsolicited ASCII frames
     * on the RS-485 bus. Define AIO_UART_BOOT_BANNER only for temporary
     * human-readable UART debugging.
     */
    uart1_init();

    logger_init();
    logger_set_level(LOG_LEVEL_ERROR);

#if defined(AIO_UART_BOOT_BANNER)
    logger_write_str("AIO-NODE boot");
    logger_new_line();
    logger_write_str("reset cause: ");
    logger_write_u32((uint32_t)boot_cause);
    logger_new_line();
    logger_write_str("Stage 3.0 Modbus RTU bring-up @9600 (HSI 8MHz)");
    logger_new_line();
    logger_flush_blocking();
#endif

    /* 7. Application composition root. */
    app_init(&g_io_node, boot_cause);

    /* 8. Restore persistent configuration from Flash emulator. */
    (void)wl_service_init(&g_io_node.registers);

    /* 9. ADC + DMA pipeline. */
    (void)adc_hardware_init(mcu_adc_dma_callback);

    /* 10. PWM output. */
    (void)pwm_hardware_init();

    /* 11. Cooperative scheduler registration. */
    sched_init(&g_mcu_scheduler, g_mcu_tasks_buffer, 4u);

    (void)sched_task_register(&g_mcu_scheduler,
                              10u,
                              task_mcu_control_loop,
                              &g_io_node,
                              "control");

    (void)sched_task_register(&g_mcu_scheduler,
                              1u,
                              task_mcu_logger_flush,
                              NULL,
                              "logger");

    (void)sched_task_register(&g_mcu_scheduler,
                              10u,
                              task_mcu_modbus_network,
                              &g_io_node,
                              "modbus");

    /* 12. Superloop. */
    last_beat_ms = 0u;

    for (;;) {
        app_watchdog_clear_tickets(&g_io_node);

        uint32_t current_time = platform_millis();

        /*
         * Unconditional proof-of-life heartbeat.
         * Placed before scheduler/watchdog so LED toggle proves:
         * CPU + SysTick + GPIO independently of task-ticket health.
         */
        if ((current_time - last_beat_ms) >= 500u) {
            last_beat_ms = current_time;
            platform_led_toggle();
        }

        (void)sched_execute_orchestrator(&g_mcu_scheduler, current_time);

        if (app_watchdog_can_feed(&g_io_node)) {
            platform_wdg_feed();
        }

        platform_wfi();
    }

    return 0;
}