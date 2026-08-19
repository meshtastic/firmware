#pragma once

#include "input/TouchTargetRegistry.h"

namespace graphics
{

inline meshtastic::TouchRect touchExpandedRect(int left, int top, int width, int height, int margin)
{
    return {static_cast<int16_t>(left - margin), static_cast<int16_t>(top - margin),
            static_cast<int16_t>(left + width + margin), static_cast<int16_t>(top + height + margin)};
}

} // namespace graphics
