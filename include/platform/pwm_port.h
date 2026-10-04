#ifndef PLATFORM_PWM_PORT_H
#define PLATFORM_PWM_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"

/**
 * @brief Configure and initiate the hardware timer peripheral for PWM generation.
 * Sets up base frequency (e.g., 1 kHz for industrial heaters/power loads), 
 * configures the output pin channel, and enforces an initial safe 0.00% duty cycle.
 * 
 * @return true if timer registers successfully configured, false on hardware fault.
 */
bool pwm_hardware_init(void);

/**
 * @brief Update the hardware timer register to modify the active duty cycle.
 * MISRA C:2012 Compliance: Enforces hard boundary checks on input arguments.
 * 
 * @param duty New duty cycle payload scaled to 0..10000 range (0.00% to 100.00%).
 *        Values exceeding limits will be safely clamped to protect execution.
 */
void pwm_hardware_set_duty(pwm_duty_t duty);

/**
 * @brief Read the current duty cycle value physically set inside the timer CCR register.
 * @return pwm_duty_t Hardware register value in 0..10000 scale.
 */
pwm_duty_t pwm_hardware_get_duty(void);

/**
 * @brief Emergency hardware-level shutdown.
 * Instantly forces the output pin configuration to Low/Hi-Z state to isolate the load,
 * bypassing standard execution loop delays.
 */
void pwm_hardware_emergency_disable(void);

#endif /* PLATFORM_PWM_PORT_H */
