#include "core/filter.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* For abort() */

static int32_t g_test_failures = 0;

static void check_int(const char *file, int32_t line, const char *expr, int32_t actual, int32_t expected)
{
    if (actual != expected) {
        (void)printf("FAIL %s:%d: %s (Actual: %d, Expected: %d)\n", file, line, expr, (int)actual, (int)expected);
        g_test_failures++;
    }
}

#define CHECK_EQ(actual, expected) check_int(__FILE__, __LINE__, #actual " == " #expected, (int32_t)(actual), (int32_t)(expected))
#define CHECK_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: Condition failed (%s)\n", __FILE__, __LINE__, #cond); \
        g_test_failures++; \
    } \
} while (0)

#define RUN_TEST(test_fn) do { (void)printf("RUN %s\n", #test_fn); test_fn(); } while (0)

/**
 * @brief Verify that a single high-amplitude hardware spike is completely suppressed by Median filter.
 */
static void test_median_spike_suppression(void)
{
    dsp_channel_t ch;
    (void)memset(&ch, 0, sizeof(dsp_channel_t)); /* <-- Clean steck */
    filter_cascade_init(&ch, 16384u); /* Set alpha = 1.0 (EMA transparent) to test pure median */

    /* Fill sliding window with baseline data */
    (void)filter_cascade_update(&ch, 2000u);
    (void)filter_cascade_update(&ch, 2000u);
    adc_raw_t out = filter_cascade_update(&ch, 2000u);
    CHECK_EQ(out, 2000u);

    /* Inject a massive hardware spike noise signal */
    out = filter_cascade_update(&ch, 4000u);
    CHECK_EQ(out, 2000u); /* Spike must be completely suppressed by median filter tree */

    /* Return back to baseline */
    out = filter_cascade_update(&ch, 2000u);
    CHECK_EQ(out, 2000u);
}

/**
 * @brief Verify that fixed-point EMA successfully smoothes white noise data ripples.
 * NOTE: Median filter (window=3) introduces a 1-sample delay on step changes.
 * We must account for this latency before verifying EMA convergence.
 */
static void test_ema_smoothing(void)
{
    dsp_channel_t ch;
    (void)memset(&ch, 0, sizeof(dsp_channel_t)); /* Clean stack */
    
    /* Set alpha = 4096 (4096 / 16384 = 0.25 smoothing weight) */
    filter_cascade_init(&ch, 4096u);
    
    /* Prime filter with initial value step to lock baseline */
    /* Step 1: Input 1000. Buffer becomes {1000, 1000, 1000}. Median=1000. EMA init=1000. */
    adc_raw_t out = filter_cascade_update(&ch, 1000u);
    CHECK_EQ(out, 1000u); 
    
    /* Step 2: Input 2000. 
       Buffer becomes {2000, 1000, 1000} (assuming index wraps or fills sequentially).
       Median of {2000, 1000, 1000} is STILL 1000.
       So EMA receives 1000. History is 1000. Result remains 1000.
       This verifies the MEDIAN LATENCY behavior. */
    out = filter_cascade_update(&ch, 2000u);
    CHECK_EQ(out, 1000u); // Expected due to median suppression of single spike/step start

    /* Step 3: Input 2000 again.
       Buffer becomes {2000, 2000, 1000}.
       Median of {2000, 2000, 1000} is NOW 2000.
       EMA receives 2000. History is 1000.
       Calculation: Y_new = 0.25 * 2000 + 0.75 * 1000 = 500 + 750 = 1250. */
    out = filter_cascade_update(&ch, 2000u);
    CHECK_EQ(out, 1250u); 

    /* Step 4: Input 2000 third time.
       Buffer becomes {2000, 2000, 2000}.
       Median is 2000.
       EMA receives 2000. History is 1250.
       Calculation: Y_new = 0.25 * 2000 + 0.75 * 1250 = 500 + 937.5 -> truncated to 937.
       Total: 500 + 937 = 1437. */
    out = filter_cascade_update(&ch, 2000u);
    CHECK_EQ(out, 1437u);
}

/* --- NEW TESTS FOR FILTER PHYSICS AND BOUNDARIES --- */

