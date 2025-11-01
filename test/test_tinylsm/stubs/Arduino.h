#pragma once

// Stub Arduino.h for native testing
// Provides minimal types and functions needed to compile without Arduino

#include <cstddef>
#include <cstdint>
#include <ctime>

// Standard integer types (these are typically defined by stdint.h, but we ensure they exist)
#ifndef uint8_t
typedef std::uint8_t uint8_t;
#endif
#ifndef uint16_t
typedef std::uint16_t uint16_t;
#endif
#ifndef uint32_t
typedef std::uint32_t uint32_t;
#endif
#ifndef uint64_t
typedef std::uint64_t uint64_t;
#endif
#ifndef int8_t
typedef std::int8_t int8_t;
#endif
#ifndef int16_t
typedef std::int16_t int16_t;
#endif
#ifndef int32_t
typedef std::int32_t int32_t;
#endif
#ifndef int64_t
typedef std::int64_t int64_t;
#endif

// Arduino-like functions stubs
inline unsigned long millis()
{
    // Return milliseconds since epoch (for testing purposes)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL;
}
