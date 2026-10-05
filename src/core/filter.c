#include "core/filter.h"
#include <stddef.h>

void filter_cascade_init(dsp_channel_t *channel, uint16_t alpha_q14)
{
    if (channel == NULL) {
        return;
    }

    channel->median.buffer_index = 0u;
    channel->median.is_initialized = false; /* <-- Сброс флага прогрева медианы */
    for (uint32_t i = 0u; i < 3u; ++i) {
        channel->median.sample_buffer[i] = 0u;
    }

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
    /* Stage 1: Median Filter (with Instant Priming/Warmup Policy)           */
    /* --------------------------------------------------------------------- */
    if (!channel->median.is_initialized) {
        /* При первом пуске заполняем все окна текущим значением, исключая ложные нули */
        channel->median.sample_buffer[0u] = raw_sample;
        channel->median.sample_buffer[1u] = raw_sample;
        channel->median.sample_buffer[2u] = raw_sample;
        channel->median.buffer_index = 0u;
        channel->median.is_initialized = true;
    } else {
        /* В штатном режиме пишем по круговому индексу */
        uint8_t idx = channel->median.buffer_index;
        channel->median.sample_buffer[idx] = raw_sample;
        
        idx++;
        if (idx >= 3u) {
            idx = 0u;
        }
        channel->median.buffer_index = idx;
    }

    /* Вычисляем медиану по честным ячейкам массива */
    adc_raw_t med_out = calc_median_of_3(
        channel->median.sample_buffer[0u],
        channel->median.sample_buffer[1u],
        channel->median.sample_buffer[2u]
    );

    /* --------------------------------------------------------------------- */
    /* Stage 2: Fixed-Point EMA Filter                                      */
    /* --------------------------------------------------------------------- */
    if (!channel->ema.is_initialized) {
        /* Теперь med_out гарантированно равен первому замеру (например, 2000), а не 0! */
        channel->ema.previous_ema = (uint32_t)med_out << 14u; 
        channel->ema.is_initialized = true;
        return med_out;
    }

    uint32_t alpha = (uint32_t)channel->ema.alpha_q14;
    uint32_t current_ema = (alpha * ((uint32_t)med_out << 14u)) + 
                           ((16384u - alpha) * (channel->ema.previous_ema >> 14u));
    
    channel->ema.previous_ema = current_ema;

    return (adc_raw_t)(current_ema >> 14u);
}
