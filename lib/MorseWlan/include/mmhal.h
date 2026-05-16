/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMHAL Morse Micro Hardware Abstraction Layer (mmhal) API
 *
 * This API provides abstraction from the underlying hardware/BSP.
 *
 * @{
 */

#pragma once

#include "mmhal_flash.h"
#include "mmhal_wlan.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialization before RTOS scheduler starts. */
void mmhal_early_init(void);

/** Initialization after RTOS scheduler started. */
void mmhal_init(void);

/** Enumeration of ISR states (i.e., whether in ISR or not). */
enum mmhal_isr_state {
    MMHAL_NOT_IN_ISR,        /**< The function was not executed from ISR context. */
    MMHAL_IN_ISR,            /**< The function was executed from ISR context. */
    MMHAL_ISR_STATE_UNKNOWN, /**< The HAL does not support checking ISR state. */
};

/**
 * Enumeration for different LED's on the board.
 *
 * @note Some of these LED's may not be available on all boards and some of these values
 *       may refer to the same LED.
 */
enum mmhal_led_id { LED_RED, LED_GREEN, LED_BLUE, LED_WHITE };

/** Enumeration of MCU sleep state. */
enum mmhal_sleep_state {
    MMHAL_SLEEP_DISABLED, /**< Disable MCU sleep. */
    MMHAL_SLEEP_SHALLOW,  /**< MCU to enter shallow sleep. */
    MMHAL_SLEEP_DEEP,     /**< MCU can enter deep sleep. */
};

/** A value of 0 turns OFF an LED */
#define LED_OFF 0

/**
 * A value of 255 turns an LED ON fully.
 *
 * Some boards support varying an LED's brightness. For these boards a value between 1 and 255
 * will result in proportionately varying levels of brightness. LED's that do not have a
 * brightness control feature will just turn ON fully for any non zero value.
 */
#define LED_ON 255

/**
 * Get the current ISR state  (i.e., whether in ISR or not).
 *
 * @returns the current ISR state, or @c MMHAL_ISR_STATE_UNKNOWN if the HAL does not support
 *          checking ISR state.
 */
enum mmhal_isr_state mmhal_get_isr_state(void);

/**
 * Write to the debug log.
 *
 * It is assumed the caller will have mechanisms in place to prevent concurrent access.
 *
 * @param data  Buffer containing data to write.
 * @param len   Length of data in buffer.
 */
void mmhal_log_write(const uint8_t *data, size_t len);

/**
 * Flush the debug log before returning.
 *
 * @warning Implementations of this function must support being invoked with interrupts disabled.
 */
void mmhal_log_flush(void);

/**
 * Generate a random 32 bit integer within the given range.
 *
 * @param min   Minimum value (inclusive).
 * @param max   Maximum value (inclusive).
 *
 * @returns a randomly generated integer (min <= i <= max).
 */
uint32_t mmhal_random_u32(uint32_t min, uint32_t max);

/** Reset the microcontroller. */
void mmhal_reset(void);

/**
 * Set the specified LED to the requested level. Do nothing if the requested LED does not exist.
 *
 * @param led   The LED to set, if the platform supports it. See @ref mmhal_led_id
 * @param level The level to set it to. 0 means OFF and non-zero means ON. If the platform supports
 *              brightness levels then 255 is full. Defines @ref LED_ON and @ref LED_OFF are
 *              provided for ease of use.
 */
void mmhal_set_led(uint8_t led, uint8_t level);

/**
 * Set the error LED to the requested state.
 *
 * @note This function is called by the bootloader and so needs to do all the initialization
 *       required to set the LED's as the bootloader does not use the regular BSP initialization
 *       located in main.c for configuring @c GPIO's and setting clock gates as required.
 *
 * @param state Set to true if the LED needs to be turned on,
 *              or false if the led needs to be turned off.
 */
void mmhal_set_error_led(bool state);

/**
 * Enumeration for buttons on the board.
 *
 * @note The support for each button is platform dependent.
 */
enum mmhal_button_id { BUTTON_ID_USER0 };

/**
 * Enumeration for button states
 */
enum mmhal_button_state { BUTTON_RELEASED, BUTTON_PRESSED };

/** Button state callback function prototype. */
typedef void (*mmhal_button_state_cb_t)(enum mmhal_button_id button_id, enum mmhal_button_state button_state);

/**
 * Registers a callback handler for button state changes.
 *
 * @note The callback will be executed in an Interrupt Service Routine context
 *
 * @param button_id The button whose state should be reported to the callback
 * @param button_state_cb The function to call on button state change, or NULL to disable.
 * @returns True if the callback is registered successfully, False if not supported
 */
bool mmhal_set_button_callback(enum mmhal_button_id button_id, mmhal_button_state_cb_t button_state_cb);

/**
 * Returns the registered callback handler for button state changes.
 *
 * @param button_id The button whose callback should be returned
 * @returns The registered callback or NULL if no callback registered
 */
mmhal_button_state_cb_t mmhal_get_button_callback(enum mmhal_button_id button_id);

/**
 * Reads the state of the specified button.
 *
 * @param button_id The button state to read
 * @returns The current button state, or BUTTON_RELEASED if not supported
 */
enum mmhal_button_state mmhal_get_button(enum mmhal_button_id button_id);

/**
 * Reads information that can be used to identify the hardware platform, such as
 * hardware ID and version number, in the form of a user readable string.
 *
 * This function attempts to detect the correct hardware and version.
 * The actual means of detecting the correct hardware and version will vary from
 * implementation to implementation and may use means such as identification
 * information stored in EEPROM or devices detected on GPIO, SPI or I2C interfaces.
 * Returns false if the hardware could not be identified correctly.
 *
 * @param version_buffer The pre-allocated buffer to return the hardware ID and version in.
 * @param version_buffer_length The length of the pre-allocated buffer.
 * @returns              True if the hardware was correctly identified and returned.
 */
bool mmhal_get_hardware_version(char *version_buffer, size_t version_buffer_length);

/**
 * Macro to simplify debug pin mask/value definition.
 *
 * @param _pin_num  The pin number to set in the mask. Must be 0-31 (inclusive).
 *
 * Example:
 *
 *     mmhal_set_debug_pins(MMHAL_DEBUG_PIN(0), MMHAL_DEBUG_PIN(0));
 */
#define MMHAL_DEBUG_PIN(_pin_num) (1ul << (_pin_num))

/** Bit mask with all debug pins selected. */
#define MMHAL_ALL_DEBUG_PINS (UINT32_MAX)

/**
 * Set the value one or more debug pins.
 *
 * Each platform can define up to 32 GPIOs for application use. If a GPIO is not supported
 * by a platform then attempts to set it will be silently ignored. These GPIOs are intended
 * for debug/test purposes.
 *
 * @param mask      Mask of GPIOs to modify. Each bit in this mask that is set will result in
 *                  the corresponding GPIO being being set to the corresponding value given in
 *                  @p values.
 * @param values    Bit field, where each bit corresponds to a GPIO, specifying the direction
 *                  to drive each GPIO with 1 meaning drive high and 0 meaning drive low.
 *                  Only GPIOs with the corresponding bit set in @p mask will be modified.
 *
 * @sa MM_DEBUG_PIN_MASK
 */
void mmhal_set_debug_pins(uint32_t mask, uint32_t values);

/**
 * Returns the time of day as set in the RTC.
 * Time is in UTC.
 *
 * @return    Epoch time (seconds since 1 Jan 1970) or 0 on failure.
 */
time_t mmhal_get_time();

/**
 * Sets the RTC to the specified time in UTC.
 *
 * @note While Unix epoch time supports years from 1970, most Real Time Clock
 *       chips support years from 2000 only as they store the year as years
 *       from 2000. So do not attempt to set any years below 2000 as this could cause
 *       the year to wrap around to an unreasonably high value. Definitely do not do:
 * @code
 * mmhal_set_time(0);
 * @endcode
 *
 * @param epoch Time in Unix epoch time (seconds since 1 Jan 1970).
 */
void mmhal_set_time(time_t epoch);

/**
 * Function to prepare MCU to enter sleep.
 * This will halt the timer that generates the RTOS tick.
 *
 * @param expected_idle_time_ms Expected time to sleep in milliseconds.
 *
 * @returns the type of sleep permitted by the current system state.
 */
enum mmhal_sleep_state mmhal_sleep_prepare(uint32_t expected_idle_time_ms);

/**
 * Function to enter MCU sleep.
 *
 * @param sleep_state             Sleep state to enter into.
 * @param expected_idle_time_ms   Expected time to sleep in milliseconds.
 *
 * @returns Elapsed sleep time in milliseconds.
 */
uint32_t mmhal_sleep(enum mmhal_sleep_state sleep_state, uint32_t expected_idle_time_ms);

/**
 * Function to abort the MCU sleep state.
 *
 * @note This must only be invoked after @ref mmhal_sleep_prepare()
 *       and before @ref mmhal_sleep().
 *
 * @param sleep_state         Sleep state to abort.
 */
void mmhal_sleep_abort(enum mmhal_sleep_state sleep_state);

/**
 * Function to cleanup on exit from the MCU sleep state.
 */
void mmhal_sleep_cleanup(void);

/** Enumeration of veto_id ranges for use with @ref mmhal_set_deep_sleep_veto() and
 *  @ref mmhal_clear_deep_sleep_veto(). */
enum mmhal_veto_id {
    /** Start of deep sleep veto ID range that is available for application use. */
    MMHAL_VETO_ID_APP_MIN = 0,
    /** End of deep sleep veto ID range that is available for application use. */
    MMHAL_VETO_ID_APP_MAX = 7,
    /** Start of deep sleep veto ID range that is available for HAL use. */
    MMHAL_VETO_ID_HAL_MIN = 8,
    /** End of deep sleep veto ID range that is available for HAL use. */
    MMHAL_VETO_ID_HAL_MAX = 15,
    /** Start of deep sleep veto ID range that is allocated for morselib use. Note that this
     *  must not be changed as it is built into morselib. */
    MMHAL_VETO_ID_MORSELIB_MIN = 16,
    /** End of deep sleep veto ID range that is allocated for morselib use. Note that this must not
     *  be changed as it is built into morselib. */
    MMHAL_VETO_ID_MORSELIB_MAX = 19,
    /** Deep sleep veto ID for data-link subsystem. */
    MMHAL_VETO_ID_DATALINK = 20,
    /** Deep sleep veto ID allocated to @ref MMCONFIG. */
    MMHAL_VETO_ID_MMCONFIG = 21,
    /** Start of deep sleep veto ID range reserved for future use. */
    MMHAL_VETO_ID_RESERVED_MIN = 22,
    /** End of deep sleep veto ID range reserved for future use. */
    MMHAL_VETO_ID_RESERVED_MAX = 31,
};

/**
 * Sets a deep sleep veto that will prevent the device from entering deep sleep. The device
 * will not enter deep sleep until there are no vetoes remaining. This veto can be cleared
 * by a call to @ref mmhal_clear_deep_sleep_veto() with the same veto_id.
 *
 * Up to 32 vetoes are supported (ID numbers 0-31). Each veto should be used exclusively by
 * a given aspect of the system (e.g., to prevent deep sleep when a DMA transfer is in progress,
 * or to prevent deep sleep when there is log data buffered for transmit, etc.).
 *
 * @param veto_id   The veto identifier. Valid values are 0-31, and these are split up into ranges
 *                  for use by different subsystems -- see @ref mmhal_veto_id.
 */
void mmhal_set_deep_sleep_veto(uint8_t veto_id);

/**
 * Clears a deep sleep veto that was preventing the device from entering deep sleep (see
 * @ref mmhal_set_deep_sleep_veto()). If the given veto was not already set then this has
 * no effect.
 *
 * @param veto_id   The veto identifier. Valid values are 0-31, and these are split up into ranges
 *                  for use by different subsystems -- see @ref mmhal_veto_id.
 */
void mmhal_clear_deep_sleep_veto(uint8_t veto_id);

#ifdef __cplusplus
}
#endif

/** @} */
