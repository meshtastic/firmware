#pragma once

#include "MeshTypes.h"
#include <cstdint>

namespace event_mode
{
constexpr bool isValidHopLimit(int value)
{
    return value >= 0 && value <= HOP_MAX;
}

#if defined(USERPREFS_EVENT_MODE) && USERPREFS_EVENT_MODE && defined(USERPREFS_EVENT_MODE_HOP_LIMIT)
static_assert(isValidHopLimit(USERPREFS_EVENT_MODE_HOP_LIMIT), "USERPREFS_EVENT_MODE_HOP_LIMIT must be between 0 and 7");
constexpr uint8_t hopLimit = static_cast<uint8_t>(USERPREFS_EVENT_MODE_HOP_LIMIT);
#else
constexpr uint8_t hopLimit = HOP_RELIABLE;
#endif

constexpr uint8_t relayHopLimitFor(uint8_t value)
{
    return value > 0 ? value - 1 : 0;
}

constexpr uint8_t relayHopLimit = relayHopLimitFor(hopLimit);
} // namespace event_mode
