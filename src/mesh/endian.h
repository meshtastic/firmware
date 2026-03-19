#pragma once

/**
 * Portable little-endian byte-swap utilities for the Meshtastic mesh protocol.
 *
 * The radio packet header fields (from, to, id) are transmitted in little-endian
 * byte order. On little-endian hosts (ARM, x86) these are no-ops. On big-endian
 * hosts (MIPS, PowerPC) they perform the necessary byte swap.
 *
 * Fixes: https://github.com/meshtastic/firmware/issues/6764
 */

#include <stdint.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define MESH_IS_BIG_ENDIAN 1
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__MIPSEB__)
#define MESH_IS_BIG_ENDIAN 1
#else
#define MESH_IS_BIG_ENDIAN 0
#endif

#if MESH_IS_BIG_ENDIAN

static inline uint32_t mesh_htole32(uint32_t v)
{
    return __builtin_bswap32(v);
}

static inline uint32_t mesh_le32toh(uint32_t v)
{
    return __builtin_bswap32(v);
}

static inline uint64_t mesh_htole64(uint64_t v)
{
    return __builtin_bswap64(v);
}

static inline uint64_t mesh_le64toh(uint64_t v)
{
    return __builtin_bswap64(v);
}

#else /* Little-endian — no-ops */

static inline uint32_t mesh_htole32(uint32_t v) { return v; }
static inline uint32_t mesh_le32toh(uint32_t v) { return v; }
static inline uint64_t mesh_htole64(uint64_t v) { return v; }
static inline uint64_t mesh_le64toh(uint64_t v) { return v; }

#endif
