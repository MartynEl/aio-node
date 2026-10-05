#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/**
 * MISRA C:2012 Rule 4.6 Compliance: Fixed-width aliases for raw ADC and PWM types.
 */
typedef uint16_t adc_raw_t;   /* Hardware ADC resolution: 0..4095 (12-bit) */
typedef uint16_t pwm_duty_t;  /* Hardware PWM resolution: 0..10000 (0.01% steps) */

/**
 * @brief Hardware Reset Causes detected during early boot stage.
 * Mapped from microcontroller register status (e.g., RCC_CSR on STM32).
 */
typedef enum {
    RESET_CAUSE_POWER_ON,    /* Power-On / Power-Down reset (POR / PDR) */
    RESET_CAUSE_WATCHDOG,    /* Independent Watchdog reset triggered by system hang (IWDG) */
    RESET_CAUSE_SOFTWARE,    /* Software reset triggered via NVIC_SystemReset() */
    RESET_CAUSE_EXTERNAL,    /* External Reset pin intervention (NRST) */
    RESET_CAUSE_UNKNOWN      /* Failed to evaluate hardware flags */
} reset_cause_t;

/**
 * @brief Finite State Machine (FSM) execution states of the core controller loop.
 */
typedef enum {
    DEVICE_STATE_INIT = 0u,       /* Peripherals configuration and initial hardware checks */
    DEVICE_STATE_RUN,            /* Normal execution: DSP filtering and PID active */
    DEVICE_STATE_SAFE_MODE,      /* Critical fault state: Load isolated, execution locked */
    DEVICE_STATE_LOW_POWER       /* Power saving state under specific supply conditions */
} device_state_t;

/**
 * @brief Context data structure for Exponential Moving Average (EMA) DSP filter.
 * Fixed-point math is enforced (Q14 representation for processing alpha).
 */
typedef struct {
    uint32_t previous_ema;  /* Scaled historical filter output value */
    uint16_t alpha_q14;     /* Smoothing factor coefficient scaled to 2^14 (0..16384) */
    bool is_initialized;    /* Execution flag preventing initial value step lag */
} ema_filter_t;

/**
 * @brief Context data structure for Median Filter (Window Size = 3).
 * Mandatory pattern for suppressing single high-amplitude impulsive hardware spikes.
 */
typedef struct {
    adc_raw_t sample_buffer[3u]; /* Static sliding sample memory window */
    uint8_t buffer_index;       /* Ring-buffer placement position counter */
} median_filter_t;

/**
 * @brief Consolidated DSP processing block context for a single ADC channel.
 */
typedef struct {
    median_filter_t median;
    ema_filter_t ema;
} dsp_channel_t;

/**
 * @brief Integer Fixed-point Context structure for the discrete PID feedback controller.
 * Coefficients use Q10 scaling factor to bypass float operations on Cortex-M3.
 */
typedef struct {
    int16_t kp_q10;         /* Proportional gain coefficient */
    int16_t ki_q10;         /* Integral gain coefficient */
    int16_t kd_q10;         /* Derivative gain coefficient */
    
    int32_t integral_accum; /* Integrated tracking error accumulator with anti-windup clamp */
    int16_t previous_error; /* Historical tracking error for derivative computation */
    
    int16_t target_setpoint;/* Execution goal coordinate (0..4095 range) */
} pid_controller_t;

#endif /* CORE_TYPES_H */
