#pragma once

/**
 * Little-endian byte-swap utilities for mesh radio packet headers.
 *
 * The mesh radio wire format is little-endian. On big-endian hosts
 * (e.g. Linux native on MIPS or PowerPC routers) these helpers swap
 * bytes; on all little-endian targets (ESP32, nRF52, STM32, RP2040,
 * x86, aarch64) they are no-ops.
 *
 * Uses compiler builtins instead of <endian.h> because htole32() is
 * not declared on every libc/toolchain combination (reported on an
 * OpenWRT MIPS toolchain). Same detection pattern as
 * src/platform/stm32wl/littlefs/lfs_util.h.
 *
 * Fixes: https://github.com/meshtastic/firmware/issues/6764
 */

#include <stdint.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
static inline uint32_t meshHtoLe32(uint32_t v)
{
    return __builtin_bswap32(v);
}
static inline uint32_t meshLe32toH(uint32_t v)
{
    return __builtin_bswap32(v);
}
static inline uint64_t meshHtoLe64(uint64_t v)
{
    return __builtin_bswap64(v);
}
static inline uint64_t meshLe64toH(uint64_t v)
{
    return __builtin_bswap64(v);
}
#else
// Little-endian host: no conversion needed.
static inline uint32_t meshHtoLe32(uint32_t v)
{
    return v;
}
static inline uint32_t meshLe32toH(uint32_t v)
{
    return v;
}
static inline uint64_t meshHtoLe64(uint64_t v)
{
    return v;
}
static inline uint64_t meshLe64toH(uint64_t v)
{
    return v;
}
#endif
