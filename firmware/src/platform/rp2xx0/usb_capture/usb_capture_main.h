/**
 * @file usb_capture_main.h
 * @brief USB signal capture controller interface (Core1)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef USB_CAPTURE_MAIN_H
#define USB_CAPTURE_MAIN_H

#include "common.h"
#include "keystroke_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Core1 main loop - Capture and Process Pipeline
 *
 * This function runs on Core1 and handles:
 * 1. Capture raw USB data from PIO FIFO
 * 2. Accumulate into raw packet buffer
 * 3. When packet complete, immediately process it
 * 4. Validated keyboard packets → decoded events pushed to queue
 * 5. Core0 reads queue for display/WiFi/logging
 */
void capture_controller_core1_main_v2(void);

/**
 * @brief Initialize capture controller
 *
 * @param controller Pointer to controller structure
 * @param keystroke_queue Pointer to keystroke queue
 */
void capture_controller_init_v2(capture_controller_t *controller,
                                keystroke_queue_t *keystroke_queue);

/**
 * @brief Set capture speed
 *
 * @param controller Pointer to controller structure
 * @param speed Capture speed (LOW or FULL)
 */
void capture_controller_set_speed_v2(capture_controller_t *controller,
                                     capture_speed_t speed);

/**
 * @brief Get capture speed
 *
 * @param controller Pointer to controller structure
 * @return Current capture speed
 */
capture_speed_t capture_controller_get_speed_v2(capture_controller_t *controller);

/**
 * @brief Check if capture is running
 *
 * @param controller Pointer to controller structure
 * @return true if capture is running
 */
bool capture_controller_is_running_v2(capture_controller_t *controller);

/**
 * @brief Start capture on Core1
 *
 * @param controller Pointer to controller structure
 */
void capture_controller_start_v2(capture_controller_t *controller);

/**
 * @brief Stop capture on Core1
 *
 * @param controller Pointer to controller structure
 */
void capture_controller_stop_v2(capture_controller_t *controller);

#ifdef __cplusplus
}
#endif
#endif /* USB_CAPTURE_MAIN_H */
