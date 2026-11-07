#include "service/wear_leveling.h"
#include <stddef.h>
#include <string.h>

/* Define flash sector emulated limits: 1024 bytes allocation sector */
#define WL_FLASH_SECTOR_SIZE   1024u
#define WL_CONFIG_SIZE         sizeof(nv_config_t)
#define WL_MAX_SLOTS_COUNT     (WL_FLASH_SECTOR_SIZE / WL_CONFIG_SIZE)
/* MISRA Compliance: Valid hexadecimal constant literal descriptor */
#define WL_MAGIC_ID            0xA100B0DEu

/* Link low-level platform flash driver contracts from hardware abstraction layer */
extern bool platform_flash_read_row(uint32_t offset, uint32_t *out_buf, size_t words_len);
extern bool platform_flash_write_row(uint32_t offset, const uint32_t *src_buf, size_t words_len);
extern bool platform_flash_sector_erase(void);

/**
 * @brief Standard Software CRC32 implementation (Ethernet polynomial).
 */
static uint32_t wl_calc_crc32(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0u; i < len; ++i) {
        crc ^= bytes[i];
        for (uint8_t j = 0u; j < 8u; ++j) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1u) ^ 0xEDB88320UL;
            } else {
                crc >>= 1u;
            }
        }
    }
    return ~crc;
}

static bool wl_validate_config(const nv_config_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    if (cfg->magic != WL_MAGIC_ID) {
        return false;
    }
    /* Calculate CRC excluding the last 4 bytes of crc32 field itself */
    size_t payload_len = offsetof(nv_config_t, crc32);
    uint32_t expected_crc = wl_calc_crc32(cfg, payload_len);
    return (cfg->crc32 == expected_crc);
}

bool wl_service_init(modbus_reg_map_t *regs)
{
    if (regs == NULL) {
        return false;
    }
    nv_config_t newest_config;
    uint32_t max_seq = 0u;
    bool found_any_valid = false;

    /* Scan all incremental slot positions inside the allocated flash sector */
    for (uint32_t slot = 0u; slot < WL_MAX_SLOTS_COUNT; ++slot) {
        nv_config_t temp_cfg;
        uint32_t offset = slot * WL_CONFIG_SIZE;
        
        /* MISRA Compliance: Use a naturally aligned 32-bit array buffer */
        uint32_t aligned_word_buf[(sizeof(nv_config_t) + 3u) / 4u];
        size_t words_len = sizeof(nv_config_t) / 4u;

        bool read_ok = platform_flash_read_row(offset, aligned_word_buf, words_len);
        if (read_ok) {
            (void)memcpy(&temp_cfg, aligned_word_buf, sizeof(nv_config_t));
            if (wl_validate_config(&temp_cfg)) {
                if (!found_any_valid || (temp_cfg.sequence_number > max_seq)) {
                    max_seq = temp_cfg.sequence_number;
                    newest_config = temp_cfg;
                    found_any_valid = true;
                }
            }
        }
    }

    if (found_any_valid) {
        /* Unpack extracted variables into live dynamic runtime Modbus memory map */
        regs->cfg_slave_address = newest_config.slave_address;
        regs->cfg_filter_alpha = newest_config.filter_alpha;
        regs->cfg_pid_kp = newest_config.pid_kp;
        regs->cfg_pid_ki = newest_config.pid_ki;
        regs->cfg_pid_kd = newest_config.pid_kd;
        regs->cfg_target_setpoint = newest_config.target_setpoint;
        regs->cfg_manual_pwm_mode = newest_config.manual_pwm_mode;
        regs->cfg_manual_pwm_duty = newest_config.manual_pwm_duty;
        return true; /* Successfully recover settings */
    } else {
        return false;
    }
}

