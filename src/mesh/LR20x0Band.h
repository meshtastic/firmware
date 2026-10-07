#pragma once

#include <cstdint>

constexpr bool isLr20x0HighBand(float frequencyMHz)
{
    return frequencyMHz > 1500.0f;
}

// Band the board LF PA table (LR2021_CUSTOM_PA_TABLE) is calibrated for; elsewhere RadioLib's default table applies.
constexpr bool isLr20x0CustomLfPaBand(float frequencyMHz)
{
    return frequencyMHz >= 500.0f && frequencyMHz <= 1000.0f;
}

constexpr bool isLr20x0BandHop(float previousMHz, float requestedMHz)
{
    return previousMHz > 0.0f && requestedMHz > 0.0f && isLr20x0HighBand(previousMHz) != isLr20x0HighBand(requestedMHz);
}

// Path taken by LR20x0Interface::reconfigure() for a frequency change.
enum class Lr20x0ReconfigurePath : uint8_t { Incremental, FullBegin };

constexpr Lr20x0ReconfigurePath lr20x0ReconfigurePath(float previousMHz, float requestedMHz)
{
    return isLr20x0BandHop(previousMHz, requestedMHz) ? Lr20x0ReconfigurePath::FullBegin : Lr20x0ReconfigurePath::Incremental;
}
