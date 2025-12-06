/**
 * @file usb_capture_main.cpp
 * @brief USB signal capture controller with Core1 processing pipeline
 *
 * Core1 runs INDEPENDENTLY without blocking Core0.
 * No command-based startup - Core1 auto-starts on launch.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "common.h"
#include "pio_manager.h"
#include "usb_packet_handler.h"
#include "keyboard_decoder_core1.h"
#include "keystroke_queue.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include <string.h>

extern "C" {

/* Global state for multi-core access */
static volatile capture_speed_t g_capture_speed_v2 = CAPTURE_SPEED_LOW;
static volatile bool g_capture_running_v2 = false;
static keystroke_queue_t *g_keystroke_queue_v2 = NULL;

/* Processing buffer for inline packet decoding */
#define PROCESSING_BUFFER_SIZE 128
static uint8_t g_processing_buffer[PROCESSING_BUFFER_SIZE];

/* Raw packet buffer for accumulating captured data */
#define RAW_PACKET_BUFFER_SIZE 256
static uint32_t g_raw_packet_buffer[RAW_PACKET_BUFFER_SIZE];

/**
 * @brief Core1 main loop - INDEPENDENT USB Capture
 *
 * Runs completely independently on Core1:
 * 1. Auto-starts on launch (no waiting for commands)
 * 2. Captures USB data via PIO
 * 3. Processes and decodes packets
 * 4. Pushes keystrokes to queue for Core0
 * 5. Never blocks Core0
 */
