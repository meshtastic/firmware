/**
 * @file keyboard_decoder_core1.h
 * @brief USB HID keyboard report decoder for Core1
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef KEYBOARD_DECODER_CORE1_H
#define KEYBOARD_DECODER_CORE1_H

#include "common.h"
#include "keystroke_queue.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Core1 keyboard decoder
 *
 * @param queue Pointer to keystroke queue for output
 */
void keyboard_decoder_core1_init(keystroke_queue_t *queue);

/**
 * @brief Reset the Core1 keyboard decoder state
 */
void keyboard_decoder_core1_reset(void);

/**
 * @brief Convert HID scancode to ASCII character
 *
 * @param scancode HID scancode
 * @param shift_pressed Whether shift modifier is active
 * @return ASCII character or 0 if not printable
 */
char keyboard_decoder_core1_scancode_to_ascii(uint8_t scancode, bool shift_pressed);

/**
 * @brief Process a USB HID keyboard report and push events to queue
 *
 * This function extracts keystroke events from a USB HID keyboard report
 * and pushes them to the keystroke queue for Core0 consumption.
 *
 * @param data Pointer to packet data (includes SYNC and PID)
 * @param size Size of packet data in bytes
 * @param timestamp_us Packet timestamp in microseconds
 */
void keyboard_decoder_core1_process_report(uint8_t *data, int size, uint32_t timestamp_us);

/**
 * @brief Get current keyboard state
 *
 * @return Pointer to keyboard state structure
 */
const keyboard_state_t *keyboard_decoder_core1_get_state(void);


#ifdef __cplusplus
}
#endif
#endif /* KEYBOARD_DECODER_CORE1_H */
