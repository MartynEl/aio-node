#include "service/modbus_regs.h"
#include <string.h>

/**
 * @brief Initialize Modbus registers layout structure with default values.
 * Strictly adheres to default values specified in documentation.
 */
void mb_regs_init(modbus_reg_map_t *regs)
{
    if (regs == NULL) {
        return;
    }

    /* Clear entire operational block memory layout safely */
    (void)memset(regs, 0, sizeof(modbus_reg_map_t));

    /* Load default configuration block constraints */
    regs->cfg_slave_address = 1u;
    regs->cfg_filter_alpha = 2000u;  /* Default EMA factor ≈ 0.12 */
    regs->cfg_pid_kp = 1000u;        /* Default Kp = 1.0 */
    regs->cfg_pid_ki = 100u;         /* Default Ki = 0.1 */
    regs->cfg_pid_kd = 50u;          /* Default Kd = 0.05 */
    regs->cfg_target_setpoint = 2048u; /* Center of 12-bit ADC range */
    regs->cfg_manual_pwm_mode = 0u;   /* Automatic PID mode enabled */
    regs->cfg_manual_pwm_duty = 0u;   /* Zero fixed load duty */
    
    regs->device_state = (uint16_t)DEVICE_STATE_INIT;
}

/**
 * @brief Helper utility pack vector array with Big-Endian 16-bit payload.
 */
static void put_be16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)((val >> 8u) & 0xFFu);
    buf[1] = (uint8_t)(val & 0xFFu);
}

/**
 * @brief Read consecutive holding registers from the memory map.
 * MISRA C:2012 Compliance: Buffer boundary and pointer verification.
 */
bool mb_regs_read(
    const modbus_reg_map_t *regs,
    uint16_t start_addr,
    uint16_t quantity,
    uint8_t *out_data,
    size_t out_capacity)
{
    if ((regs == NULL) || (out_data == NULL)) {
        return false;
    }

    if ((quantity == 0u) || (quantity > 125u)) {
        return false;
    }

    /* Verify destination memory limits to block buffer overflow vulnerabilities */
    size_t needed_bytes = (size_t)quantity * 2u;
    if (out_capacity < needed_bytes) {
        return false;
    }

    /* Linearly parse requested wire register addresses */
    for (uint16_t i = 0u; i < quantity; ++i) {
        uint16_t addr = start_addr + i;
        uint16_t reg_val = 0u;

        if (addr < MB_REG_OP_COUNT) {
            /* Map 0x0000 - 0x000B block variables */
            switch (addr) {
                case 0x0000u: reg_val = regs->adc_ch0_raw; break;
                case 0x0001u: reg_val = regs->adc_ch0_filtered; break;
                case 0x0002u: reg_val = regs->adc_ch1_raw; break;
                case 0x0003u: reg_val = regs->adc_ch1_filtered; break;
                case 0x0004u: reg_val = regs->adc_ch2_raw; break;
                case 0x0005u: reg_val = regs->adc_ch2_filtered; break;
                case 0x0006u: reg_val = regs->pwm_current_duty; break;
                case 0x0007u: reg_val = regs->device_state; break;
                case 0x0008u: reg_val = regs->system_fault_mask; break;
                case 0x0009u: reg_val = regs->crash_reset_count; break;
                /* Handle 32-bit uptime milliseconds atomically */
                case 0x000Au: reg_val = (uint16_t)(regs->system_uptime_ms & 0xFFFFu); break;
                case 0x000Bu: reg_val = (uint16_t)((regs->system_uptime_ms >> 16u) & 0xFFFFu); break;
                default: return false; /* Architecture fault trap */
            }
        } else if ((addr >= MB_REG_CFG_START) && (addr < (MB_REG_CFG_START + MB_REG_CFG_COUNT))) {
            /* Map 0x0100 - 0x0107 block configurations */
            switch (addr) {
                case 0x0100u: reg_val = regs->cfg_slave_address; break;
                case 0x0101u: reg_val = regs->cfg_filter_alpha; break;
                case 0x0102u: reg_val = regs->cfg_pid_kp; break;
                case 0x0103u: reg_val = regs->cfg_pid_ki; break;
                case 0x0104u: reg_val = regs->cfg_pid_kd; break;
                case 0x0105u: reg_val = regs->cfg_target_setpoint; break;
                case 0x0106u: reg_val = regs->cfg_manual_pwm_mode; break;
                case 0x0107u: reg_val = regs->cfg_manual_pwm_duty; break;
                default: return false;
            }
        } else {
            /* Requested register address is out of legal specification zones */
            return false;
        }

        /* Write serialized Big-Endian value to wire data stream payload */
        put_be16(&out_data[i * 2u], reg_val);
    }

    return true;
}

/**
 * @brief Intercept and process incoming Modbus Write configuration request.
 * Enforces rigorous boundary parameter validation.
 */
bool mb_regs_write_single(
    modbus_reg_map_t *regs,
    uint16_t reg_addr,
    uint16_t value,
    bool *out_cfg_changed)
{
    if ((regs == NULL) || (out_cfg_changed == NULL)) {
        return false;
    }

    *out_cfg_changed = false;
    uint16_t *target_reg = NULL;
    uint32_t min_bound = 0u;
    uint32_t max_bound = 0xFFFFu;

    /* Evaluate register targets and apply strict boundaries constraints */
    switch (reg_addr) {
        case 0x0100u: /* Network ID address */
            target_reg = &regs->cfg_slave_address;
            min_bound = 1u;
            max_bound = 247u;
            break;
        case 0x0101u: /* EMA Factor alpha filter */
            target_reg = &regs->cfg_filter_alpha;
            min_bound = 0u;
            max_bound = 16384u; /* Max scale limit Q14 (100% alpha) */
            break;
        case 0x0102u: /* PID Kp */
            target_reg = &regs->cfg_pid_kp;
            break;
        case 0x0103u: /* PID Ki */
            target_reg = &regs->cfg_pid_ki;
            break;
        case 0x0104u: /* PID Kd */
            target_reg = &regs->cfg_pid_kd;
            break;
        case 0x0105u: /* Target Setpoint coordinate */
            target_reg = &regs->cfg_target_setpoint;
            min_bound = 0u;
            max_bound = 4095u; /* Limited to 12-bit ADC physical resolution */
            break;
        case 0x0106u: /* Output execution mode flag */
            target_reg = &regs->cfg_manual_pwm_mode;
            min_bound = 0u;
            max_bound = 1u;
            break;
        case 0x0107u: /* Manual enforced load duty */
            target_reg = &regs->cfg_manual_pwm_duty;
            min_bound = 0u;
            max_bound = 10000u; /* Limited to 100.00% duty cycle configuration */
            break;
        default:
            /* Address is either Read-Only (0x0000 block) or out of layout ranges */
            return false;
    }

    /* Guard criteria verification against overflow anomalies */
    if (((uint32_t)value < min_bound) || ((uint32_t)value > max_bound)) {
        return false;
    }

    if (target_reg != NULL) {
        /* If incoming data matches old state, intercept execution to save Flash resource */
        if (*target_reg != value) {
            *target_reg = value;
            *out_cfg_changed = true;
        }
    }

    return true;
}
