#include "core/pid.h"
#include <stddef.h>

/* Define extreme constraints for the integral accumulator to prevent internal numeric overflows */
#define PID_INTEGRAL_MAX_LIMIT   10240000L
#define PID_INTEGRAL_MIN_LIMIT  -10240000L

void pid_init(pid_controller_t *pid, int16_t kp, int16_t ki, int16_t kd)
{
    if (pid == NULL) {
        return;
    }

    pid->kp_q10 = kp;
    pid->ki_q10 = ki;
    pid->kd_q10 = kd;
    
    pid->integral_accum = 0L;
    pid->previous_error = 0;
    pid->target_setpoint = 2048; /* Center of the 12-bit physical coordinate system */
}

void pid_set_gains(pid_controller_t *pid, int16_t kp, int16_t ki, int16_t kd)
{
    if (pid != NULL) {
        pid->kp_q10 = kp;
        pid->ki_q10 = ki;
        pid->kd_q10 = kd;
    }
}

void pid_set_setpoint(pid_controller_t *pid, int16_t setpoint)
{
    if (pid != NULL) {
        pid->target_setpoint = (setpoint > 4095) ? 4095 : setpoint;
        if (pid->target_setpoint < 0) {
            pid->target_setpoint = 0;
        }
    }
}

void pid_reset_integral(pid_controller_t *pid)
{
    if (pid != NULL) {
        pid->integral_accum = 0L;
    }
}

pwm_duty_t pid_update(pid_controller_t *pid, adc_raw_t current_feedback)
{
    if (pid == NULL) {
        return 0u;
    }

    /* 1. Calculate tracking error */
    int16_t error = pid->target_setpoint - (int16_t)current_feedback;

    /* 2. Compute proportional term (P-component) scaled in Q10 */
    int32_t p_term = (int32_t)pid->kp_q10 * (int32_t)error;

    /* 3. Compute integral term (I-component) with anti-windup clamping constraints */
    pid->integral_accum += (int32_t)pid->ki_q10 * (int32_t)error;
    
    if (pid->integral_accum > PID_INTEGRAL_MAX_LIMIT) {
        pid->integral_accum = PID_INTEGRAL_MAX_LIMIT;
    }
    if (pid->integral_accum < PID_INTEGRAL_MIN_LIMIT) {
        pid->integral_accum = PID_INTEGRAL_MIN_LIMIT;
    }

    /* 4. Compute derivative term (D-component) */
    int16_t error_derivative = error - pid->previous_error;
    int32_t d_term = (int32_t)pid->kd_q10 * (int32_t)error_derivative;
    
    /* Update historical memory tracking */
    pid->previous_error = error;

    /* 5. Consolidate full output and scale back from Q10 space to physical duty space (>> 10) */
    int32_t total_output = (p_term + pid->integral_accum + d_term) >> 10u;

    /* 6. Hardware safety limitation: Clamp final duty cycle values strictly to 0..10000 range */
    pwm_duty_t final_duty;
    if (total_output >= 10000L) {
        final_duty = 10000u; /* Full power cap load (100.00%) */
    } else if (total_output <= 0L) {
        final_duty = 0u;     /* Cutoff load output (0.00%) */
    } else {
        final_duty = (pwm_duty_t)total_output;
    }

    return final_duty;
}
