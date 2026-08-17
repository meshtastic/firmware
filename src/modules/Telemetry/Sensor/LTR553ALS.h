#pragma once

#include <stdint.h>

namespace ltr553
{
constexpr float luxFromChannels(uint16_t channel0, uint16_t channel1, float gain = 1.0f,
                                float integrationTimeMs = 100.0f)
{
    if (gain <= 0.0f || integrationTimeMs <= 0.0f)
        return 0.0f;

    const uint32_t channelSum = static_cast<uint32_t>(channel0) + channel1;
    if (channelSum == 0)
        return 0.0f;

    const float ratio = static_cast<float>(channel1) / static_cast<float>(channelSum);
    float lux = 0.0f;

    if (ratio < 0.45f) {
        lux = 1.7743f * channel0 + 1.1059f * channel1;
    } else if (ratio < 0.64f) {
        lux = 4.2785f * channel0 - 1.9548f * channel1;
    } else if (ratio < 0.85f) {
        lux = 0.5926f * channel0 + 0.1185f * channel1;
    }

    if (lux <= 0.0f)
        return 0.0f;

    return lux * (100.0f / integrationTimeMs) / gain;
}
} // namespace ltr553
