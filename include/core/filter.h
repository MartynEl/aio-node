#ifndef CORE_FILTER_H
#define CORE_FILTER_H

#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"

/**
 * @brief Initialize a combined cascade DSP channel structure.
 * @param channel Pointer to the dsp_channel_t context block.
 * @param alpha_q14 Initial EMA smoothing factor scaled to 2^14 (0..16384).
 */
void filter_cascade_init(dsp_channel_t *channel, uint16_t alpha_q14);

/**
 * @brief Process a single raw ADC sample through the Median + EMA cascade filter.
 * MISRA C:2012 Compliance: Deterministic execution time with zero float operations.
 * 
 * @param channel Pointer to the active channel's dsp_channel_t context.
 * @param raw_sample Fresh 12-bit hardware sample directly from the ADC buffer.
 * @return adc_raw_t Clean, smoothed 12-bit result (0..4095) ready for PID input.
 */
adc_raw_t filter_cascade_update(dsp_channel_t *channel, adc_raw_t raw_sample);

/**
 * @brief Update the EMA coefficient on the fly (e.g., via Modbus command write).
 * @param channel Pointer to the targeted dsp_channel_t context.
 * @param new_alpha_q14 New alpha coefficient bounded to 0..16384 range.
 */
void filter_set_alpha(dsp_channel_t *channel, uint16_t new_alpha_q14);

#endif /* CORE_FILTER_H */
