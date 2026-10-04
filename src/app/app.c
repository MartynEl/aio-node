#include "app/app.h"
#include "core/filter.h"
#include "core/pid.h"
#include <stddef.h>

void app_init(app_t *app, reset_cause_t cause)
{
    if (app == NULL) {
        return;
    }

    /* 1. Initialize the Modbus memory-mapped holding registers table */
    mb_regs_init(&app->registers);
    app->registers.crash_reset_count = (uint16_t)cause;

    /* 2. Initialize dual-stage DSP cascade filters for all 3 channels */
    uint16_t default_alpha = app->registers.cfg_filter_alpha;
    filter_cascade_init(&app->ch0_dsp, default_alpha);
    filter_cascade_init(&app->ch1_dsp, default_alpha);
    filter_cascade_init(&app->ch2_dsp, default_alpha);

    /* 3. Initialize discrete fixed-point PID controller context */
    pid_init(
        &app->controller,
        (int16_t)app->registers.cfg_pid_kp,
        (int16_t)app->registers.cfg_pid_ki,
        (int16_t)app->registers.cfg_pid_kd
    );
    pid_set_setpoint(&app->controller, (int16_t)app->registers.cfg_target_setpoint);

    /* 4. Configure Watchdog static ticket enforcement masks */
    app->active_wdt_tickets = 0u;
    /* Enforcement policy: Modules DSP, PID, and MODBUS must report execution health */
    app->required_wdt_tickets = APP_TICKET_DSP | APP_TICKET_PID | APP_TICKET_MODBUS;

    app->next_control_cycle_ms = 0u;
    app->registers.device_state = (uint16_t)DEVICE_STATE_RUN;
}

void app_task_process_control(app_t *app, uint32_t now_ms)
{
    if (app == NULL) {
        return;
    }

    /* Forced safety trap: If system is in a locked Safe Mode state, isolate outputs */
    if (app->registers.device_state == (uint16_t)DEVICE_STATE_SAFE_MODE) {
        app->registers.pwm_current_duty = 0u;
        return;
    }

    /* --------------------------------------------------------------------- */
    /* Step 1: Execute Digital Signal Processing (DSP) Cascade Filters       */
    /* --------------------------------------------------------------------- */
    app->registers.adc_ch0_filtered = filter_cascade_update(&app->ch0_dsp, app->registers.adc_ch0_raw);
    app->registers.adc_ch1_filtered = filter_cascade_update(&app->ch1_dsp, app->registers.adc_ch1_raw);
    app->registers.adc_ch2_filtered = filter_cascade_update(&app->ch2_dsp, app->registers.adc_ch2_raw);
    
    /* Mark DSP component task checkpoint as alive */
    app_watchdog_mark_ticket(app, APP_TICKET_DSP);

    /* --------------------------------------------------------------------- */
    /* Step 2: Sync Dynamic Modbus Control Parameters into Core Loops        */
    /* --------------------------------------------------------------------- */
    filter_set_alpha(&app->ch0_dsp, app->registers.cfg_filter_alpha);
    filter_set_alpha(&app->ch1_dsp, app->registers.cfg_filter_alpha);
    filter_set_alpha(&app->ch2_dsp, app->registers.cfg_filter_alpha);

    pid_set_gains(
        &app->controller,
        (int16_t)app->registers.cfg_pid_kp,
        (int16_t)app->registers.cfg_pid_ki,
        (int16_t)app->registers.cfg_pid_kd
    );
    pid_set_setpoint(&app->controller, (int16_t)app->registers.cfg_target_setpoint);

    /* --------------------------------------------------------------------- */
    /* Step 3: Run Regulatory Feedback Control Loop Calculation              */
    /* --------------------------------------------------------------------- */
    pwm_duty_t targeted_duty;

    if (app->registers.cfg_manual_pwm_mode == 1u) {
        /* Manual Override execution path */
        targeted_duty = app->registers.cfg_manual_pwm_duty;
        pid_reset_integral(&app->controller); /* Flush accumulated history to prevent transfer bumps */
    } else {
        /* Automated discrete PID processing execution path. 
         * Feedback loop uses Channel 0 (filtered pressure/process tracker value) */
        targeted_duty = pid_update(&app->controller, app->registers.adc_ch0_filtered);
    }

    /* Wire final mathematical decision directly to the output register mirror */
    app->registers.pwm_current_duty = targeted_duty;

    /* Mark PID automated control loop checkpoint as alive */
    app_watchdog_mark_ticket(app, APP_TICKET_PID);
    
    app->registers.system_uptime_ms = now_ms;
}

void app_sync_runtime_configuration(app_t *app)
{
    if (app == NULL) {
        return;
    }
    /* 
     * TODO: Hook the persistent Flash storage Wear-Leveling update trigger here 
     * once src/service/wear_leveling.c logic is defined.
     */
}

bool app_watchdog_can_feed(const app_t *app)
{
    if (app == NULL) {
        return false;
    }
    /* Strictly verify if execution tickets match rigid design template mask criteria */
    return ((app->active_wdt_tickets & app->required_wdt_tickets) == app->required_wdt_tickets);
}

void app_watchdog_clear_tickets(app_t *app)
{
    if (app != NULL) {
        app->active_wdt_tickets = 0u;
    }
}

void app_watchdog_mark_ticket(app_t *app, uint32_t ticket_id)
{
    if (app != NULL) {
        app->active_wdt_tickets |= ticket_id;
    }
}
