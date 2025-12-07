/**
 * @file common.h
 * @brief Common definitions for USB capture module
 *
 * Consolidated from: config.h, types.h, stats.h, cpu_monitor.h
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef USB_CAPTURE_COMMON_H
#define USB_CAPTURE_COMMON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * GPIO PIN DEFINITIONS - RP2350 Pico
 * ============================================================================ */
/*
 * CRITICAL CONSTRAINTS:
 * 1. These THREE pins MUST be consecutive: DP, DM, START
 * 2. PIO0 uses: pio_sm_set_consecutive_pindirs(pio0, pio0_sm, DP_INDEX, 3, false)
 *    This configures DP_INDEX, DP_INDEX+1, DP_INDEX+2 as the signal trio
 */

/* USB Capture Pins - RP2350 board configuration */
#define DP_INDEX           16    /* USB D+ (GPIO 16) */
#define DM_INDEX           17    /* USB D- = DP_INDEX+1 (GPIO 17) */
#define START_INDEX        18    /* PIO sync = DP_INDEX+2 (GPIO 18) */

/* ============================================================================
 * USB PROTOCOL CONSTANTS
 * ============================================================================ */
#define USB_LOW_SPEED_SYNC     0x81
#define USB_FULL_SPEED_SYNC    0x80
#define USB_CRC16_RESIDUAL     0xb001

/* ============================================================================
 * PERFORMANCE TUNING
 * ============================================================================ */
#define KEYBOARD_PACKET_MIN_SIZE   10
#define KEYBOARD_PACKET_MAX_SIZE   64

/* ============================================================================
 * CAPTURE ERROR FLAGS
 * ============================================================================ */
#define CAPTURE_ERROR_STUFF    (1 << 31)
#define CAPTURE_ERROR_CRC      (1 << 30)
#define CAPTURE_ERROR_PID      (1 << 29)
#define CAPTURE_ERROR_SYNC     (1 << 28)
#define CAPTURE_ERROR_NBIT     (1 << 27)
#define CAPTURE_ERROR_SIZE     (1 << 26)
#define CAPTURE_RESET          (1 << 25)
#define CAPTURE_ERROR_MASK     (CAPTURE_ERROR_STUFF | CAPTURE_ERROR_CRC | \
                                CAPTURE_ERROR_PID | CAPTURE_ERROR_SYNC | \
                                CAPTURE_ERROR_NBIT | CAPTURE_ERROR_SIZE)
#define CAPTURE_SIZE_MASK      0xffff

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

/**
 * @brief USB capture speed modes
 */
typedef enum {
    CAPTURE_SPEED_LOW = 0,   /**< Low speed USB (1.5 Mbps) */
    CAPTURE_SPEED_FULL = 1   /**< Full speed USB (12 Mbps) */
} capture_speed_t;

/**
 * @brief USB PID types
 */
typedef enum {
    PID_RESERVED = 0,
    PID_DATA0 = 3,
    PID_IN = 9,
    PID_DATA1 = 11
} usb_pid_t;

/**
 * @brief Keyboard state tracking structure
 */
typedef struct {
    uint8_t prev_keys[6];
    uint8_t prev_modifier;
} keyboard_state_t;

/**
 * @brief Capture controller structure (V2 architecture)
 */
typedef struct {
    capture_speed_t speed;
    bool running;
} capture_controller_t;

/**
 * @brief Keystroke event types
 */
typedef enum {
    KEYSTROKE_TYPE_CHAR = 0,        /**< Printable character */
    KEYSTROKE_TYPE_BACKSPACE = 1,   /**< Backspace key */
    KEYSTROKE_TYPE_ENTER = 2,       /**< Enter/Return key */
    KEYSTROKE_TYPE_TAB = 3,         /**< Tab key */
    KEYSTROKE_TYPE_ERROR = 4,       /**< Error event */
    KEYSTROKE_TYPE_RESET = 5        /**< Reset marker */
} keystroke_type_t;

/**
 * @brief Full keystroke event (32 bytes)
 */
typedef struct {
    keystroke_type_t type;           /**< Event type */
    char character;                  /**< ASCII character */
    uint8_t scancode;                /**< HID scancode */
    uint8_t modifier;                /**< HID modifier byte */
    uint64_t capture_timestamp_us;   /**< USB packet capture time */
    uint64_t queue_timestamp_us;     /**< Queue insertion time */
    uint32_t processing_latency_us;  /**< Processing latency */
    uint32_t error_flags;            /**< Error flags */
} keystroke_event_t;

/* Compile-time size validation */
#ifdef __cplusplus
static_assert(sizeof(keystroke_event_t) == 32, "keystroke_event_t MUST be exactly 32 bytes");
#else
_Static_assert(sizeof(keystroke_event_t) == 32, "keystroke_event_t MUST be exactly 32 bytes");
#endif

/* ============================================================================
 * STATISTICS FUNCTIONS (Stubs)
 * ============================================================================ */

/* Minimal stub functions - can be expanded later */
static inline void stats_increment_overflow(void) {}
static inline void stats_increment_stuff_error(void) {}
static inline void stats_increment_crc_error(void) {}
static inline void stats_increment_pid_error(void) {}
static inline void stats_increment_sync_error(void) {}
static inline void stats_increment_size_error(void) {}
static inline void stats_record_packet(uint32_t bytes) { (void)bytes; }
static inline void stats_update_core1_capture_time(uint64_t us) { (void)us; }
static inline void stats_update_core1_idle_time(uint64_t us) { (void)us; }

/* ============================================================================
 * CPU MONITORING FUNCTIONS (Stubs)
 * ============================================================================ */

/* Minimal stub functions - can be expanded later */
static inline void cpu_monitor_record_core1_work(void) {}

#ifdef __cplusplus
}
#endif

#endif /* USB_CAPTURE_COMMON_H */
