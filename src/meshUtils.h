#pragma once
#include "DebugConfiguration.h"
#include <algorithm>
#include <cstdarg>
#include <iterator>
#include <stdint.h>

/// C++ v17+ clamp function, limits a given value to a range defined by lo and hi
template <class T> constexpr const T &clamp(const T &v, const T &lo, const T &hi)
{
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

/// Return the smallest power of 2 >= n (undefined for n > 2^31)
static inline uint32_t nextPowerOf2(uint32_t n)
{
    if (n <= 1)
        return 1;
#if defined(__GNUC__)
    return 1U << (32 - __builtin_clz(n - 1));
#else
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
#endif
}

#if HAS_SCREEN
#define IF_SCREEN(X)                                                                                                             \
    if (screen) {                                                                                                                \
        X;                                                                                                                       \
    }
#else
#define IF_SCREEN(...)
#endif

#if (defined(ARCH_PORTDUINO) && !defined(STRNSTR))
#define STRNSTR
#include <string.h>
char *strnstr(const char *s, const char *find, size_t slen);
#endif

void printBytes(const char *label, const uint8_t *p, size_t numbytes);

// is the memory region filled with a single character?
bool memfll(const uint8_t *mem, uint8_t find, size_t numbytes);

bool isOneOf(int item, int count, ...);

const std::string vformat(const char *const zcFormat, ...);

// Get actual string length for nanopb char array fields.
size_t pb_string_length(const char *str, size_t max_len);

#define IS_ONE_OF(item, ...) isOneOf(item, sizeof((int[]){__VA_ARGS__}) / sizeof(int), __VA_ARGS__)
