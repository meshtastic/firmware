#pragma once

// The mesh radio wire format is little-endian. These helpers convert between
// host and wire byte order: byte swaps on big-endian hosts, no-ops otherwise.

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
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
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
#else
#error "Unable to determine target byte order: __BYTE_ORDER__ is not defined by this compiler"
#endif
