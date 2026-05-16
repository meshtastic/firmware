/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMCRC Morse Micro Cyclic Redundancy Check (mmcrc) API
 *
 * This API provides support for CRC algorithms used by Morse Micro code.
 *
 * @{
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute the CRC-16 for the data buffer using the XMODEM model.
 *
 * @param crc       Seed for CRC calc, zero in most cases this is zero (0). If chaining calls
 *                  then this is the output from the previous invocation.
 * @param data      Pointer to the start of the data to calculate the crc over.
 * @param data_len  Length of the data array in bytes.
 *
 * @return Returns the CRC value.
 */
uint16_t mmcrc_16_xmodem(uint16_t crc, const void *data, size_t data_len);

#ifdef __cplusplus
}
#endif

/** @} */
