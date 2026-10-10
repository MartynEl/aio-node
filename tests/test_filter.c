#include "core/filter.h"
#include <stdio.h>
#include <string.h>

static int32_t g_test_failures = 0;

static void check_int(const char *file, int32_t line, const char *expr, int32_t actual, int32_t expected)
{
    if (actual != expected) {
        (void)printf("FAIL %s:%d: %s (Actual: %d, Expected: %d)\n", file, line, expr, (int)actual, (int)expected);
        g_test_failures++;
    }
}

#define CHECK_EQ(actual, expected) check_int(__FILE__, __LINE__, #actual " == " #expected, (int32_t)(actual), (int32_t)(expected))
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

int main(void)
{
    RUN_TEST(test_median_spike_suppression);
    RUN_TEST(test_ema_smoothing);

    if (g_test_failures != 0) {
        (void)printf("🔴 FILTER TEST SUITE FAILED with %d failures\n", (int)g_test_failures);
        return 1;
    }

    (void)printf("🟢 FILTER TEST SUITE PASSED\n");
    return 0;
}
