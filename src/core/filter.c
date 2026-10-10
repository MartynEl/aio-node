#include "core/filter.h"
#include <stddef.h>

void filter_cascade_init(dsp_channel_t *channel, uint16_t alpha_q14)
{
    if (channel == NULL) {
        return;
    }
    channel->median.buffer_index = 0u;
    channel->median.is_initialized = false;
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
    adc_raw_t middle = a; // Safe init
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
    /* Stage 1: Median Filter with Instant Priming                           */
    /* --------------------------------------------------------------------- */
    if (!channel->median.is_initialized) {
        channel->median.sample_buffer[0u] = raw_sample;
        channel->median.sample_buffer[1u] = raw_sample;
        channel->median.sample_buffer[2u] = raw_sample;
        channel->median.buffer_index = 0u;
        channel->median.is_initialized = true;
    } else {
        uint8_t idx = channel->median.buffer_index;
        channel->median.sample_buffer[idx] = raw_sample;
        idx++;
        if (idx >= 3u) {
            idx = 0u;
        }
        channel->median.buffer_index = idx;
    }

    adc_raw_t med_out = calc_median_of_3(
        channel->median.sample_buffer[0u],
        channel->median.sample_buffer[1u],
        channel->median.sample_buffer[2u]
    );

    /* --------------------------------------------------------------------- */
    /* Stage 2: Fixed-Point EMA Filter (Q14) using 64-bit intermediates      */
    /* --------------------------------------------------------------------- */
    
    uint32_t alpha = (uint32_t)channel->ema.alpha_q14;
    
    // Масштабируем вход до Q14: Value * 16384
    uint32_t input_q14 = (uint32_t)med_out << 14u;

    if (!channel->ema.is_initialized) {
        channel->ema.previous_ema = input_q14;
        channel->ema.is_initialized = true;
        return med_out;
    }

    /*
     * ФОРМУЛА: Y_new_Q14 = (Alpha_Q14 * X_in_Q14 + (Scale - Alpha_Q14) * Y_old_Q14) / Scale
     * ВАЖНО: Используем uint64_t, так как произведение двух 32-битных чисел
     * может превысить диапазон uint32_t (макс ~1.1e12 против лимита 4.2e9).
     */
    uint64_t term1 = (uint64_t)alpha * (uint64_t)input_q14;
    uint64_t term2 = (uint64_t)(16384u - alpha) * (uint64_t)channel->ema.previous_ema;
    
    uint64_t sum = term1 + term2;
    
    // Делим на масштаб 2^14, получая результат обратно в формате Q14
    uint32_t new_state_q14 = (uint32_t)(sum >> 14u);
    
    // Сохраняем состояние уже в формате Q14 для следующего шага
    channel->ema.previous_ema = new_state_q14;
    
    // Возвращаем результат в исходном диапазоне ADC (0..4095), деля еще раз на 2^14
    return (adc_raw_t)(new_state_q14 >> 14u);
}