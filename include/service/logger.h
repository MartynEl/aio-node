#ifndef SERVICE_LOGGER_H
#define SERVICE_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief System debug log levels execution thresholds.
 */
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} log_level_t;

/**
 * @brief Initialize the static asynchronous ring buffer logger memory block.
 */
void logger_init(void);

/**
 * @brief Set the global dynamic log execution level criteria threshold.
 */
void logger_set_level(log_level_t level);

/**
 * @brief Evaluate if a specific severity level is allowed to execute logging.
 */
bool logger_level_enabled(log_level_t level);

/**
 * @brief Fast non-blocking push of a single character into the internal ring buffer.
 * MISRA C:2012 Compliance: Operates atomically with zero wait states.
 */
void logger_write_char(char c);

/**
 * @brief Stream a null-terminated string literal into the lockless log memory buffer.
 */
void logger_write_str(const char *s);

/**
 * @brief Format and log an unsigned 32-bit integer.
 */
void logger_write_u32(uint32_t x);

/**
 * @brief Format and log an unsigned 32-bit integer with explicit zero-padding width.
 */
void logger_write_u32_padded(uint32_t x, uint8_t width);

/**
 * @brief Format and log a signed 32-bit integer.
 */
void logger_write_i32(int32_t x);

/**
 * @brief Append a standard platform-independent CRLF newline sequence.
 */
void logger_new_line(void);

/**
 * @brief Periodic processing task flushing accumulated buffer chunks into the hardware output UART/DMA.
 * Invoked continuously by the cooperative background task scheduler.
 */
void logger_process_flush_task(void);

/**
 * @brief Synchronous blocking flush forcing the entire ring buffer layout to exhaust completely.
 * Invoked strictly during critical shutdown routines or early system boot stages.
 */
void logger_flush_blocking(void);

/**
 * @brief Retrieve total telemetry counter of characters dropped due to ring buffer capacity saturation.
 */
uint32_t logger_get_dropped_count(void);

#endif /* SERVICE_LOGGER_H */
