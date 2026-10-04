#include "service/logger.h"
#include "platform/platform.h"
#include <stddef.h>

#define LOGGER_BUF_SIZE    256u
#define LOGGER_CHUNK_SIZE   32u

static char g_logger_ring_buffer[LOGGER_BUF_SIZE];
static size_t g_log_head = 0u;
static size_t g_log_tail = 0u;
static size_t g_log_count = 0u;

static log_level_t g_current_log_level = LOG_LEVEL_INFO;
static uint32_t g_logger_dropped_chars = 0u;

static bool logger_push_buf(char c)
{
    if (g_log_count >= LOGGER_BUF_SIZE) {
        g_logger_dropped_chars++;
        return false;
    }

    g_logger_ring_buffer[g_log_head] = c;
    g_log_head++;
    if (g_log_head >= LOGGER_BUF_SIZE) {
        g_log_head = 0u;
    }
    g_log_count++;
    return true;
}

static bool logger_pop_buf(char *out_char)
{
    if ((out_char == NULL) || (g_log_count == 0u)) {
        return false;
    }

    *out_char = g_logger_ring_buffer[g_log_tail];
    g_log_tail++;
    if (g_log_tail >= LOGGER_BUF_SIZE) {
        g_log_tail = 0u;
    }
    g_log_count--;
    return true;
}

void logger_init(void)
{
    g_log_head = 0u;
    g_log_tail = 0u;
    g_log_count = 0u;
    g_logger_dropped_chars = 0u;
    g_current_log_level = LOG_LEVEL_INFO;
}

void logger_set_level(log_level_t level)
{
    g_current_log_level = level;
}

bool logger_level_enabled(log_level_t level)
{
    return (level <= g_current_log_level);
}

void logger_write_char(char c)
{
    (void)logger_push_buf(c);
}

void logger_write_str(const char *s)
{
    if (s == NULL) {
        return;
    }
    const char *ptr = s;
    while (*ptr != '\0') {
        (void)logger_push_buf(*ptr);
        ptr++;
    }
}

void logger_write_u32(uint32_t x)
{
    if (x == 0u) {
        (void)logger_push_buf('0');
        return;
    }

    char temp_str[10];
    uint8_t digits_cnt = 0u;
    uint32_t val = x;

    while (val > 0u) {
        temp_str[digits_cnt] = (char)('0' + (char)(val % 10u));
        val /= 10u;
        digits_cnt++;
    }

    while (digits_cnt > 0u) {
        digits_cnt--;
        (void)logger_push_buf(temp_str[digits_cnt]);
    }
}

void logger_write_u32_padded(uint32_t x, uint8_t width)
{
    uint32_t dummy_val = x;
    uint8_t registered_digits = 0u;

    do {
        registered_digits++;
        dummy_val /= 10u;
    } while (dummy_val != 0u);

    while (registered_digits < width) {
        (void)logger_push_buf('0');
        registered_digits++;
    }

    logger_write_u32(x);
}

void logger_write_i32(int32_t x)
{
    if (x < 0L) {
        (void)logger_push_buf('-');
        uint32_t absolute_magnitude = (uint32_t)(-(x + 1L)) + 1u;
        logger_write_u32(absolute_magnitude);
    } else {
        logger_write_u32((uint32_t)x);
    }
}

void logger_new_line(void)
{
    (void)logger_push_buf('\r');
    (void)logger_push_buf('\n');
}

void logger_process_flush_task(void)
{
    char transfer_chunk[LOGGER_CHUNK_SIZE];
    size_t chunk_bytes_collected = 0u;

    /* Extract a limited chunk packet bounded to prevent scheduler thread monopoly states */
    while ((chunk_bytes_collected < LOGGER_CHUNK_SIZE) && logger_pop_buf(&transfer_chunk[chunk_bytes_collected])) {
        chunk_bytes_collected++;
    }

    if (chunk_bytes_collected > 0u) {
        /* Delegate the packed memory row to low-level platform HAL driver */
        platform_write(transfer_chunk, chunk_bytes_collected);
    }
}

void logger_flush_blocking(void)
{
    while (g_log_count > 0u) {
        logger_process_flush_task();
    }
}

uint32_t logger_get_dropped_count(void)
{
    return g_logger_dropped_chars;
}
