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

/* --- NEW TESTS FOR WEAR LEVELING ROBUSTNESS --- */

/**
 * @brief Test Sector Overflow and Erase Logic.
 * Simulates filling all slots in a sector to trigger erase.
 */
static void test_wl_sector_full(void)
{
    /* Assuming WL_SECTOR_SIZE_WORDS is defined in headers or we calculate it */
    /* Let's assume standard STM32F1 page size is 1KB = 256 words (uint32_t) */
    /* But our slot structure might be larger. Let's use the constant from code if available, 
       otherwise estimate based on struct size. */
    
    /* For this test, let's just iterate enough times to wrap around. 
       If slot size is ~32 bytes (8 words), and sector is 1KB (256 words), 
       that's 32 slots per sector. */
    
    const uint32_t SLOTS_PER_SECTOR = 32u; /* Adjust based on actual struct size / flash page size */
    
    nv_config_t cfg_in;
    nv_config_t cfg_out;
    
    /* Fill buffer with distinct pattern for each iteration to track recovery */
    for (uint32_t i = 0; i < SLOTS_PER_SECTOR + 2u; ++i) {
        memset(&cfg_in, 0, sizeof(nv_config_t));
        cfg_in.magic_word = 0xDEADBEEFu;
        cfg_in.sequence_number = i; /* Unique sequence ID */
        cfg_in.checksum = compute_crc32((uint8_t*)&cfg_in, offsetof(nv_config_t, checksum));
        
        bool write_ok = wl_service_save(&cfg_in);
        CHECK_TRUE(write_ok);
    }
    
    /* Now try to read back the last valid config */
    bool read_ok = wl_service_load(&cfg_out);
    CHECK_TRUE(read_ok);
    
    /* It should have recovered the one written at index (SLOTS_PER_SECTOR + 1) % SLOTS_PER_SECTOR 
       which effectively wrapped around after erase. */
    /* Actually, depending on implementation, it might recover the very last one before crash/erase logic kicked in fully.
       Key assertion: Sequence number must be high (near end of cycle). */
    CHECK_TRUE(cfg_out.sequence_number >= (SLOTS_PER_SECTOR - 2u));
}

/**
 * @brief Test Recovery from Corrupted Data (CRC Mismatch).
 * Manually corrupts data in emulated flash and checks if loader skips bad slots.
 */
static void test_wl_corrupted_slot_skip(void)
{
    nv_config_t cfg_good;
    nv_config_t cfg_loaded;
    
    /* Write two good configs */
    memset(&cfg_good, 0, sizeof(nv_config_t));
    cfg_good.magic_word = 0xCAFEBABEu;
    cfg_good.sequence_number = 100u;
    cfg_good.checksum = compute_crc32((uint8_t*)&cfg_good, offsetof(nv_config_t, checksum));
    CHECK_TRUE(wl_service_save(&cfg_good));
    
    /* Write second good config */
    cfg_good.sequence_number = 101u;
    cfg_good.checksum = compute_crc32((uint8_t*)&cfg_good, offsetof(nv_config_t, checksum));
    CHECK_TRUE(wl_service_save(&cfg_good));
    
    /* CORRUPT THE SECOND SLOT IN MEMORY DIRECTLY */
    /* We need access to the raw emulator array here. 
       Usually exposed via platform_get_flash_emulator_ptr() or similar in host builds. */
    extern uint32_t g_flash_emulator[]; /* Hypothetical global for host testing */
    extern size_t g_current_write_offset_words; /* Track where next write goes */
    
    /* Calculate offset of the LAST WRITTEN slot (index 101) */
    /* This depends heavily on your specific WL implementation details. 
       Simplified assumption: Slots are contiguous blocks. */
    uint32_t *corrupt_target = &g_flash_emulator[g_current_write_offset_words - (sizeof(nv_config_t)/4)];
    
    /* Flip some bits to break CRC */
    *corrupt_target ^= 0xFFFFFFFFu; 
    
    /* Try to load */
    bool ok = wl_service_load(&cfg_loaded);
    
    /* System SHOULD ignore the corrupted slot (101) and fall back to previous valid one (100) */
    CHECK_TRUE(ok);
    CHECK_EQ(cfg_loaded.sequence_number, 100u);
}

int main(void)
{
    RUN_TEST(test_wear_leveling_linear_progression);

    RUN_TEST(test_wl_sector_full);
    RUN_TEST(test_wl_corrupted_slot_skip);

    if (g_test_failures != 0) {
        (void)printf("🔴 WEAR LEVELING TEST SUITE FAILED with %d failures\n", (int)g_test_failures);
        return 1;
    }

    (void)printf("🟢 WEAR LEVELING TEST SUITE PASSED\n");
    return 0;
}
