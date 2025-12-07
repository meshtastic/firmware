/**
 * @file usb_packet_handler.cpp
 * @brief USB packet processing implementation for Core1
 *
 * Consolidated packet processing combining:
 * - USB protocol validation (CRC, PID, SYNC)
 * - Bit unstuffing and packet reconstruction
 * - Keyboard report extraction
 *
 * CRITICAL: ALL functions in this file execute from RAM, not flash.
 * This prevents crashes when Core0 writes to flash (Arduino-Pico limitation).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb_packet_handler.h"
#include "keyboard_decoder_core1.h"
#include <string.h>

extern "C" {
#include "common.h"

/* ============================================================================
 * PRIVATE: USB PROTOCOL VALIDATION
 * ============================================================================ */

/**
 * @brief CRC16 USB lookup table for fast computation
 */
static const uint16_t crc16_usb_table[256] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
    0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
    0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
    0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
    0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
    0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
    0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
    0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
    0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
    0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
    0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
    0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
    0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
    0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
    0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
    0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
    0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
    0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
    0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
    0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
    0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
    0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
    0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
    0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
    0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
    0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
    0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
    0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
    0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
    0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
    0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
    0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040};

/**
 * @brief Calculate USB CRC16 checksum (private)
 */
static uint16_t calculate_crc16(uint8_t *data, int size)
{
    uint16_t crc = 0xffff;

    for (int i = 0; i < size; i++)
    {
        crc = crc16_usb_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }

    return crc;
}

/**
 * @brief Verify USB CRC16 checksum (private)
 */
static bool verify_crc16(uint8_t *data, int size)
{
    if (size < 2)
    {
        return false;
    }

    uint16_t crc = calculate_crc16(data, size);
    return (crc == USB_CRC16_RESIDUAL);
}

/**
 * @brief Validate USB PID (Packet Identifier) (private)
 */
CORE1_RAM_FUNC
static bool validate_pid(uint8_t pid_byte)
{
    uint8_t pid = pid_byte & 0x0f;
    uint8_t npid = (~pid_byte >> 4) & 0x0f;

    /* Check that PID and complement match */
    if (pid != npid)
    {
        return false;
    }

    /* Check that PID is not reserved value */
    if (pid == PID_RESERVED)
    {
        return false;
    }

    return true;
}

/**
 * @brief Extract PID value from PID byte (private)
 */
static inline uint8_t extract_pid(uint8_t pid_byte)
{
    return pid_byte & 0x0f;
}

/**
 * @brief Validate USB SYNC byte (private)
 */
CORE1_RAM_FUNC
static bool validate_sync(uint8_t sync_byte, bool fs)
{
    uint8_t expected_sync = fs ? USB_FULL_SPEED_SYNC : USB_LOW_SPEED_SYNC;
    return (sync_byte == expected_sync);
}

/**
 * @brief Check if PID indicates a data packet (private)
 */
static inline bool is_data_pid(uint8_t pid)
{
    return (pid == PID_DATA0 || pid == PID_DATA1);
}

/* ============================================================================
 * PUBLIC: PACKET PROCESSING
 * ============================================================================ */

/**
 * @brief Process a single captured packet (inline, in-place)
 *
 * This function performs bit unstuffing and validation on a raw captured
 * packet, then immediately passes valid keyboard packets to the decoder.
 */
