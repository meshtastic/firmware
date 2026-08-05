#pragma once
#ifndef _MT_TIME_H
#define _MT_TIME_H

#include <Arduino.h>

/**
 * Monotonic uptime clock — the single seam for "milliseconds since boot".
 *
 * Why this exists:
 *  - Test injection: any unit test can drive a virtual timebase via setTestMillis() /
 *    advanceTestMillis() instead of sleeping real seconds. One seam for all suites — no
 *    per-module test clock (this replaces the ad-hoc HopScalingModule::s_testNowMs /
 *    TrafficManagementModule::clockMs patterns).
 *  - Rollover immunity: getMillis64() never wraps (uint32 millis() wraps at ~49.7 days, which
 *    routers/repeaters exceed). Use it for any "duration over days" logic.
 *
 * This is uptime, NOT wall-clock. For wall-clock seconds (RTC/GPS/NTP) use getTime() in
 * src/gps/RTC.h — a separate axis that can jump and be unset.
 *
 * Hot/ISR paths use getMillis() (32-bit, a drop-in for millis(), rollover-safe only with the
 * unsigned-subtraction idiom `(uint32_t)(now - then)`). getMillis64() keeps mutable static
 * carry state and is NOT ISR-safe — call it from normal task context only.
 */
namespace Time
{
#ifdef PIO_UNIT_TESTING
// Test-only virtual clock. Default: OFF — getMillis() returns real millis() so suites that
// rely on real time are unaffected by the seam. A test opts in by calling setTestMillis().
inline uint32_t testNowMs = 0;
inline bool useTestClock = false;

inline void setTestMillis(uint32_t ms)
{
    testNowMs = ms;
    useTestClock = true;
}
inline void advanceTestMillis(uint32_t deltaMs)
{
    testNowMs += deltaMs;
    useTestClock = true;
}
// Restore real-clock behaviour (call in test tearDown if a suite mixes real and fake time).
inline void useRealClock()
{
    useTestClock = false;
    testNowMs = 0;
}
#endif

/// Milliseconds since boot, 32-bit (wraps ~49.7 days). Drop-in for millis().
uint32_t getMillis();

/// Milliseconds since boot, 64-bit, rollover-immune. Non-ISR use only.
uint64_t getMillis64();

} // namespace Time

#endif
