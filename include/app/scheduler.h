#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Functional pointer type definition for a scheduler task callback.
 * @param ctx Generic pointer referencing the task's private context data block.
 */
typedef void (*sched_task_fn)(void *ctx);

/**
 * @brief Task control block structure representing a single periodic execution node.
 */
typedef struct {
    uint32_t period_ms;    /* Desired execution period interval in milliseconds */
    uint32_t next_run_ms;   /* Internal monotonic timestamp marker for the next activation */
    sched_task_fn task_fn;  /* Executable callback function pointer */
    void *task_ctx;         /* Opaque pointer referencing mutable task variables space */
    const char *task_name;  /* Debug identity string compliant with MISRA telemetry */
} sched_task_t;

/**
 * @brief Consolidated scheduler management context handling the static tasks array.
 */
typedef struct {
    sched_task_t *tasks_array; /* Static allocation table pointing to the task entries */
    size_t max_capacity;        /* Maximum boundary size of the static array allocation */
    size_t active_count;       /* Total tasks currently registered inside the execution engine */
} scheduler_t;

/**
 * @brief Initialize the static cooperative scheduler context block.
 * 
 * @param s Pointer to the uninitialized scheduler_t manager instance.
 * @param static_tasks_buffer Pointer to a statically allocated array of sched_task_t nodes.
 * @param capacity Maximum capacity slots supported by the static buffer array.
 */
void sched_init(scheduler_t *s, sched_task_t *static_tasks_buffer, size_t capacity);

/**
 * @brief Register a periodic functional task into the scheduler execution table.
 * MISRA C:2012 Compliance: Rejects dynamic runtime allocation, enforces static bounds checking.
 * 
 * @param s Pointer to the active scheduler manager context.
 * @param period_ms Desired task loop interval (must be greater than 0).
 * @param fn Valid task execution function pointer callback.
 * @param ctx Opaque task context data block pointer (Dependency Injection pattern).
 * @param name Debug string literal tagging the component.
 * @return true if task registration successfully fits into bounds, false otherwise.
 */
bool sched_task_register(
    scheduler_t *s,
    uint32_t period_ms,
    sched_task_fn fn,
    void *ctx,
    const char *name);

/**
 * @brief Execute one non-blocking tick cycle checking timestamps against system time.
 * Processes tasks sequentially if their execution window is due.
 * 
 * @param s Pointer to the active operational scheduler instance.
 * @param current_time_ms Fresh monotonic clock tick directly from platform_millis().
 * @return true if at least one due task was executed during this cycle, false if idle.
 */
bool sched_execute_orchestrator(scheduler_t *s, uint32_t current_time_ms);

#endif /* APP_SCHEDULER_H */