/**
 * @brief Test 1: Step Response to Maximum ADC Value.
 * Verifies that the cascade handles a sudden jump to saturation limit (4095).
 * Median window=3 requires 2 steps to switch output fully if starting from low baseline.
 */
static void test_step_response_max(void)
{
    dsp_channel_t ch;
    (void)memset(&ch, 0, sizeof(dsp_channel_t));
    
    /* Init with high alpha (fast response) to isolate EMA behavior after median settles */
    filter_cascade_init(&ch, 16384u); /* Transparent EMA initially for clarity? No, let's use standard 4096 */
    filter_cascade_init(&ch, 4096u);  

    /* Baseline: 1000 */
    (void)filter_cascade_update(&ch, 1000u);
    (void)filter_cascade_update(&ch, 1000u);
    adc_raw_t out = filter_cascade_update(&ch, 1000u);
    CHECK_EQ(out, 1000u); /* Stable at 1000 */

    /* STEP TO MAXIMUM: 4095 */
    
    /* Step 1: Input 4095. 
       Buffer state: {4095, 1000, 1000} (assuming index wrap or overwrite logic).
       Sorted: 1000, 1000, 4095. Median = 1000.
       EMA Calc: Y_new_Q14 = (4096 * (1000<<14) + 12288 * previous_ema_Q14) >> 14.
       Since input to EMA is still 1000 and history was 1000, result remains ~1000.
    */
    out = filter_cascade_update(&ch, 4095u);
    CHECK_EQ(out, 1000u); /* Latency check: Median suppresses step start */

    /* Step 2: Input 4095 again.
       Buffer state: {4095, 4095, 1000}.
       Sorted: 1000, 4095, 4095. Median = 4095.
       EMA Input becomes 4095. History is ~1000 (specifically 16,384,000 in Q14).
       
       Math in Q14:
       Term1 = Alpha(4096) * X_Q14(4095 * 16384 = 67,108,608) = 274,877,906,688
       Term2 = (Scale-Alpha)(12288) * Y_old_Q14(16,384,000)   = 201,326,592,000
       Sum   = 476,204,498,688
       New_State_Q14 = Sum >> 14 = 29,065,216
       Output = New_State_Q14 >> 14 = 1773 (Truncated from 1773.99...)
    */
    out = filter_cascade_update(&ch, 4095u);
    CHECK_EQ(out, 1773u); 

    /* Step 3: Input 4095 third time.
       Buffer state: {4095, 4095, 4095}.
       Sorted: 4095, 4095, 4095. Median = 4095.
       EMA Input is 4095. History is now 29,065,216 (Q14 of 1773.99...).
       
       Math in Q14:
       Term1 = 4096 * 67,108,608 = 274,877,906,688
       Term2 = 12288 * 29,065,216 = 357,153,374,208
       Sum   = 632,031,280,896
       New_State_Q14 = Sum >> 14 = 38,576,128
       Output = New_State_Q14 >> 14 = 2354 (Truncated from 2354.5)
       
       NOTE: Our earlier estimate of 2352 was incorrect due to floating-point intuition.
       Integer truncation at each stage accumulates error differently.
    */
    out = filter_cascade_update(&ch, 4095u);
    CHECK_EQ(out, 2354u); /* Corrected for integer truncation accumulation */

    /* Verify no overflow beyond 4095 in subsequent steps and check convergence */
    for (int i = 0; i < 30; ++i) { /* Increased iterations for slower convergence */
        out = filter_cascade_update(&ch, 4095u);
        if (out > 4095u) {
            printf("FAIL: Overflow detected! Out=%u\n", out);
            abort();
        }
    }
    /* Eventually should converge close to 4095 */
    CHECK_TRUE(out >= 4090u && out <= 4095u);
}

/**
 * @brief Test 2: Noise Rejection (Variance Reduction).
 * Feeds noisy signal and checks if output variance is lower than input variance.
 */
