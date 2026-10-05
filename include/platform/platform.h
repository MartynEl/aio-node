#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "core/types.h"

/**
 * @brief Read the continuous system uptime counter in milliseconds.
 * MISRA C:2012 Compliance: Enforces monotonic uint32_t type.
 * 
 * @return uint32_t Elapsed milliseconds since hardware boot stage.
 */
uint32_t platform_millis(void);

/**
 * @brief Force the central CPU core into sleep mode until the next hardware interrupt.
 * Implements the standard assembly low-power "Wait For Interrupt" (WFI) instruction wrapper.
 */
void platform_wfi(void);

/**
 * @brief Initialize the hardware Independent Watchdog (IWDG) timer.
 * @param timeout_ms Safety execution window in milliseconds before a hardware reset triggers.
 */
void platform_wdg_init(uint32_t timeout_ms);

/**
 * @brief Reload the hardware Watchdog counter to prevent device reset.
 * Must be executed periodically within the stable scheduler loop.
 */
void platform_wdg_feed(void);

/**
 * @brief Read the hardware reset flag markers to evaluate the physical cause of boot.
 * Automatically clears hardware registers (e.g., RCC_CSR) after reading to prepare for next loop.
 * 
 * @return reset_cause_t Evaluated hardware cause enum value.
 */
reset_cause_t platform_reset_cause_get(void);

/**
 * @brief Configure and enable the internal Programmable Voltage Detector (PVD).
 * Monitors the power rail and triggers an immediate high-priority emergency interrupt 
 * if voltage drops below a safe execution threshold (e.g., 2.9V).
 */
void platform_pvd_init(void);

/**
 * @brief Atomically save critical context block into hardware non-volatile Backup Registers.
 * Retains stored state even during internal software or Watchdog CPU resets.
 * 
 * @param data Pointer to the source data block memory array.
 * @param len Byte size of data to stream (limited to hardware backup domain capacity).
 */
void platform_emergency_backup_save(const void *data, size_t len);

/**
 * @brief Recover critical tracking context block from the hardware Backup Registers upon boot.
 * 
 * @param out_data Destination memory pointer array to unpack registers payload.
 * @param len Expected block data byte size.
 * @return true if backup domain read is valid, false if memory corruption or power-loss occurs.
 */
bool platform_emergency_backup_load(void *out_data, size_t len);

/**
 * @brief Stream raw character byte block directly to the active hardware or host terminal.
 * 
 * @param data Pointer to the character memory buffer.
 * @param len Total bytes count to output.
 */
void platform_write(const char *data, size_t len);

/**
 * @brief Background processing hook flushing low-level driver tasks queues.
 */
void platform_poll(void);

#endif /* PLATFORM_PLATFORM_H */