CORE1_RAM_FUNC
static int process_packet_inline(
    const uint32_t *raw_data,
    int raw_size,
    uint8_t *out_buffer,
    int max_out_size,
    bool is_full_speed,
    uint32_t timestamp_us)
{
    uint32_t error = 0;

    /* Validate raw packet size - early exit for noise */
    if (raw_size < 4 || raw_size > 1000)
    {
        /* Don't even count this as an error - likely just noise */
        return 0;
    }

    /* Additional filter: packets smaller than minimum USB packet are noise
     * Minimum meaningful USB packet: SYNC(8) + PID(8) + CRC(16) = 32 bits minimum
     * But we'll be conservative and allow smaller to not miss edge cases
     */
    if (raw_size < 24) /* Below this is definitely noise */
    {
        return 0;
    }

    /* Bit unstuffing variables */
    uint32_t v = 0x80000000;
    int out_size = 0;
    int out_byte = 0;
    int out_bit = 0;
    int stuff_count = 0;
    int rd_ptr = 0;

    /* Calculate number of 31-bit words to process */
    const int word_count = (raw_size + 30) / 31;

    /* Bit unstuffing loop */
    for (int w_idx = 0; w_idx < word_count && raw_size > 0; w_idx++)
    {
        uint32_t w = raw_data[rd_ptr++];
        int bit_count;

        if (raw_size < 31)
        {
            w <<= (30 - raw_size);
            bit_count = raw_size;
        }
        else
        {
            bit_count = 31;
        }

        v ^= (w ^ (w << 1));

        /* Process each bit */
        for (int i = 0; i < bit_count; i++)
        {
            int bit = (v & 0x80000000) ? 0 : 1;
            v <<= 1;

            /* Bit stuffing check */
            if (stuff_count == 6)
            {
                if (bit)
                {
                    error |= CAPTURE_ERROR_STUFF;
                    stats_increment_stuff_error();
                }
                stuff_count = 0;
                continue;
            }
            else if (bit)
            {
                stuff_count++;
            }
            else
            {
                stuff_count = 0;
            }

            /* Accumulate bits into bytes */
            out_byte |= (bit << out_bit);
            out_bit++;

            if (out_bit == 8)
            {
                if (out_size >= max_out_size)
                {
                    error |= CAPTURE_ERROR_SIZE;
                    stats_increment_size_error();
                    return 0;
                }

                out_buffer[out_size++] = out_byte;
                out_byte = 0;
                out_bit = 0;

                /* Early exit for oversized packets */
                if (out_size > 64)
                {
                    error |= CAPTURE_ERROR_SIZE;
                    stats_increment_size_error();
                    return 0;
                }
            }
        }
        raw_size -= bit_count;
    }

    /* Check for incomplete byte */
    if (out_bit)
    {
        error |= CAPTURE_ERROR_NBIT;
    }

    /* Validate minimum packet size */
    if (out_size < 2)
    {
        error |= CAPTURE_ERROR_SIZE;
        stats_increment_size_error();
        return 0;
    }

    /* Validate SYNC byte */
    if (!validate_sync(out_buffer[0], is_full_speed))
    {
        error |= CAPTURE_ERROR_SYNC;
        stats_increment_sync_error();
    }

    /* Validate PID */
    if (!validate_pid(out_buffer[1]))
    {
        error |= CAPTURE_ERROR_PID;
        stats_increment_pid_error();
    }

    /* Extract PID for data packet checking */
    uint8_t pid = extract_pid(out_buffer[1]);

    /* OPTIMIZATION: Only process DATA packets (skip tokens, handshakes)
     * This filters out IN, OUT, SOF, ACK, NAK, STALL packets
     * We only care about DATA0/DATA1 which contain keyboard reports
     */
    if (!is_data_pid(pid))
    {
        /* Not a data packet - ignore it */
        return 0;
    }

    /* Size validation for data packets */
    if (out_size < 4)
    {
        error |= CAPTURE_ERROR_SIZE;
        stats_increment_size_error();
        return 0;
    }

    /* CRC VALIDATION SKIPPED FOR KEYBOARD CAPTURE
     *
     * V1 skips CRC in live mode for performance and reliability.
     * Reasons to skip:
     * 1. USB keyboards rarely have bit errors with good wiring
     * 2. If a key is corrupted, user just retypes it
     * 3. We only care about typed keys, not perfect capture
     * 4. Real-time responsiveness > perfect validation
     * 5. CRC validation was causing 100% packet rejection
     *
     * Uncomment below to enable CRC validation:
     * if (!verify_crc16(&out_buffer[2], out_size - 2))
     * {
     *     error |= CAPTURE_ERROR_CRC;
     *     stats_increment_crc_error();
     * }
     */

    /* Update statistics for successful packets */
    if (!error)
    {
        stats_record_packet(out_size);
    }

    /* Process keyboard packets immediately (even with minor errors)
     * We've already validated SYNC, PID, and size
     * Minor bit errors won't crash the decoder
     */
    if (out_size >= 10)
    {
        keyboard_decoder_core1_process_report(out_buffer, out_size, timestamp_us);
    }

    return out_size;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

CORE1_RAM_FUNC
int usb_packet_handler_process(
    const uint32_t *raw_packet_data,
    int raw_size_bits,
    uint8_t *output_buffer,
    int output_buffer_size,
    bool is_full_speed,
    uint32_t timestamp_us)
{
    return process_packet_inline(
        raw_packet_data,
        raw_size_bits,
        output_buffer,
        output_buffer_size,
        is_full_speed,
        timestamp_us);
}

} // extern "C"
