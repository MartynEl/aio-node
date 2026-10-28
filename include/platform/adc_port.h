#ifndef PLATFORM_ADC_PORT_H
#define PLATFORM_ADC_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "core/types.h"

/**
 * @brief Total synchronized analog channels executed by hardware DMA scan loop.
 */
#define ADC_CHANNELS_COUNT  3u

/**
 * @brief Oversampling depth factor. Hardware DMA collects this many samples 
 * per individual channel before triggering the processing cycle callback.
 */
#define ADC_SAMPLES_PER_CHANNEL 1u

/**
 * @brief Asynchronous hardware DMA processing events.
 */
typedef enum {
    ADC_EVENT_HALF_COMPLETE, /* DMA filled first half of the continuous circular buffer */
    ADC_EVENT_FULL_COMPLETE, /* DMA filled second half (wrap-around) of the circular buffer */
    ADC_EVENT_HARDWARE_ERROR /* Hardware peripheral or DMA stream transmission fault detected */
} adc_event_t;

/**
 * @brief Hardware DMA event notification function pointer callback.
 * 
 * @param buffer_buffer_ptr Const pointer directly referencing active DMA memory chunk (Zero-copy).
 * @param samples_count Total available samples inside the ready chunk array.
 * @param event Triggered hardware DMA status event.
 */
typedef void (*adc_callback_fn)(const adc_raw_t *buffer_buffer_ptr, size_t samples_count, adc_event_t event);

/**
 * @brief Configure and initiate the low-level multi-channel ADC peripheral.
 * Sets up scanning mode, internal clocking, and connects the continuous DMA pipeline.
 * 
 * @param callback Valid event handler function pointer to delegate data packs to Core layer.
 * @return true if hardware registers verified and execution loop running, false on fault.
 */
bool adc_hardware_init(adc_callback_fn callback);

/**
 * @brief Emergency lock halting the hardware ADC conversion loop.
 * Invoked under critical safe state requirements.
 */
void adc_hardware_stop(void);

/**
 * @brief Request one single ADC DMA scan.
 *
 * Workaround for silicon/clone variants where ADC_CR2.CONT cannot be latched.
 * Each call starts exactly one regular conversion sequence and arms DMA to
 * generate a half-transfer event after ADC_CHANNELS_COUNT samples.
 */
void adc_hardware_kick_scan(void);

#endif /* PLATFORM_ADC_PORT_H */
