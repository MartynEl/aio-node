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

    /* --- НАШ НОВЫЙ ЗАЩИЩЕННЫЙ ЗАПУСК --- */
    /* Запускаем триггеры АЦП строго ПОСЛЕ того, как TIM1 отстрелялся своим EGR */
    adc_hardware_start_trigger();
    /* ----------------------------------- */

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