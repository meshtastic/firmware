/**
 * @file usb_packet_handler.h
 * @brief USB packet processing interface for Core1
 *
 * Consolidated packet processing combining protocol validation and bit unstuffing.
 * Processes raw PIO data into validated USB packets and extracts keyboard reports.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef USB_PACKET_HANDLER_H
#define USB_PACKET_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process a captured USB packet on Core1
 *
 * This function performs complete packet processing including:
 * - Bit unstuffing (removes USB bit-stuffing)
 * - SYNC byte validation
 * - PID validation and extraction
 * - Data packet filtering (ignores tokens/handshakes)
 * - Keyboard report decoding (if applicable)
 *
 * The function is designed to be called inline during capture on Core1.
 * Valid keyboard events are pushed directly to the keystroke queue.
 *
 * @param raw_packet_data Pointer to raw packet data (31-bit words from PIO)
 * @param raw_size_bits Size of raw packet in bits
 * @param output_buffer Temporary buffer for decoded packet (min 64 bytes)
 * @param output_buffer_size Size of output buffer
 * @param is_full_speed Full speed mode flag
 * @param timestamp_us Packet timestamp in microseconds
 * @return Size of processed packet in bytes, or 0 on error/filtered
 */
int usb_packet_handler_process(
    const uint32_t *raw_packet_data,
    int raw_size_bits,
    uint8_t *output_buffer,
    int output_buffer_size,
    bool is_full_speed,
    uint32_t timestamp_us);

#ifdef __cplusplus
}
#endif

#endif /* USB_PACKET_HANDLER_H */
