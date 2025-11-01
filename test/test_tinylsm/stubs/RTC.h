#pragma once

// Stub RTC.h for native testing
// Provides getTime() function using standard time library

#include <cstdint>
#include <ctime>

// Return time since 1970 in seconds (Unix epoch)
inline uint32_t getTime(bool local = false)
{
    (void)local; // Suppress unused parameter warning
    std::time_t now = std::time(nullptr);
    return static_cast<uint32_t>(now);
}