void capture_controller_core1_main_v2(void)
{
    /* Signal Core0 that Core1 has started (via queue status event) */
    if (g_keystroke_queue_v2)
    {
        keystroke_event_t startup_event = keystroke_event_create_special(
            KEYSTROKE_TYPE_RESET, 0xC1, time_us_64());  /* 0xC1 = Core1 started */
        keystroke_queue_push(g_keystroke_queue_v2, &startup_event);
    }

    /* Configure PIO for capture */
    pio_config_t pio_config = {0};
    bool fs = (g_capture_speed_v2 == CAPTURE_SPEED_FULL);

    /* Signal before PIO config */
    if (g_keystroke_queue_v2)
    {
        keystroke_event_t event = keystroke_event_create_special(
            KEYSTROKE_TYPE_RESET, 0xC2, time_us_64());  /* 0xC2 = Starting PIO config */
        keystroke_queue_push(g_keystroke_queue_v2, &event);
    }

    if (!pio_manager_configure_capture(&pio_config, fs))
    {
        /* PIO configuration failed - signal error */
        if (g_keystroke_queue_v2)
        {
            keystroke_event_t error_event = keystroke_event_create_error(
                0xDEADC1C1, time_us_64());  /* PIO config failed */
            keystroke_queue_push(g_keystroke_queue_v2, &error_event);
        }

        /* Sleep forever */
        while (1)
        {
            sleep_ms(1000);
        }
    }

    /* Signal PIO config success */
    if (g_keystroke_queue_v2)
    {
        keystroke_event_t event = keystroke_event_create_special(
            KEYSTROKE_TYPE_RESET, 0xC3, time_us_64());  /* 0xC3 = PIO configured */
        keystroke_queue_push(g_keystroke_queue_v2, &event);
    }

    /* Initialize keyboard decoder with queue */
    if (g_keystroke_queue_v2)
    {
        keyboard_decoder_core1_init(g_keystroke_queue_v2);
    }

    /* Enable watchdog */
    watchdog_enable(4000, true);

    /* Signal ready to capture */
    if (g_keystroke_queue_v2)
    {
        keystroke_event_t event = keystroke_event_create_special(
            KEYSTROKE_TYPE_RESET, 0xC4, time_us_64());  /* 0xC4 = Ready to capture */
        keystroke_queue_push(g_keystroke_queue_v2, &event);
    }

    /* Base timestamp for relative timing */
    uint32_t base_time = 0;

    /* Packet accumulation state */
    int raw_packet_index = 0;
    int raw_packet_size_bits = 0;
    uint32_t packet_start_time = 0;

    /* Idle detection to reduce CPU when no USB activity */
    uint32_t empty_fifo_count = 0;
    const uint32_t IDLE_THRESHOLD = 100;

    /* Mark as running */
    g_capture_running_v2 = true;

    /* Main capture and processing loop */
    while (g_capture_running_v2)
    {
        /* Check for stop command from Core0 (non-blocking check) */
        if (multicore_fifo_rvalid())
        {
            uint32_t signal = multicore_fifo_pop_blocking();
            if (signal == 0xDEADBEEF)
            {
                g_capture_running_v2 = false;
                break;
            }
        }

        watchdog_update();

        /* Check if PIO has data (non-blocking) */
        if (pio_sm_is_rx_fifo_empty(pio_config.pio0_instance, pio_config.pio0_sm))
        {
            /* No data available */
            empty_fifo_count++;

            /* After many empty checks, assume USB is idle and back off */
            if (empty_fifo_count > IDLE_THRESHOLD)
            {
                /* Micro-sleep to reduce CPU usage during idle periods */
                uint64_t idle_start = time_us_64();
                sleep_us(10); /* Very short sleep - won't miss packets */
                uint64_t idle_end = time_us_64();
                stats_update_core1_idle_time(idle_end - idle_start);
                empty_fifo_count = 0;
            }
            else
            {
                tight_loop_contents();
            }
            continue;
        }

        /* Data available - reset idle counter */
        empty_fifo_count = 0;

        /* Read value from PIO FIFO */
        uint64_t read_start = time_us_64();
        uint32_t v = pio_sm_get(pio_config.pio0_instance, pio_config.pio0_sm);
        uint64_t read_end = time_us_64();

        stats_update_core1_capture_time(read_end - read_start);
        cpu_monitor_record_core1_work();

        /* Check if this is a packet boundary marker */
        if (v & 0x80000000)
        {
            /* Packet boundary marker - end of current packet */
            uint32_t current_packet_size_bits = 0xffffffff - v;

            /* Capture timestamp for END of this packet */
            if (base_time == 0)
            {
                base_time = timer_hw->timelr;
            }
            uint32_t current_packet_timestamp = timer_hw->timelr - base_time;

            /* Process the packet we just accumulated */
            if (raw_packet_index > 0 && current_packet_size_bits > 0)
            {
                /* Only process keyboard-sized packets */
                const uint32_t min_bits = KEYBOARD_PACKET_MIN_SIZE * 8;
                const uint32_t max_bits = KEYBOARD_PACKET_MAX_SIZE * 8;

                if (current_packet_size_bits >= min_bits &&
                    current_packet_size_bits <= max_bits)
                {
                    uint64_t process_start = time_us_64();

                    /* Process packet inline - validates and decodes */
                    usb_packet_handler_process(
                        g_raw_packet_buffer,
                        current_packet_size_bits,
                        g_processing_buffer,
                        PROCESSING_BUFFER_SIZE,
                        fs,
                        current_packet_timestamp);

                    uint64_t process_end = time_us_64();
                    stats_update_core1_capture_time(process_end - process_start);
                    cpu_monitor_record_core1_work();
                }
                else
                {
                    /* Skip noise/non-keyboard packets */
                    uint64_t idle_delta = 10;
                    stats_update_core1_idle_time(idle_delta);
                }
            }

            /* Reset for NEXT packet */
            raw_packet_index = 0;
            raw_packet_size_bits = 0;
            packet_start_time = 0;
        }
        else
        {
            /* Data word - accumulate into raw packet buffer */
            if (raw_packet_index < RAW_PACKET_BUFFER_SIZE)
            {
                g_raw_packet_buffer[raw_packet_index++] = v;
            }
            else
            {
                /* Buffer overflow - reset packet accumulation */
                raw_packet_index = 0;
                raw_packet_size_bits = 0;
                stats_increment_overflow();
            }
        }
    }

    /* Cleanup */
    pio_manager_stop_capture(&pio_config);

    /* Disable watchdog */
    volatile uint32_t *watchdog_ctrl = (volatile uint32_t *)0x40058000;
    *watchdog_ctrl &= ~(1 << 30);

    /* Signal completion to Core0 (if it's listening) */
    if (multicore_fifo_wready())
    {
        multicore_fifo_push_blocking(0x69696969);
    }
}

/* Controller interface functions */
void capture_controller_init_v2(capture_controller_t *controller,
                                 keystroke_queue_t *keystroke_queue)
{
    controller->speed = CAPTURE_SPEED_LOW;
    controller->running = false;

    /* Set global queue for Core1 access */
    g_keystroke_queue_v2 = keystroke_queue;
    g_capture_speed_v2 = CAPTURE_SPEED_LOW;
    g_capture_running_v2 = false;
}

void capture_controller_set_speed_v2(capture_controller_t *controller,
                                      capture_speed_t speed)
{
    controller->speed = speed;
    g_capture_speed_v2 = speed;
}

capture_speed_t capture_controller_get_speed_v2(capture_controller_t *controller)
{
    return controller->speed;
}

bool capture_controller_is_running_v2(capture_controller_t *controller)
{
    return g_capture_running_v2;
}

void capture_controller_start_v2(capture_controller_t *controller)
{
    g_capture_running_v2 = true;
    controller->running = true;
}

void capture_controller_stop_v2(capture_controller_t *controller)
{
    g_capture_running_v2 = false;
    controller->running = false;
    /* Send stop signal to Core1 (non-blocking) */
    if (multicore_fifo_wready())
    {
        multicore_fifo_push_blocking(0xDEADBEEF);
    }
}

}