static void test_noise_rejection(void)
{
    dsp_channel_t ch;
    (void)memset(&ch, 0, sizeof(dsp_channel_t));
    filter_cascade_init(&ch, 2048u); /* Slower smoothing (alpha=0.125) */

    const uint32_t SAMPLE_COUNT = 100u;
    int32_t sum_in = 0;
    int32_t sum_out = 0;
    int64_t sq_sum_in = 0;
    int64_t sq_sum_out = 0;
    
    /* Simple deterministic pseudo-noise generator for reproducibility */
    uint32_t seed = 12345u;
    
    for (uint32_t i = 0; i < SAMPLE_COUNT; ++i) {
        /* Generate noise around 2000 +/- 50 */
        seed = seed * 1103515245u + 12345u;
        int32_t noise = ((seed >> 16) % 101) - 50; /* Range -50..+50 */
        adc_raw_t raw_val = (adc_raw_t)(2000 + noise);
        
        /* Accumulate stats for INPUT */
        sum_in += raw_val;
        sq_sum_in += (int64_t)raw_val * raw_val;

        /* Process through filter */
        adc_raw_t filtered_val = filter_cascade_update(&ch, raw_val);
        
        /* Accumulate stats for OUTPUT */
        sum_out += filtered_val;
        sq_sum_out += (int64_t)filtered_val * filtered_val;
    }

    /* Calculate Variances: Var = E[X^2] - (E[X])^2 */
    double mean_in = (double)sum_in / SAMPLE_COUNT;
    double var_in = ((double)sq_sum_in / SAMPLE_COUNT) - (mean_in * mean_in);
    
    double mean_out = (double)sum_out / SAMPLE_COUNT;
    double var_out = ((double)sq_sum_out / SAMPLE_COUNT) - (mean_out * mean_out);

    printf("Input Mean: %.2f, Variance: %.2f\n", mean_in, var_in);
    printf("Output Mean: %.2f, Variance: %.2f\n", mean_out, var_out);

    /* Assertions */
    CHECK_TRUE(mean_out >= 1950.0 && mean_out <= 2050.0); /* Mean should stay near 2000 */
    CHECK_TRUE(var_out < var_in);                         /* Output MUST be smoother (lower variance) */
}

/**
 * @brief Test 3: Alpha Boundaries.
 * Checks extreme cases where EMA becomes transparent or frozen.
 */
static void test_alpha_boundaries(void)
{
    dsp_channel_t ch;
    
    /* Case A: Alpha = 16384 (Transparent EMA) */
    (void)memset(&ch, 0, sizeof(dsp_channel_t));
    filter_cascade_init(&ch, 16384u);
    
    /* Prime median */
    (void)filter_cascade_update(&ch, 1000u);
    (void)filter_cascade_update(&ch, 1000u);
    (void)filter_cascade_update(&ch, 1000u);
    
    /* Change input. Median will lag by 1 step, but once settled, EMA must match Median exactly. */
    (void)filter_cascade_update(&ch, 2000u); /* Med=1000, Out=1000 */
    adc_raw_t out = filter_cascade_update(&ch, 2000u); /* Med=2000, Out=2000 (since alpha=1) */
    CHECK_EQ(out, 2000u);
    
    out = filter_cascade_update(&ch, 2000u); /* Med=2000, Out=2000 */
    CHECK_EQ(out, 2000u);

    /* Case B: Alpha = 0 (Frozen EMA) */
    (void)memset(&ch, 0, sizeof(dsp_channel_t));
    filter_cascade_init(&ch, 0u);
    
    /* Prime */
    (void)filter_cascade_update(&ch, 1000u); /* Init EMA to 1000 */
    
    /* Try to change input */
    (void)filter_cascade_update(&ch, 2000u); 
    (void)filter_cascade_update(&ch, 3000u);
    out = filter_cascade_update(&ch, 4000u);
    
    /* With Alpha=0, formula is: Y_new = 0*X + 1*Y_old => Y_new = Y_old.
       However, our implementation initializes previous_ema on first call.
       Subsequent calls with alpha=0 should keep returning the INITIALIZED value (1000),
       ignoring new inputs entirely. */
    CHECK_EQ(out, 1000u);
}

int main(void)
{
    RUN_TEST(test_median_spike_suppression);
    RUN_TEST(test_ema_smoothing);

    RUN_TEST(test_step_response_max);
    RUN_TEST(test_noise_rejection);
    RUN_TEST(test_alpha_boundaries);

    if (g_test_failures != 0) {
        (void)printf("🔴 FILTER TEST SUITE FAILED with %d failures\n", (int)g_test_failures);
        return 1;
    }

    (void)printf("🟢 FILTER TEST SUITE PASSED\n");
    return 0;
}
