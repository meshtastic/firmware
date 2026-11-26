#pragma once
// File-scope constants and helpers shared by the StoreForwardPlusPlusModule source files.
#if __has_include("sqlite3.h")

#include "StoreForwardPlusPlus.h"
#include "UptimeClock.h"

#include <time.h>

// G19 ages rows by uptime, and Linux millis() follows the wall clock (an NTP step moved it by years on the bench),
// so SF++ reads the monotonic clock; a unit test's clock still wins.
static inline uint32_t sfppUptimeSecs()
{
#ifdef PIO_UNIT_TESTING
    if (Time::useTestClock.load(std::memory_order_relaxed))
        return Time::getUptimeSecs();
#endif
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static const time_t start = ts.tv_sec;
    return (uint32_t)(ts.tv_sec - start);
}

// One enumeration exchange is retried by the next advert tick; this is how long it may stay in flight.
static constexpr uint32_t kEnumExchangeTimeoutMs = 15 * 60 * 1000;

// One item frame per drain keeps the radio's 16-entry TX queue from overflowing on a large request.
static constexpr uint32_t kEnumDrainMinMs = 100;
static constexpr size_t kEnumTxQueueMax = 256;
static constexpr size_t kEnumMaxRequesters = 16;
static constexpr uint32_t kEnumRetryMs = 30 * 1000;
static constexpr uint32_t kEnumRequesterTtlMs = 2 * kEnumExchangeTimeoutMs;
static constexpr uint32_t kEnumFullRootEvery = 10;
static constexpr size_t kEnumPendingMax = 8;
static constexpr uint32_t kEnumLateAcceptMs = 5 * 60 * 1000;

// G19: a proper clock is NTP or better. Without one, a row's age carries an uncertainty at the newest
// end; an unknown reboot gap widens it to the retention horizon, past which the row is already gone.
static constexpr uint32_t kTimeMarginS = 300;
static constexpr uint8_t kTimeBasisEpoch = 0;
static constexpr uint8_t kTimeBasisUptime = 1;
static constexpr uint8_t kTimeBasisAge = 2;

static inline uint32_t enumDrainIntervalMs()
{
    const uint32_t configured = portduino_config.sfpp_enum_drain_ms;
    return configured < kEnumDrainMinMs ? kEnumDrainMinMs : configured;
}

#endif // has include sqlite3
