#pragma once

#include "MeshTypes.h"
#include <cstdint>
#include <type_traits>

namespace event_mode
{
template <typename T> constexpr bool isValidHopLimit(T value)
{
    using Value = typename std::remove_cv<T>::type;
    if constexpr (!std::is_integral<Value>::value || std::is_same<Value, bool>::value) {
        return false;
    } else if constexpr (std::is_signed<Value>::value) {
        return value >= 0 && value <= HOP_MAX;
    } else {
        return value <= HOP_MAX;
    }
}

#if defined(USERPREFS_EVENT_MODE) && USERPREFS_EVENT_MODE && defined(USERPREFS_EVENT_MODE_HOP_LIMIT)
static_assert(isValidHopLimit(USERPREFS_EVENT_MODE_HOP_LIMIT),
              "USERPREFS_EVENT_MODE_HOP_LIMIT must be an integer between 0 and 7");
constexpr uint8_t hopLimit = static_cast<uint8_t>(USERPREFS_EVENT_MODE_HOP_LIMIT);
#else
constexpr uint8_t hopLimit = HOP_RELIABLE;
#endif

constexpr uint8_t relayHopLimitFor(uint8_t value)
{
    return value > 0 ? value - 1 : 0;
}

constexpr uint8_t capHopLimitFor(uint8_t value, uint8_t maximum)
{
    return value > maximum ? maximum : value;
}

struct RelayHopFields {
    uint8_t hopStart;
    uint8_t hopLimit;
};

constexpr RelayHopFields capRelayHopFieldsFor(uint8_t hopStart, uint8_t currentHopLimit, uint8_t eventHopLimit)
{
    const uint8_t cappedHopLimit = capHopLimitFor(currentHopLimit, relayHopLimitFor(eventHopLimit));
    const uint8_t reduction = currentHopLimit - cappedHopLimit;
    return {reduction <= hopStart ? static_cast<uint8_t>(hopStart - reduction) : static_cast<uint8_t>(0), cappedHopLimit};
}

constexpr uint8_t relayHopLimit = relayHopLimitFor(hopLimit);
constexpr uint8_t capHopLimit(uint8_t value)
{
    return capHopLimitFor(value, hopLimit);
}

constexpr RelayHopFields capRelayHopFields(uint8_t hopStart, uint8_t currentHopLimit)
{
    return capRelayHopFieldsFor(hopStart, currentHopLimit, hopLimit);
}
} // namespace event_mode
