/*
 * Morse logging API
 *
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * Macro for printing a @c uint64_t as two separate @c uint32_t values. This is to allow printing of
 * these values even when the @c printf implementation doesn't support it.
 */
#define MM_X64_VAL(value) ((uint32_t)(value >> 32)), ((uint32_t)value)

/** Macro for format specifier to print @ref MM_X64_VAL */
#define MM_X64_FMT "%08lx%08lx"

/**
 * Macro for printing a MAC address. This saves writing it out by hand.
 *
 * Must be used in conjunction with @ref MM_MAC_ADDR_FMT. For example:
 *
 * @code
 * uint8_t mac_addr[] = { 0, 1, 2, 3, 4, 5 };
 * printf("MAC address: " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(mac_addr));
 * @endcode
 */
#define MM_MAC_ADDR_VAL(value) ((value)[0]), ((value)[1]), ((value)[2]), ((value)[3]), ((value)[4]), ((value)[5])

/** Macro for format specifier to print @ref MM_MAC_ADDR_VAL */
#define MM_MAC_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"

/**
 * Initialize Morse logging API.
 *
 * This should be invoked after OS initialization since it will create a mutex for
 * logging.
 */
void mm_logging_init(void);

/**
 * Dumps a binary buffer in hex.
 *
 * @param level         A single character indicating log level.
 * @param function      Name of function this was invoked from.
 * @param line_number   Line number this was invoked from.
 * @param title         Title of the buffer.
 * @param buf           The buffer to dump.
 * @param len           Length of the buffer.
 */
void mm_hexdump(char level, const char *function, unsigned line_number, const char *title, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
