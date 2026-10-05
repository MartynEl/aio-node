#ifndef SERVICE_MODBUS_REGS_H
#define SERVICE_MODBUS_REGS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  /* <-- In order to recognize type size_t */
#include "core/types.h"

/**
 * MISRA C:2012 Rule 10.6 Compliance: Hexadecimal base register offsets.
 * Mapped directly from the modbus_regs.md specifications.
 */
#define MB_REG_OP_START     0x0000u  /* Operational data block start address */
#define MB_REG_OP_COUNT     12u      /* Total operational holding registers (RO) */

#define MB_REG_CFG_START    0x0100u  /* Configuration block start address */
#define MB_REG_CFG_COUNT    8u       /* Total configuration holding registers (RW) */

/**
 * @brief System fault bitmask flags for SYSTEM_FAULT_MASK (Register 0x0008).
 * Enforces safe state triggers if any bit is evaluated as active.
 */
#define FAULT_BIT_ADC_DMA   (1u << 0)  /* Hardware peripheral DMA stream failure */
#define FAULT_BIT_FLASH_WEAR (1u << 1) /* Flash page layout wear-leveling failure */
#define FAULT_BIT_PVD_WARN   (1u << 2) /* Programmable Voltage Detector early drop warning */

/**
 * @brief Unified memory-mapped structure containing the Modbus Holding Registers table.
 * Divided into Read-Only Process variables and Read/Write persistent configurations.
 */
typedef struct {
    /* --------------------------------------------------------------------- */
    /* 1. Process & Diagnostics Data Block (Read-Only: Mapped to 0x0000)     */
    /* --------------------------------------------------------------------- */
    adc_raw_t adc_ch0_raw;        /* Reg 0x0000: Raw 12-bit Analog input 0 */
    adc_raw_t adc_ch0_filtered;   /* Reg 0x0001: Processed Input 0 (Median + EMA) */
    
    adc_raw_t adc_ch1_raw;        /* Reg 0x0002: Raw 12-bit Analog input 1 */
    adc_raw_t adc_ch1_filtered;   /* Reg 0x0003: Processed Input 1 (Median + EMA) */
    
    adc_raw_t adc_ch2_raw;        /* Reg 0x0004: Raw 12-bit Analog input 2 */
    adc_raw_t adc_ch2_filtered;   /* Reg 0x0005: Processed Input 2 (Median + EMA) */
    
    pwm_duty_t pwm_current_duty;  /* Reg 0x0006: Current active hardware PWM output */
    uint16_t device_state;        /* Reg 0x0007: Casted device_state_t FSM value */
    uint16_t system_fault_mask;   /* Reg 0x0008: Bitmask of active system anomalies */
    uint16_t crash_reset_count;   /* Reg 0x0009: Watchdog consecutive reset tracking */
    
    uint32_t system_uptime_ms;    /* Reg 0x000A - 0x000B: Double word atomic system timer */

    /* --------------------------------------------------------------------- */
    /* 2. Persistent Configuration Block (Read/Write: Mapped to 0x0100)       */
    /* --------------------------------------------------------------------- */
    uint16_t cfg_slave_address;   /* Reg 0x0100: Device address network ID */
    uint16_t cfg_filter_alpha;    /* Reg 0x0101: EMA filter factor scaled to Q14 */
    
    uint16_t cfg_pid_kp;          /* Reg 0x0102: Proportional gain (Q10 Fixed) */
    uint16_t cfg_pid_ki;          /* Reg 0x0103: Integral gain (Q10 Fixed) */
    uint16_t cfg_pid_kd;          /* Reg 0x0104: Derivative gain (Q10 Fixed) */
    
    adc_raw_t cfg_target_setpoint;/* Reg 0x0105: PID target value (0..4095 range) */
    uint16_t cfg_manual_pwm_mode; /* Reg 0x0106: Override automation (0=Auto, 1=Manual) */
    pwm_duty_t cfg_manual_pwm_duty;/* Reg 0x0107: Manual forced output (0..10000 range) */
} modbus_reg_map_t;

/**
 * @brief Initialize Modbus registers layout structure with default values.
 * @param regs Pointer to the uninitialized register map structure block.
 */
void mb_regs_init(modbus_reg_map_t *regs);

/**
 * @brief Read consecutive holding registers from the memory map to serialize into Modbus response buffer.
 * 
 * @param regs Valid context pointer to the global register map.
 * @param start_addr Modbus wire 0-based PDU register index.
 * @param quantity Total requested 16-bit registers (1..125 bounds validation enforced).
 * @param out_data Destination serial buffer array.
 * @param out_capacity Size memory limit of the destination buffer array to prevent stack corruptions.
 * @return true on flawless parsing, false if boundary overflow or illegal address occurs.
 */
bool mb_regs_read(
    const modbus_reg_map_t *regs,
    uint16_t start_addr,
    uint16_t quantity,
    uint8_t *out_data,
    size_t out_capacity);

/**
 * @brief Atomic write interceptor processing execution modification requests from Modbus Master.
 * 
 * @param regs Valid context pointer to the global mutable register map.
 * @param reg_addr Targeted configuration register address index.
 * @param value Incoming raw 16-bit register payload argument.
 * @param out_cfg_changed Pointer flagged as true if written payload overrides persistent configuration properties.
 * @return true if register address is valid and configuration bounds accepted, false on validation failure.
 */
bool mb_regs_write_single(
    modbus_reg_map_t *regs,
    uint16_t reg_addr,
    uint16_t value,
    bool *out_cfg_changed);

#endif /* SERVICE_MODBUS_REGS_H */
