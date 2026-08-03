#pragma once

constexpr bool isLr20x0HighBand(float frequencyMHz)
{
    return frequencyMHz > 1500.0f;
}

constexpr bool isLr20x0BandHop(float previousMHz, float requestedMHz)
{
    return previousMHz > 0.0f && isLr20x0HighBand(previousMHz) != isLr20x0HighBand(requestedMHz);
}