bool wl_service_save(const modbus_reg_map_t *regs)
{
    if (regs == NULL) {
        return false;
    }
    uint32_t target_slot = 0u;
    uint32_t max_seq = 0u;
    bool slot_alloc_map[WL_MAX_SLOTS_COUNT];
    
    /* Pre-fill allocation state map */
    (void)memset(slot_alloc_map, 0, sizeof(slot_alloc_map));

    /* Step 1: Scan sector profile to locate active versions and identify the first raw empty slot */
    for (uint32_t slot = 0u; slot < WL_MAX_SLOTS_COUNT; ++slot) {
        nv_config_t temp_cfg;
        uint32_t offset = slot * WL_CONFIG_SIZE;
        uint32_t aligned_word_buf[(sizeof(nv_config_t) + 3u) / 4u];
        size_t words_len = sizeof(nv_config_t) / 4u;
        
        bool read_ok = platform_flash_read_row(offset, aligned_word_buf, words_len);
        if (read_ok) {
            (void)memcpy(&temp_cfg, aligned_word_buf, sizeof(nv_config_t));
            /* Check if slot is cleanly erased (STM32 erased flash reads as 0xFFFFFFFF) */
            if (temp_cfg.magic != WL_MAGIC_ID) {
                slot_alloc_map[slot] = true; /* Verified as raw empty and write-ready */
            } else if (wl_validate_config(&temp_cfg)) {
                if (temp_cfg.sequence_number > max_seq) {
                    max_seq = temp_cfg.sequence_number;
                }
            } else {
                /* Structural mismatch or corrupted slot cell, mark as unsafe for writing */
                slot_alloc_map[slot] = false;
            }
        }
    }

    /* Locate the first available empty slot entry */
    bool found_empty_slot = false;
    for (uint32_t slot = 0u; slot < WL_MAX_SLOTS_COUNT; ++slot) {
        if (slot_alloc_map[slot]) {
            target_slot = slot;
            found_empty_slot = true;
            break;
        }
    }

    /* Step 2: Handle Page Cycle rotation. If sector layout is completely full, trigger sector erase */
    if (!found_empty_slot) {
        if (!platform_flash_sector_erase()) {
            return false; /* Hardware Flash page controller error protection */
        }
        target_slot = 0u; /* Loop resets back to start position coordinate */
    }

    /* Step 3: Pack mutable runtime data into the non-volatile struct container */
    nv_config_t outbound_cfg;
    outbound_cfg.magic = WL_MAGIC_ID;
    outbound_cfg.sequence_number = max_seq + 1u; /* Increment software version counter */
    outbound_cfg.slave_address = regs->cfg_slave_address;
    outbound_cfg.filter_alpha = regs->cfg_filter_alpha;
    outbound_cfg.pid_kp = regs->cfg_pid_kp;
    outbound_cfg.pid_ki = regs->cfg_pid_ki;
    outbound_cfg.pid_kd = regs->cfg_pid_kd;
    outbound_cfg.target_setpoint = regs->cfg_target_setpoint;
    outbound_cfg.manual_pwm_mode = regs->cfg_manual_pwm_mode;
    outbound_cfg.manual_pwm_duty = regs->cfg_manual_pwm_duty;
    
    /* IMPORTANT: Fill padding with zeros or specific pattern to ensure deterministic CRC */
    outbound_cfg.align_pad = 0x0000u; 

    /* Finalize payload data calculation with CRC32 signature */
    size_t payload_len = offsetof(nv_config_t, crc32);
    outbound_cfg.crc32 = wl_calc_crc32(&outbound_cfg, payload_len);

    /* Step 4: Stream compiled block via safely aligned local intermediate memory buffer row */
    uint32_t target_offset = target_slot * WL_CONFIG_SIZE;
    uint32_t aligned_write_buf[(sizeof(nv_config_t) + 3u) / 4u];
    size_t write_words_len = sizeof(nv_config_t) / 4u;

    (void)memcpy(aligned_write_buf, &outbound_cfg, sizeof(nv_config_t));
    return platform_flash_write_row(target_offset, aligned_write_buf, write_words_len);
}