/**
 * @file pio_manager.h
 * @brief PIO state machine management for USB signal capture
 *
 * This module manages the Programmable I/O (PIO) state machines used to
 * capture USB signals at high speed. It handles initialization, configuration,
 * and cleanup of both PIO0 (data capture) and PIO1 (synchronization).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PIO_MANAGER_H
#define PIO_MANAGER_H

#include <stdbool.h>
#include "hardware/pio.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PIO configuration structure
 */
typedef struct {
    PIO pio0_instance;
    PIO pio1_instance;
    int pio0_sm;
    int pio1_sm;
    uint pio0_offset;
    uint pio1_offset;
    bool initialized;
} pio_config_t;

/**
 * @brief Initialize PIO manager
 *
 * Sets up internal structures. Must be called before any other PIO
 * manager functions.
 */
void pio_manager_init(void);

/**
 * @brief Configure and start PIO state machines for USB capture
 *
 * Configures both PIO0 and PIO1 state machines for capturing USB signals
 * at the specified speed. Sets up GPIO pins, clock dividers, and loads
 * the appropriate PIO programs.
 *
 * @param config Pointer to PIO configuration structure to populate
 * @param full_speed true for full speed (12 Mbps), false for low speed (1.5 Mbps)
 * @return true if successful, false on error
 */
bool pio_manager_configure_capture(pio_config_t *config, bool full_speed);

/**
 * @brief Stop and cleanup PIO state machines
 *
 * Disables the PIO state machines and releases resources.
 *
 * @param config Pointer to PIO configuration structure
 */
void pio_manager_stop_capture(pio_config_t *config);

/**
 * @brief Destroy all PIO state machines and clear memory
 *
 * Performs complete cleanup of both PIO blocks, unclaiming all state
 * machines and clearing instruction memory. Used for full reset.
 */
void pio_manager_destroy_all(void);

/**
 * @brief Calculate clock divider for target frequency
 *
 * Calculates the appropriate clock divider to achieve the target
 * frequency for USB capture timing.
 *
 * @param full_speed true for full speed, false for low speed
 * @return Calculated clock divider value
 */
float pio_manager_calculate_clock_divider(bool full_speed);


#ifdef __cplusplus
}
#endif
#endif /* PIO_MANAGER_H */
