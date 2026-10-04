#include "app/scheduler.h"

/**
 * @brief Helper utility evaluating if a specific time coordinate has arrived.
 * Exploits signed arithmetic properties of Two's Complement to guarantee 
 * mathematically flawless handling of 32-bit timer overflows (wrap-around).
 */
static bool sched_is_task_due(uint32_t current_time_ms, uint32_t scheduled_time_ms)
{
    /* Cast subtraction to signed integer to safely catch continuous modulo wrapping */
    return ((int32_t)(current_time_ms - scheduled_time_ms) >= 0L);
}

void sched_init(scheduler_t *s, sched_task_t *static_tasks_buffer, size_t capacity)
{
    if ((s == NULL) || (static_tasks_buffer == NULL)) {
        return;
    }

    s->tasks_array = static_tasks_buffer;
    s->max_capacity = capacity;
    s->active_count = 0u;
}

bool sched_task_register(
    scheduler_t *s,
    uint32_t period_ms,
    sched_task_fn fn,
    void *ctx,
    const char *name)
{
    if ((s == NULL) || (s->tasks_array == NULL)) {
        return false;
    }

    if ((fn == NULL) || (period_ms == 0u)) {
        return false;
    }

    /* Enforce rigid static boundary limits to block memory layout degradation */
    if (s->active_count >= s->max_capacity) {
        return false;
    }

    /* Locate fresh array slot to commit the Task Control Block properties */
    sched_task_t *t = &s->tasks_array[s->active_count];

    t->period_ms = period_ms;
    t->next_run_ms = 0u; /* Default execution starts immediately upon orchestrator activation */
    t->task_fn = fn;
    t->task_ctx = ctx;
    t->task_name = name;

    s->active_count++;

    return true;
}

bool sched_execute_orchestrator(scheduler_t *s, uint32_t current_time_ms)
{
    if ((s == NULL) || (s->tasks_array == NULL)) {
        return false;
    }

    bool executed_any_task = false;

    /* Process registered operational slots sequentially (Cooperative non-preemptive logic) */
    for (size_t i = 0u; i < s->active_count; ++i) {
        sched_task_t *t = &s->tasks_array[i];

        /* Verify execution window constraints for the active slot entry */
        if (!sched_is_task_due(current_time_ms, t->next_run_ms)) {
            continue;
        }

        /* Launch functional callback (Dependency Injection context execution) */
        t->task_fn(t->task_ctx);
        executed_any_task = true;

        /* Anti-Drift Rescheduling Pattern: incremental step based on static period */
        t->next_run_ms += t->period_ms;

        /* 
         * Safety Catch-Up Routine: If a massive lag block shifts execution past multiple 
         * processing intervals, enforce drop-frame policy. Reschedule target from now 
         * to prevent execution cascading bursts.
         */
        if (sched_is_task_due(current_time_ms, t->next_run_ms)) {
            t->next_run_ms = current_time_ms + t->period_ms;
        }
    }

    return executed_any_task;
}
