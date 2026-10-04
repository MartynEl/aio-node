#ifndef APP_APP_H
#define APP_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "service/modbus_regs.h"

/**
 * @brief Watchdog task synchronization tickets mask definitions.
 * Every critical task must mark its execution bit before feeding the hardware IWDG.
 */
#define APP_TICKET_DSP      (1u << 0)  /* DSP cascading channels processing task bit */
#define APP_TICKET_PID      (1u << 1)  /* PID feedback execution control loop bit */
#define APP_TICKET_MODBUS   (1u << 2)  /* Modbus RTU network communication service bit */

/**
 * @brief Consolidated application context object wiring core math and network services.
 * Serves as the structural root for Dependency Injection across layers.
 */
typedef struct {
    modbus_reg_map_t registers;     /* Unified memory-mapped Modbus registers block */
    
    dsp_channel_t ch0_dsp;          /* Cascaded DSP filter for Channel 0 (Pressure) */
    dsp_channel_t ch1_dsp;          /* Cascaded DSP filter for Channel 1 (Temperature) */
    dsp_channel_t ch2_dsp;          /* Cascaded DSP filter for Channel 2 (Potentiometer) */
    
    pid_controller_t controller;    /* Integer discrete feedback PID loop context */
    
    uint32_t next_control_cycle_ms; /* Scheduler timestamp marker for control precision */
    
    uint32_t active_wdt_tickets;    /* Dynamic execution bitmask for runtime tasks */
    uint32_t required_wdt_tickets;  /* Rigid bitmask template enforced for safe operation */
} app_t;

/**
 * @brief Initialize the application composite root context.
 * Resolves boot cause, loads flash variables, and wires mathematical contexts.
 * 
 * @param app Pointer to the global app_t instance allocation block.
 * @param cause Hardware evaluation of why the system booted up.
 */
void app_init(app_t *app, reset_cause_t cause);

/**
 * @brief Main execution task processing analog data acquisition and feedback loop math.
 * Invoked periodically by the cooperative task scheduler.
 * 
 * @param app Pointer to the active app_t application context.
 * @param now_ms Monotonic continuous hardware clock timestamp.
 * @return err_t Core error code mapping the execution safety state.
 */
void app_task_process_control(app_t *app, uint32_t now_ms);

/**
 * @brief Intercept and safely apply run-time configuration changes received over Modbus.
 * Triggers Flash storage mirror updates with wear-leveling protection.
 * 
 * @param app Pointer to the active mutable application context.
 */
void app_sync_runtime_configuration(app_t *app);

/**
 * @brief Evaluates whether all synchronized components finished execution cycles cleanly.
 * If true, allows the system layer to feed the hardware Independent Watchdog.
 * 
 * @param app Const pointer to the read-only application context status.
 * @return true if all required tickets match execution status, false otherwise.
 */
bool app_watchdog_can_feed(const app_t *app);

/**
 * @brief Reset the active runtime execution tickets bitmask for a new scheduler loop.
 */
void app_watchdog_clear_tickets(app_t *app);

/**
 * @brief Mark a specific functional task ticket as successfully completed.
 * @param app Pointer to the mutable application context.
 * @param ticket_id Target bit mask identity (e.g., APP_TICKET_DSP).
 */
void app_watchdog_mark_ticket(app_t *app, uint32_t ticket_id);

#endif /* APP_APP_H */
