#ifndef CORE_PID_H
#define CORE_PID_H

#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"

/**
 * @brief Initialize the discrete PID controller context variables.
 * 
 * @param pid Pointer to the execution context structure block.
 * @param kp Default proportional gain coefficient scaled to Q10 (0..32767).
 * @param ki Default integral gain coefficient scaled to Q10 (0..32767).
 * @param kd Default derivative gain coefficient scaled to Q10 (0..32767).
 */
void pid_init(pid_controller_t *pid, int16_t kp, int16_t ki, int16_t kd);

/**
 * @brief Update ПИД-регулятор coefficients on the fly (e.g., via Modbus).
 */
void pid_set_gains(pid_controller_t *pid, int16_t kp, int16_t ki, int16_t kd);

/**
 * @brief Set a new target coordinate coordinate setpoint layer.
 * @param pid Pointer to the active pid_controller_t context.
 * @param setpoint New target position value clamped to physical ADC boundaries (0..4095).
 */
void pid_set_setpoint(pid_controller_t *pid, int16_t setpoint);

/**
 * @brief Compute the discrete PID controller step output based on feedback loop error.
 * MISRA C:2012 Compliance: Enforces hard anti-windup clamping to protect integers from overflow.
 * 
 * @param pid Pointer to the active pid_controller_t execution context block.
 * @param current_feedback Clean filtered value directly from the DSP cascade filter.
 * @return pwm_duty_t Calculated load duty cycle ready for timer assignment (0..10000 range).
 */
pwm_duty_t pid_update(pid_controller_t *pid, adc_raw_t current_feedback);

/**
 * @brief Reset the internal integral error accumulator tracking context.
 * Prevents legacy error bumps when switching from Manual to Auto mode.
 */
void pid_reset_integral(pid_controller_t *pid);

#endif /* CORE_PID_H */
