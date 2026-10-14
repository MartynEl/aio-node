#include "service/wear_leveling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* For memset */
#include <stdint.h>
#include <stdbool.h>
#include "service/wear_leveling.h"
#include "service/modbus_regs.h"

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

extern bool platform_flash_sector_erase(void);

/**
 * @brief Verify that incremental saves smoothly shift positions inside memory sector 
 * and correctly recover the highest tracking version version snapshot sequence.
 */
static void test_wear_leveling_linear_progression(void)
{
    modbus_reg_map_t regs;
    
    (void)platform_flash_sector_erase();

    /* 1. First init on raw chip: must report false (no valid sectors found) */
    bool init_ok = wl_service_init(&regs);
    CHECK_EQ(init_ok, false); 
    
    /* Force factory configuration variables */
    mb_regs_init(&regs);
    CHECK_EQ(regs.cfg_slave_address, 1u);

    /* 2. Modify and trigger incremental saves tracking snapshot progression */
    regs.cfg_slave_address = 42u;
    bool save_ok = wl_service_save(&regs);
    CHECK_EQ(save_ok, true);

    regs.cfg_slave_address = 99u;
    save_ok = wl_service_save(&regs);
    CHECK_EQ(save_ok, true);

    /* 3. Re-boot simulation: flash now CONTAINS valid data, must report true! */
    modbus_reg_map_t fresh_boot_regs;
    init_ok = wl_service_init(&fresh_boot_regs);
    CHECK_EQ(init_ok, true);
    
    /* Must seamlessly recover the highest tracking version (99u) */
    CHECK_EQ(fresh_boot_regs.cfg_slave_address, 99u);
}

/**
 * @brief Test Sector Overflow and Erase Logic via Public API.
 * 
 * Strategy: Write more configurations than fit in one Flash page (approx 32 slots).
 * Verify that after reboot (clear RAM + init), the system recovers the LAST valid config.
 */
static void test_wl_sector_overflow(void)
{
    modbus_reg_map_t regs;
    bool ok;
    
    /* STM32F103 Page Size is typically 1KB. nv_config_t is 32 bytes. 
     * Capacity ~= 1024 / 32 = 32 slots. 
     * We write 40 times to force at least one erase cycle. */
    const uint32_t ITERATIONS = 40u; 

    printf("Starting WL Sector Overflow Test (Iterating %u times)...\n", ITERATIONS);

    /* Phase 1: Stress Write */
    for (uint32_t i = 0; i < ITERATIONS; ++i) {
        memset(&regs, 0, sizeof(regs));
        
        /* Encode iteration number into unique fields to track recovery later */
        /* USING REAL FIELD NAMES FROM modbus_regs.h */
        regs.cfg_slave_address = (uint16_t)(100 + i);
        regs.cfg_target_setpoint = (uint16_t)(2000 + i);
        regs.cfg_filter_alpha = 4096u; /* Standard Q14 alpha */
        regs.cfg_pid_kp = 1024u;       /* Kp = 1.0 in Q10 */
        regs.cfg_pid_ki = 512u;        /* Ki = 0.5 in Q10 */
        regs.cfg_pid_kd = 0u;
        regs.cfg_manual_pwm_mode = 0u;
        regs.cfg_manual_pwm_duty = 0u;

        ok = wl_service_save(&regs);
        if (!ok) {
            printf("CRITICAL FAIL: wl_service_save returned false at iter %u\n", i);
            CHECK_TRUE(false);
            return;
        }
    }

    /* Phase 2: Simulate Reboot */
    /* Clear RAM context completely to ensure we don't read stale data */
    memset(&regs, 0, sizeof(regs));
    
    /* Attempt Recovery from Flash */
    ok = wl_service_init(&regs);
    CHECK_TRUE(ok);
    
    /* Verification: Should have recovered the VERY LAST written configuration */
    uint32_t last_iter = ITERATIONS - 1;
    uint16_t expected_id = (uint16_t)(100 + last_iter);
    uint16_t expected_sp = (uint16_t)(2000 + last_iter);
    
    printf("Recovered Config: SlaveAddr=%u, Setpoint=%u\n", 
           regs.cfg_slave_address, regs.cfg_target_setpoint);
           
    CHECK_EQ(regs.cfg_slave_address, expected_id);
    CHECK_EQ(regs.cfg_target_setpoint, expected_sp);
}

/**
 * @brief Test Basic Integrity and Sequential Consistency.
 * Verifies that sequential saves/loads maintain consistency without crashing.
 */
static void test_wl_integrity_check(void)
{
    modbus_reg_map_t regs;
    bool ok;

    printf("Starting WL Integrity Check Test...\n");

    /* Step 1: Write Config A */
    memset(&regs, 0, sizeof(regs));
    regs.cfg_slave_address = 10u;
    regs.cfg_target_setpoint = 500u;
    regs.cfg_filter_alpha = 2048u;
    regs.cfg_pid_kp = 1000u;
    regs.cfg_pid_ki = 100u;
    regs.cfg_pid_kd = 0u;
    regs.cfg_manual_pwm_mode = 0u;
    regs.cfg_manual_pwm_duty = 0u;
    
    ok = wl_service_save(&regs);
    CHECK_TRUE(ok);

    /* Step 2: Write Config B (Newer) */
    memset(&regs, 0, sizeof(regs));
    regs.cfg_slave_address = 20u;
    regs.cfg_target_setpoint = 1500u;
    regs.cfg_filter_alpha = 4096u;
    regs.cfg_pid_kp = 2000u;
    regs.cfg_pid_ki = 200u;
    regs.cfg_pid_kd = 0u;
    regs.cfg_manual_pwm_mode = 0u;
    regs.cfg_manual_pwm_duty = 0u;
    
    ok = wl_service_save(&regs);
    CHECK_TRUE(ok);

    /* Step 3: Reload from Flash */
    memset(&regs, 0, sizeof(regs));
    ok = wl_service_init(&regs);
    CHECK_TRUE(ok);

    /* Expectation: Must recover Config B (the newest valid one) */
    CHECK_EQ(regs.cfg_slave_address, 20u);
    CHECK_EQ(regs.cfg_target_setpoint, 1500u);
    
    printf("PASS: Basic Integrity Check OK.\n");
}

int main(void)
{
    RUN_TEST(test_wear_leveling_linear_progression);

    RUN_TEST(test_wl_sector_overflow);
    RUN_TEST(test_wl_integrity_check);

    if (g_test_failures != 0) {
        (void)printf("🔴 WEAR LEVELING TEST SUITE FAILED with %d failures\n", (int)g_test_failures);
        return 1;
    }

    (void)printf("🟢 WEAR LEVELING TEST SUITE PASSED\n");
    return 0;
}
