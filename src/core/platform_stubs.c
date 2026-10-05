#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "platform/platform.h"

/* Add to src/core/platform_stubs.c to emulate hardware flash page behavior on PC host */

#define STUB_FLASH_SECTOR_SIZE 1024u
static uint8_t g_stub_flash_sector[STUB_FLASH_SECTOR_SIZE];
static bool g_stub_flash_initialized = false;

/**
 * @brief Internal helper to guarantee emulated flash page is initialized 
 * with default blank state (0xFF pattern matching raw silicon STM32).
 */
static void stub_flash_ensure_initialized(void)
{
    if (!g_stub_flash_initialized) {
        for (uint32_t i = 0u; i < STUB_FLASH_SECTOR_SIZE; ++i) {
            g_stub_flash_sector[i] = 0xFFu;
        }
        g_stub_flash_initialized = true;
    }
}

__attribute__((weak)) bool platform_flash_read_row(uint32_t offset, uint32_t *out_buf, size_t words_len)
{
    if ((out_buf == NULL) || (words_len == 0u)) {
        return false;
    }
    
    stub_flash_ensure_initialized();

    /* Verify if requested read transaction stays strictly within sector bounds boundary limits */
    size_t byte_len = words_len * 4u;
    if ((offset + byte_len) > STUB_FLASH_SECTOR_SIZE) {
        return false;
    }

    /* Copy raw binary data from virtual flash sector array into output block pointer buffer */
    uint8_t *dst = (uint8_t *)out_buf;
    for (size_t i = 0u; i < byte_len; ++i) {
        dst[i] = g_stub_flash_sector[offset + i];
    }

    return true;
}

__attribute__((weak)) bool platform_flash_write_row(uint32_t offset, const uint32_t *src_buf, size_t words_len)
{
    if ((src_buf == NULL) || (words_len == 0u)) {
        return false;
    }
    
    stub_flash_ensure_initialized();

    size_t byte_len = words_len * 4u;
    if ((offset + byte_len) > STUB_FLASH_SECTOR_SIZE) {
        return false;
    }

    /* 
     * Real Hardware Simulation Note: 
     * In real physical Flash silicon, writing can only flip bits from 1 to 0.
     * To simulate this rule, we execute an AND operation with existing cells.
     */
    const uint8_t *src = (const uint8_t *)src_buf;
    for (size_t i = 0u; i < byte_len; ++i) {
        g_stub_flash_sector[offset + i] &= src[i];
    }

    return true;
}

__attribute__((weak)) bool platform_flash_sector_erase(void)
{
    /* Reset entire emulated memory chunk matrix back to clean 0xFF space state */
    for (uint32_t i = 0u; i < STUB_FLASH_SECTOR_SIZE; ++i) {
        g_stub_flash_sector[i] = 0xFFu;
    }
    g_stub_flash_initialized = true;
    return true;
}
