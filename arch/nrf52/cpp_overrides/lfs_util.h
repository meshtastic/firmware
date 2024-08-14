/*
 * lfs utility functions
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// MESHTASTIC/@geeksville note: This file is copied from the Adafruit nrf52 arduino lib.  And we use a special -include in
// nrf52.ini to load it before EVERY file we do this hack because the default definitions for LFS_ASSERT are quite poor and we
// don't want to fork the adafruit lib (again) and send in a PR that they probably won't merge anyways. This file might break if
// they ever update lfs.util on their side, in which case we'll need to update this file to match their new version. The version
// this is a copy from is almost exactly
// https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/c25d93268a3b9c23e9a1ccfcaf9b208beca624ca/libraries/Adafruit_LittleFS/src/littlefs/lfs_util.h

#ifndef LFS_UTIL_H
#define LFS_UTIL_H

// Users can override lfs_util.h with their own configuration by defining
// LFS_CONFIG as a header file to include (-DLFS_CONFIG=lfs_config.h).
//
// If LFS_CONFIG is used, none of the default utils will be emitted and must be
// provided by the config file. To start I would suggest copying lfs_util.h and
// modifying as needed.
#ifdef LFS_CONFIG
#define LFS_STRINGIZE(x) LFS_STRINGIZE2(x)
#define LFS_STRINGIZE2(x) #x
#include LFS_STRINGIZE(LFS_CONFIG)
#else

// System includes
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef LFS_NO_MALLOC
#include <stdlib.h>
#endif
#ifndef LFS_NO_ASSERT
#include <assert.h>
#endif

#if !defined(LFS_NO_DEBUG) || !defined(LFS_NO_WARN) || !defined(LFS_NO_ERROR)
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Macros, may be replaced by system specific wrappers. Arguments to these
// macros must not have side-effects as the macros can be removed for a smaller
// code footprint

// Logging functions
#ifndef LFS_NO_DEBUG

void logLegacy(const char *level, const char *fmt, ...);
#define LFS_DEBUG(fmt, ...) logLegacy("DEBUG", "lfs debug:%d: " fmt "\n", __LINE__, __VA_ARGS__)
#else
#define LFS_DEBUG(fmt, ...)
#endif

#ifndef LFS_NO_WARN
#define LFS_WARN(fmt, ...) logLegacy("WARN", "lfs warn:%d: " fmt "\n", __LINE__, __VA_ARGS__)
#else
#define LFS_WARN(fmt, ...)
#endif

#ifndef LFS_NO_ERROR
#define LFS_ERROR(fmt, ...) logLegacy("ERROR", "lfs error:%d: " fmt "\n", __LINE__, __VA_ARGS__)
#else
#define LFS_ERROR(fmt, ...)
#endif

// Runtime assertions
#ifndef LFS_NO_ASSERT
#define LFS_ASSERT(test) assert(test)
#else
extern void lfs_assert(const char *reason);
#define LFS_ASSERT(test)                                                                                                         \
    if (!(test))                                                                                                                 \
    lfs_assert(#test)
#endif

// Builtin functions, these may be replaced by more efficient
// toolchain-specific implementations. LFS_NO_INTRINSICS falls back to a more
// expensive basic C implementation for debugging purposes

// Min/max functions for unsigned 32-bit numbers
static inline uint32_t lfs_max(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

static inline uint32_t lfs_min(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

// Find the next smallest power of 2 less than or equal to a
static inline uint32_t lfs_npw2(uint32_t a)
{
#if !defined(LFS_NO_INTRINSICS) && (defined(__GNUC__) || defined(__CC_ARM))
    return 32 - __builtin_clz(a - 1);
#else
    uint32_t r = 0;
    uint32_t s;
    a -= 1;
    s = (a > 0xffff) << 4;
    a >>= s;
    r |= s;
    s = (a > 0xff) << 3;
    a >>= s;
    r |= s;
    s = (a > 0xf) << 2;
    a >>= s;
    r |= s;
    s = (a > 0x3) << 1;
    a >>= s;
    r |= s;
    return (r | (a >> 1)) + 1;
#endif
}

// Count the number of trailing binary zeros in a
// lfs_ctz(0) may be undefined
static inline uint32_t lfs_ctz(uint32_t a)
{
#if !defined(LFS_NO_INTRINSICS) && defined(__GNUC__)
    return __builtin_ctz(a);
#else
    return lfs_npw2((a & -a) + 1) - 1;
#endif
}

// Count the number of binary ones in a
static inline uint32_t lfs_popc(uint32_t a)
{
#if !defined(LFS_NO_INTRINSICS) && (defined(__GNUC__) || defined(__CC_ARM))
    return __builtin_popcount(a);
#else
    a = a - ((a >> 1) & 0x55555555);
    a = (a & 0x33333333) + ((a >> 2) & 0x33333333);
    return (((a + (a >> 4)) & 0xf0f0f0f) * 0x1010101) >> 24;
#endif
}

// Find the sequence comparison of a and b, this is the distance
// between a and b ignoring overflow
static inline int lfs_scmp(uint32_t a, uint32_t b)
{
    return (int)(unsigned)(a - b);
}

// Convert from 32-bit little-endian to native order
static inline uint32_t lfs_fromle32(uint32_t a)
{
#if !defined(LFS_NO_INTRINSICS) && ((defined(BYTE_ORDER) && BYTE_ORDER == ORDER_LITTLE_ENDIAN) ||                                \
                                    (defined(__BYTE_ORDER) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN) ||                          \
                                    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
    return a;
#elif !defined(LFS_NO_INTRINSICS) &&                                                                                             \
    ((defined(BYTE_ORDER) && BYTE_ORDER == ORDER_BIG_ENDIAN) || (defined(__BYTE_ORDER) && __BYTE_ORDER == __ORDER_BIG_ENDIAN) || \
     (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
    return __builtin_bswap32(a);
#else
    return (((uint8_t *)&a)[0] << 0) | (((uint8_t *)&a)[1] << 8) | (((uint8_t *)&a)[2] << 16) | (((uint8_t *)&a)[3] << 24);
#endif
}

// Convert to 32-bit little-endian from native order
static inline uint32_t lfs_tole32(uint32_t a)
{
    return lfs_fromle32(a);
}

// Calculate CRC-32 with polynomial = 0x04c11db7
void lfs_crc(uint32_t *crc, const void *buffer, size_t size);

// Allocate memory, only used if buffers are not provided to littlefs
static inline void *lfs_malloc(size_t size)
{
#ifndef LFS_NO_MALLOC
    extern void *pvPortMalloc(size_t xWantedSize);
    return pvPortMalloc(size);
#else
    (void)size;
    return NULL;
#endif
}

// Deallocate memory, only used if buffers are not provided to littlefs
static inline void lfs_free(void *p)
{
#ifndef LFS_NO_MALLOC
    extern void vPortFree(void *pv);
    vPortFree(p);
#else
    (void)p;
#endif
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
#endif