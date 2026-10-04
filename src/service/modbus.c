#include "service/modbus.h"

uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    if ((data == NULL) || (len == 0u)) {
        return 0xFFFFu;
    }

    for (size_t i = 0u; i < len; ++i) {
        crc ^= (uint16_t)data[i];

        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            if ((crc & 0x0001u) != 0u) {
                crc = (crc >> 1u) ^ 0xA001u;
            } else {
                crc = crc >> 1u;
            }
        }
    }

    return crc;
}

modbus_parse_err_t modbus_parse_request(
    const uint8_t *frame,
    size_t len,
    uint8_t own_slave_addr,
    modbus_rx_request_t *out_request)
{
    if ((frame == NULL) || (out_request == NULL)) {
        return MODBUS_PARSE_ERR_INVALID_ARG;
    }

    /* Minimal legal Modbus RTU frame size: SlaveID(1) + Func(1) + Data/Addr(2) + CRC(2) = 4 bytes minimum */
    if (len < 4u) {
        return MODBUS_PARSE_ERR_SHORT_FRAME;
    }

    /* Verify trailing 16-bit CRC checksum integrity */
    uint16_t calculated_crc = modbus_crc16(frame, len - 2u);
    uint16_t received_crc = (uint16_t)frame[len - 2u] | ((uint16_t)frame[len - 1u] << 8u);

    if (calculated_crc != received_crc) {
        return MODBUS_PARSE_ERR_CRC;
    }

    uint8_t target_addr = frame[0];
    uint8_t func_code = frame[1];

    /* Address validation: Match node ID network station address or accept Broadcast (0u) commands */
    if ((target_addr != own_slave_addr) && (target_addr != 0u)) {
        return MODBUS_PARSE_ERR_BAD_ADDR;
    }

    out_request->slave_addr = target_addr;
    out_request->function_code = func_code;
    out_request->payload = &frame[2];
    out_request->payload_len = len - 4u;

    /* Enforce verification of function criteria lengths constraints */
    if ((func_code == 0x03u) || (func_code == 0x06u)) {
        if (out_request->payload_len != 4u) {
            return MODBUS_PARSE_ERR_BAD_LENGTH;
        }
    } else {
        return MODBUS_PARSE_ERR_UNSUPPORTED_FUNC;
    }

    return MODBUS_PARSE_OK;
}

size_t modbus_build_response(
    uint8_t slave_addr,
    uint8_t function_code,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *out_buf,
    size_t out_capacity)
{
    if ((out_buf == NULL) || (out_capacity < 5u)) {
        return 0u;
    }

    size_t required_size = 2u + payload_len + 2u;
    if (out_capacity < required_size) {
        return 0u;
    }

    out_buf[0] = slave_addr;
    out_buf[1] = function_code;

    if ((payload != NULL) && (payload_len > 0u)) {
        for (size_t i = 0u; i < payload_len; ++i) {
            out_buf[2u + i] = payload[i];
        }
    }

    /* Compute and append CRC16 Big-Endian layout bytes */
    uint16_t crc = modbus_crc16(out_buf, 2u + payload_len);
    out_buf[2u + payload_len] = (uint8_t)(crc & 0xFFu);
    out_buf[2u + payload_len + 1u] = (uint8_t)((crc >> 8u) & 0xFFu);

    return required_size;
}

size_t modbus_build_exception(
    uint8_t slave_addr,
    uint8_t original_func,
    uint8_t exception_code,
    uint8_t *out_buf,
    size_t out_capacity)
{
    if ((out_buf == NULL) || (out_capacity < 5u)) {
        return 0u;
    }

    out_buf[0] = slave_addr;
    out_buf[1] = original_func | 0x80u; /* Assert error bit flag marker */
    out_buf[2] = exception_code;

    uint16_t crc = modbus_crc16(out_buf, 3u);
    out_buf[3] = (uint8_t)(crc & 0xFFu);
    out_buf[4] = (uint8_t)((crc >> 8u) & 0xFFu);

    return 5u;
}
