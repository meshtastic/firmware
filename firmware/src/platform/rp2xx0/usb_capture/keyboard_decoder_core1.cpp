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
#include "pico/time.h"  /* For time_us_64() */
#include <string.h>

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

/* Keystroke queue pointer (set during init) */
static keystroke_queue_t *g_keystroke_queue = NULL;

void keyboard_decoder_core1_init(keystroke_queue_t *queue)
{
    g_keystroke_queue = queue;
    keyboard_decoder_core1_reset();
}

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
            if (keycode == HID_SCANCODE_ENTER)
            {
                /* Enter key */
                event = keystroke_event_create_special(
                    KEYSTROKE_TYPE_ENTER, keycode, capture_timestamp_us);
                keystroke_queue_push(g_keystroke_queue, &event);
            }
            else if (keycode == HID_SCANCODE_BACKSPACE)
            {
                /* Backspace key */
                event = keystroke_event_create_special(
                    KEYSTROKE_TYPE_BACKSPACE, keycode, capture_timestamp_us);
                keystroke_queue_push(g_keystroke_queue, &event);
            }
            else if (keycode == HID_SCANCODE_TAB)
            {
                /* Tab key */
                event = keystroke_event_create_special(
                    KEYSTROKE_TYPE_TAB, keycode, capture_timestamp_us);
                keystroke_queue_push(g_keystroke_queue, &event);
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
}
