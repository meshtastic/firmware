#pragma once
// File-scope constants and helpers shared by the StoreForwardPlusPlusModule source files.
#if __has_include("sqlite3.h")

#include "StoreForwardPlusPlus.h"

// One enumeration exchange is retried by the next advert tick; this is how long it may stay in flight.
static constexpr uint32_t kEnumExchangeTimeoutMs = 15 * 60 * 1000;

// One item frame per drain keeps the radio's 16-entry TX queue from overflowing on a large request.
static constexpr uint32_t kEnumDrainMinMs = 100;

static inline uint32_t scratchSweepMs()
{
    const uint32_t minutes = portduino_config.sfpp_scratch_sweep < 1 ? 1 : portduino_config.sfpp_scratch_sweep;
    return minutes * 60 * 1000;
}

static inline uint32_t enumDrainIntervalMs()
{
    const uint32_t configured = portduino_config.sfpp_enum_drain_ms;
    return configured < kEnumDrainMinMs ? kEnumDrainMinMs : configured;
}

#endif // has include sqlite3
