#include "core/filter.h"
#include <stddef.h>

void filter_cascade_init(dsp_channel_t *channel, uint16_t alpha_q14)
{
    if (channel == NULL) {
        return;
    }

    /* Initialize Median sliding window buffer */
    channel->median.buffer_index = 0u;
    for (uint32_t i = 0u; i < 3u; ++i) {
        channel->median.sample_buffer[i] = 0u;
    }

    /* Initialize Exponential Moving Average context */
    channel->ema.previous_ema = 0u;
    channel->ema.alpha_q14 = (alpha_q14 > 16384u) ? 16384u : alpha_q14;
    channel->ema.is_initialized = false;
}

void filter_set_alpha(dsp_channel_t *channel, uint16_t new_alpha_q14)
{
    if (channel != NULL) {
        channel->ema.alpha_q14 = (new_alpha_q14 > 16384u) ? 16384u : new_alpha_q14;
    }
}

/**
 * @brief Fast deterministic 3-point Median calculation via branch trees.
 * MISRA C:2012 Rule 15.7 compliant (all branches terminated).
 */
static adc_raw_t calc_median_of_3(adc_raw_t a, adc_raw_t b, adc_raw_t c)
{
    adc_raw_t middle;

    if (a > b) {
        if (b > c) {
            middle = b;
        } else if (a > c) {
            middle = c;
        } else {
            middle = a;
        }
    } else {
        if (a > c) {
            middle = a;
        } else if (b > c) {
            middle = c;
        } else {
            middle = b;
        }
    }

    return middle;
}

adc_raw_t filter_cascade_update(dsp_channel_t *channel, adc_raw_t raw_sample)
{
    if (channel == NULL) {
        return 0u;
    }

    /* --------------------------------------------------------------------- */
    /* Stage 1: Median Filter (Impulsive Spike Suppression)                  */
    /* --------------------------------------------------------------------- */
    uint8_t idx = channel->median.buffer_index;
    channel->median.sample_buffer[idx] = raw_sample;
    
    /* Advance circular index counter */
    idx++;
    if (idx >= 3u) {
        idx = 0u;
    }
    channel->median.buffer_index = idx;

    /* Extract median output window values safely */
    adc_raw_t med_out = calc_median_of_3(
        channel->median.sample_buffer[0u],
        channel->median.sample_buffer[1u],
        channel->median.sample_buffer[2u]
    );

    /* --------------------------------------------------------------------- */
    /* Stage 2: Fixed-Point EMA Filter (High-Frequency White Noise Smoothing)*/
    /* --------------------------------------------------------------------- */
    if (!channel->ema.is_initialized) {
        /* Avoid startup ramp lag by locking directly to the first valid median output */
        channel->ema.previous_ema = (uint32_t)med_out << 14u; /* Scale to Q14 space */
        channel->ema.is_initialized = true;
        return med_out;
    }

    uint32_t alpha = (uint32_t)channel->ema.alpha_q14;
    uint32_t input_scaled = (uint32_t)med_out << 14u;
    
    /* EMA Fixed-point standard formula: Out = Alpha*In + (1 - Alpha)*Prev */
    uint32_t current_ema = (alpha * input_scaled) + ((16384u - alpha) * (channel->ema.previous_ema >> 14u));
    channel->ema.previous_ema = current_ema;

    /* Scaled space back to standard 12-bit ADC coordinates space (0..4095) */
    return (adc_raw_t)(current_ema >> 14u);
}
