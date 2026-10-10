#ifndef SERVICE_WEAR_LEVELING_H
#define SERVICE_WEAR_LEVELING_H

#include <stdint.h>
#include <stdbool.h>
#include "service/modbus_regs.h"

/**
 * @brief Persistent configuration structure synced with Modbus configuration block.
 * Packed and aligned to 4 bytes for efficient flash row streaming.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;               /* Validation tag identifier: 0xA100N0DEu */
    uint32_t sequence_number;     /* Incremental version counter for Wear-Leveling tracking */
    
    uint16_t slave_address;       /* Network Node ID */
    uint16_t filter_alpha;        /* EMA factor Q14 */
    
    uint16_t pid_kp;              /* PID terms scaled in Q10 */
    uint16_t pid_ki;
    uint16_t pid_kd;
    
    uint16_t target_setpoint;     /* Targeted goal (0..4095) */
    uint16_t manual_pwm_mode;     /* Loop control override flag */
    uint16_t manual_pwm_duty;     /* Forced manual duty payload */
    
    uint32_t crc32;               /* Structural payload integrity verification checksum */
    uint16_t align_pad;           /* 2 bytes -> Total 32 bytes */
} nv_config_t;

/**
 * @brief Initialize the Wear Leveling service. 
 * Scans the flash memory sector, validates layouts, and extracts the newest config version.
 * 
 * @param regs Pointer to the global mutable modbus registers map to populate with recovered data.
 * @return true if valid configuration found or defaults loaded successfully, false on critical HW fault.
 */
bool wl_service_init(modbus_reg_map_t *regs);

/**
 * @brief Synchronize and save current Modbus configuration state to the non-volatile storage.
 * Evaluates sector space, increments sequence, handles page erase triggers if sector is full.
 * 
 * @param regs Const pointer to the current application runtime register map state.
 * @return true if flash streaming operation reports complete, false on write/erase errors.
 */
bool wl_service_save(const modbus_reg_map_t *regs);

#endif /* SERVICE_WEAR_LEVELING_H */
