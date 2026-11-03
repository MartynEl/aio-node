/* include/platform/uart1.h */
#ifndef PLATFORM_UART1_H
#define PLATFORM_UART1_H

#include <stdint.h>
#include <stddef.h>

void uart1_init(void);
void uart1_write_raw(const uint8_t *data, size_t len);
size_t uart1_rx_read(uint8_t *dst, size_t max_len);

#endif /* PLATFORM_UART1_H */