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
 */
static void test_ema_smoothing(void)
{
    dsp_channel_t ch;
    (void)memset(&ch, 0, sizeof(dsp_channel_t)); /* <-- Clean steck */
    /* Set alpha = 4096 (4096 / 16384 = 0.25 smoothing weight) */
    filter_cascade_init(&ch, 4096u);

    /* Prime filter with initial value step to lock baseline */
    (void)filter_cascade_update(&ch, 1000u);

    /* Input steps to 2000u, verify fractional convergence exponential steps */
    adc_raw_t out = filter_cascade_update(&ch, 2000u);
    /* Out = 0.25 * 2000 + 0.75 * 1000 = 500 + 750 = 1250 */
    CHECK_EQ(out, 1250u);

    out = filter_cascade_update(&ch, 2000u);
    /* Out = 0.25 * 2000 + 0.75 * 1250 = 500 + 937 = 1437 */
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
