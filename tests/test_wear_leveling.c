#include "service/wear_leveling.h"
#include "service/modbus_regs.h"
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

int main(void)
{
    RUN_TEST(test_wear_leveling_linear_progression);

    if (g_test_failures != 0) {
        (void)printf("🔴 WEAR LEVELING TEST SUITE FAILED with %d failures\n", (int)g_test_failures);
        return 1;
    }

    (void)printf("🟢 WEAR LEVELING TEST SUITE PASSED\n");
    return 0;
}
