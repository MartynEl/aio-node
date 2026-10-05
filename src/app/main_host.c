#include "app/app.h"
#include "app/scheduler.h"
#include "platform/platform.h"
#include "platform/adc_port.h"
#include "platform/pwm_port.h"
#include "service/logger.h"
#include <unistd.h> /* Standard Linux system calls header for write() */
#include "service/modbus.h" // <-- In order to recognize type modbus_request_t

/**
 * External linkage declaration advancing the PC thermodynamic simulation loop.
 * Defined inside src/platform/host/platform_host.c
 */
extern void platform_host_advance_simulation(uint32_t step_ms);

/* Global static application context instance allocation */
static app_t g_io_node;

/**
 * @brief MISRA compliant implementation linking our core logger to Linux OS.
 * Bypasses heavy buffered stdio.h pipelines.
 */
int32_t char_output_stub(const char *data, size_t len);
int32_t char_output_stub(const char *data, size_t len)
{
    /* Redirect directly to Linux Standard Output file descriptor (STDOUT_FILENO = 1) */
    ssize_t result = write(1, data, len);
    return (int32_t)result;
}

/**
 * @brief Dummy placeholder callback implemented to satisfy compiler linkages.
 * Real network event dispatching is covered inside main_mcu.c
 */
void modbus_server_on_request_ready(const modbus_request_t *req)
{
    (void)req;
}

/* --------------------------------------------------------------------------- */
/* 🛠️ Periodic Task Callbacks                                                 */
/* --------------------------------------------------------------------------- */

static void task_control_loop(void *ctx)
{
    app_t *app = (app_t *)ctx;
    uint32_t now = platform_millis();
    
    /* Execute DSP filtering and update automated PID control loop output */
    app_task_process_control(app, now);
}

static void task_logger_flush(void *ctx)
{
    (void)ctx;
    /* Quantized non-blocking extraction flushing strings to Ubuntu terminal */
    logger_process_flush_task();
}

static void task_telemetry_report(void *ctx)
{
    app_t *app = (app_t *)ctx;
    
    if (!logger_level_enabled(LOG_LEVEL_INFO)) {
        return;
    }

    logger_write_str("[SiL Telemetry] Uptime: ");
    logger_write_u32(platform_millis());
    logger_write_str(" ms | Raw Ch0: ");
    logger_write_u32(app->registers.adc_ch0_raw);
    logger_write_str(" | Filtered Ch0: ");
    logger_write_u32(app->registers.adc_ch0_filtered);
    logger_write_str(" | PWM Duty: ");
    logger_write_u32(app->registers.pwm_current_duty / 100u); /* Convert 0..10000 to 0..100% */
    logger_write_str("%");
    logger_new_line();
}

/* --------------------------------------------------------------------------- */
/* 🏁 Execution Entry Point                                                    */
/* --------------------------------------------------------------------------- */
int main(void)
{
    /* Initialize logging infrastructure */
    logger_init();
    logger_set_level(LOG_LEVEL_INFO);
    
    logger_write_str("==================================================\n");
    logger_write_str("🚀 AIO-Node Software-in-the-Loop Simulator Active\n");
    logger_write_str("==================================================\n");
    logger_flush_blocking();

    /* 1. Wire and boot the structured application root */
    app_init(&g_io_node, RESET_CAUSE_POWER_ON);

    /* 2. Bind the simulated ADC callback loop */
    // Note: In PC simulation, adc_hardware_init connects to our host tracking callback
    extern void dsp_adc_hardware_callback(const adc_raw_t *buf, size_t len, adc_event_t event);
    (void)adc_hardware_init(NULL); /* In simple host runner, we advance step loops linearly */

    /* 3. Define and register task schedules into the static orchestrator */
    static sched_task_t tasks_memory_buffer[3u];
    static scheduler_t system_scheduler;
    
    sched_init(&system_scheduler, tasks_memory_buffer, 3u);

    (void)sched_task_register(&system_scheduler, 10u,   task_control_loop,    &g_io_node, "control");
    (void)sched_task_register(&system_scheduler, 1u,    task_logger_flush,     NULL,       "logger");
    (void)sched_task_register(&system_scheduler, 1000u, task_telemetry_report, &g_io_node, "telemetry");

    /* 4. Infinite Superloop: Emulates hardware clock stepping with a 1 ms tick rate */
    const uint32_t simulation_step_ms = 1u;
    const uint32_t total_simulation_ticks = 10000u; /* Run simulator for 10.000 ms (10 seconds) */

    for (uint32_t tick = 0u; tick < total_simulation_ticks; ++tick) {
        /* Advance simulated physical thermodynamics and generate fresh ADC pool inputs */
        platform_host_advance_simulation(simulation_step_ms);

        /* Map simulated raw channel inputs directly to app registers for calculation */
        // (In real hardware, this mapping is done zero-copy inside the DMA interrupt)
        extern adc_raw_t pwm_hardware_get_duty(void);
        g_io_node.registers.adc_ch0_raw = (adc_raw_t)g_io_node.registers.adc_ch0_filtered; /* Simulate loop */

        uint32_t current_time = platform_millis();
        
        /* Tick the deterministic task scheduler manager */
        (void)sched_execute_orchestrator(&system_scheduler, current_time);

        /* Feed Watchdog if execution health tickets match rigid mask criteria */
        if (app_watchdog_can_feed(&g_io_node)) {
            platform_wdg_feed();
        }
        
        /* Clean completed task bits to prepare for the next clock quantum cycle */
        /* Note: For simplicity in the basic host runner, we clear them after cycles */
        app_watchdog_clear_tickets(&g_io_node);
        
        /* Force App tickets manual assert in simplified host loop to mock real interrupt arrivals */
        app_watchdog_mark_ticket(&g_io_node, APP_TICKET_MODBUS); /* Mock Modbus thread is alive */
    }

    logger_write_str("🏁 Simulation complete. Exiting cleanly.\n");
    logger_flush_blocking();

    return 0;
}
