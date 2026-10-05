#include "core/pid.h"
#include <stdio.h>

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
 * @brief Verify that PID controller converges output towards target setpoint error.
 */
static void test_pid_error_convergence(void)
{
    pid_controller_t pid;
    /* Kp = 1.0 (1024), Ki = 0.0, Kd = 0.0 */
    pid_init(&pid, 1024, 0, 0);
    pid_set_setpoint(&pid, 2000);

    /* Feedback is below target (1000u), error = 1000, output should scale power up */
    pwm_duty_t duty = pid_update(&pid, 1000u);
    CHECK_EQ(duty, 10000u); /* Clamped to max full load ceiling capacity */

    /* Feedback is at target, error = 0, proportional term drops */
    pid_init(&pid, 1024, 0, 0);
    pid_set_setpoint(&pid, 2000);
    duty = pid_update(&pid, 2000u);
    CHECK_EQ(duty, 0u);
}

/**
 * @brief Verify that hard anti-windup clamping shields accumulator from overflows.
 */
static void test_pid_anti_windup(void)
{
    pid_controller_t pid;
    /* Kp = 0.0, Ki = 1000, Kd = 0.0 */
    pid_init(&pid, 0, 1000, 0);
    pid_set_setpoint(&pid, 3000);

    /* Continuously update with large error to force massive integration accumulator drift */
    for (uint32_t i = 0u; i < 50u; ++i) {
        (void)pid_update(&pid, 1000u);
    }

    /* Accumulator must lock cleanly inside design constraints bounds, protecting limits */
    CHECK_EQ(pid.integral_accum, 10240000L);
}

int main(void)
{
    RUN_TEST(test_pid_error_convergence);
    RUN_TEST(test_pid_anti_windup);

    if (g_test_failures != 0) {
        (void)printf("🔴 PID TEST SUITE FAILED with %d failures\n", (int)g_test_failures);
        return 1;
    }

    (void)printf("🟢 PID TEST SUITE PASSED\n");
    return 0;
}
