#ifndef SERVICE_MODBUS_H
#define SERVICE_MODBUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Modbus protocol parsing error codes compliant with standard exceptions.
 */
typedef enum {
    MODBUS_PARSE_OK = 0,
    MODBUS_PARSE_ERR_INVALID_ARG,
    MODBUS_PARSE_ERR_SHORT_FRAME,
    MODBUS_PARSE_ERR_CRC,
    MODBUS_PARSE_ERR_BAD_ADDR,
    MODBUS_PARSE_ERR_UNSUPPORTED_FUNC,
    MODBUS_PARSE_ERR_BAD_LENGTH
} modbus_parse_err_t;

/**
 * @brief Structured representation of an extracted valid Modbus PDU request.
 */
typedef struct {
    uint8_t slave_addr;      /* Targeted station address ID */
    uint8_t function_code;   /* Executed Modbus function (0x03 or 0x06) */
    const uint8_t *payload;  /* Pointer directly mapping data payload inside DMA buffer */
    size_t payload_len;      /* Byte size length of the active payload segment */
} modbus_rx_request_t;

/**
 * @brief Calculate standard Modbus RTU CRC16 checksum (polynomial 0xA001).
 * 
 * @param data Pointer to the source memory byte array.
 * @param len Total length of data to process.
 * @return uint16_t Evaluated 16-bit CRC checksum value.
 */
uint16_t modbus_crc16(const uint8_t *data, size_t len);

/**
 * @brief Parse a raw frame payload directly received from the asynchronous DMA channel.
 * MISRA C:2012 Compliance: Enforces hard boundary checks and const correctness.
 * 
 * @param frame Pointer to the start of the received data packet block.
 * @param len Total byte size of the captured packet frame.
 * @param own_slave_addr Current station network ID to verify target matching.
 * @param out_request Pointer to output struct to populate upon successful verification.
 * @return modbus_parse_err_t Status parsing error code result.
 */
modbus_parse_err_t modbus_parse_request(
    const uint8_t *frame,
    size_t len,
    uint8_t own_slave_addr,
    modbus_rx_request_t *out_request);

/**
 * @brief Serialize a standard Modbus RTU response frame into transmission buffer.
 * Automatically appends 16-bit Big-Endian CRC trailing bytes.
 * 
 * @return Total bytes written to destination buffer, 0 on capacity fault bounds.
 */
size_t modbus_build_response(
    uint8_t slave_addr,
    uint8_t function_code,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *out_buf,
    size_t out_capacity);

/**
 * @brief Serialize a standard Modbus Exception packet (Error frames).
 */
size_t modbus_build_exception(
    uint8_t slave_addr,
    uint8_t original_func,
    uint8_t exception_code,
    uint8_t *out_buf,
    size_t out_capacity);

/* Standard Modbus hardware exception definitions */
#define MODBUS_EX_ILLEGAL_FUNC      0x01u
#define MODBUS_EX_ILLEGAL_DATA_ADDR 0x02u
#define MODBUS_EX_ILLEGAL_DATA_VAL  0x03u
#define MODBUS_EX_SLAVE_DEVICE_FAIL 0x04u

#endif /* SERVICE_MODBUS_H */
