#pragma once

/**
 * Portable little-endian byte-swap utilities for the Meshtastic mesh protocol.
 *
 * The radio packet header fields (from, to, id) are transmitted in little-endian
 * byte order. On little-endian hosts (ARM, x86) these are no-ops. On big-endian
 * hosts (MIPS, PowerPC) they perform the necessary byte swap.
 *
 * Endianness detection follows the triple-check pattern from lfs_util.h,
 * with a manual byte-shift fallback for non-GCC/Clang toolchains.
 *
 * Fixes: https://github.com/meshtastic/firmware/issues/6764
 */

#include <stdint.h>

static inline uint32_t meshHtoLe32(uint32_t a)
{
#if (defined(BYTE_ORDER) && defined(ORDER_LITTLE_ENDIAN) && BYTE_ORDER == ORDER_LITTLE_ENDIAN) ||       \
    (defined(__BYTE_ORDER) && defined(__ORDER_LITTLE_ENDIAN) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return a;
#elif (defined(BYTE_ORDER) && defined(ORDER_BIG_ENDIAN) && BYTE_ORDER == ORDER_BIG_ENDIAN) ||       \
    (defined(__BYTE_ORDER) && defined(__ORDER_BIG_ENDIAN) && __BYTE_ORDER == __ORDER_BIG_ENDIAN) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return __builtin_bswap32(a);
#else
    return (((uint8_t *)&a)[0] << 0) | (((uint8_t *)&a)[1] << 8) | (((uint8_t *)&a)[2] << 16) | (((uint8_t *)&a)[3] << 24);
#endif
}

static inline uint32_t meshLe32toH(uint32_t a)
{
    return meshHtoLe32(a);
}

static inline uint64_t meshHtoLe64(uint64_t a)
{
#if (defined(BYTE_ORDER) && defined(ORDER_LITTLE_ENDIAN) && BYTE_ORDER == ORDER_LITTLE_ENDIAN) ||       \
    (defined(__BYTE_ORDER) && defined(__ORDER_LITTLE_ENDIAN) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return a;
#elif (defined(BYTE_ORDER) && defined(ORDER_BIG_ENDIAN) && BYTE_ORDER == ORDER_BIG_ENDIAN) ||       \
    (defined(__BYTE_ORDER) && defined(__ORDER_BIG_ENDIAN) && __BYTE_ORDER == __ORDER_BIG_ENDIAN) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return __builtin_bswap64(a);
#else
    return (((uint64_t)((uint8_t *)&a)[0]) << 0) | (((uint64_t)((uint8_t *)&a)[1]) << 8) |
           (((uint64_t)((uint8_t *)&a)[2]) << 16) | (((uint64_t)((uint8_t *)&a)[3]) << 24) |
           (((uint64_t)((uint8_t *)&a)[4]) << 32) | (((uint64_t)((uint8_t *)&a)[5]) << 40) |
           (((uint64_t)((uint8_t *)&a)[6]) << 48) | (((uint64_t)((uint8_t *)&a)[7]) << 56);
#endif
}

static inline uint64_t meshLe64toH(uint64_t a)
{
    return meshHtoLe64(a);
}
