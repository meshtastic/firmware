// See UptimeClock.h for the full contract.
#include "UptimeClock.h"
#include <Arduino.h>
#include <atomic>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_timer.h>
#elif defined(ARDUINO_ARCH_RP2040)
#include <pico/time.h>
#elif defined(ARDUINO_NRF52_ADAFRUIT)
#include <nrf.h>
#endif

uint32_t Time::getMillis()
{
#ifdef PIO_UNIT_TESTING
    if (Time::useTestClock.load(std::memory_order_relaxed))
        return Time::testNowMs.load(std::memory_order_relaxed);
#endif
    return millis();
}

namespace
{
struct PublishedSnapshot {
    std::atomic<uint32_t> high{0};
    std::atomic<uint32_t> low{0};
};

// The constexpr atomic initializers make both snapshots available before firmware startup.
PublishedSnapshot published[2];
std::atomic<uint32_t> publishedGeneration{0};
#ifdef PIO_UNIT_TESTING
std::atomic<Time::MonotonicPublishHook> monotonicPublishHook{nullptr};
#endif

// Extend a published (high, low) snapshot to `now`; unsigned subtraction is exact across the wrap
// for any gap under 49.7 days. One copy, because reader and writer must agree on it exactly.
uint64_t extendPublished(uint32_t high, uint32_t low, uint32_t now)
{
    return ((((uint64_t)high << 32) | low) + (uint32_t)(now - low));
}

// A generation change means the writer completed a publish while this copy was being read. A
// paused publish leaves the generation unchanged and writes only the inactive snapshot.
void readPublished(uint32_t &high, uint32_t &low)
{
    for (;;) {
        const uint32_t before = publishedGeneration.load(std::memory_order_acquire);
        PublishedSnapshot &snapshot = published[before & 1u];
        high = snapshot.high.load(std::memory_order_relaxed);
        low = snapshot.low.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (publishedGeneration.load(std::memory_order_relaxed) == before)
            return;
    }
}
} // namespace

uint64_t Time::getMillisMonotonic()
{
    uint32_t high, low;
    readPublished(high, low);
    // The reader writes nothing back; it just extends the last published carry to now.
    return extendPublished(high, low, getMillis());
}

uint64_t Time::getMicrosMonotonic()
{
#ifdef PIO_UNIT_TESTING
    if (Time::useTestClock.load(std::memory_order_relaxed))
        return getMillisMonotonic() * 1000;
#endif
#if defined(ARDUINO_ARCH_ESP32)
    return (uint64_t)esp_timer_get_time();
#elif defined(ARDUINO_ARCH_RP2040)
    return time_us_64();
#elif defined(ARDUINO_NRF52_ADAFRUIT)
    return nrf52MicrosFromCounter(NRF_RTC2->COUNTER, getMillisMonotonic());
#else
    return getMillisMonotonic() * 1000;
#endif
}

#if defined(ARDUINO_NRF52_ADAFRUIT)
namespace
{
// RTC2 runs undivided from the same 32.768 kHz crystal as the FreeRTOS tick (RTC1), so its 24-bit
// counter is the fine part and the ms clock picks the wrap: unambiguous while they agree to 256 s.
std::atomic<bool> rtc2Running{false};
uint64_t rtc2CounterAtStart = 0; // written once, before rtc2Running is set
uint64_t msTicksAtStart = 0;

uint64_t msToTicks(uint64_t ms)
{
    return ms * 32768 / 1000;
}
} // namespace

uint64_t Time::nrf52MicrosFromCounter(uint32_t counter, uint64_t refMs)
{
    if (!rtc2Running.load(std::memory_order_acquire))
        return refMs * 1000;
    // The tick count the ms clock implies, then the counter value nearest to it
    const uint64_t expected = msToTicks(refMs) - msTicksAtStart + rtc2CounterAtStart;
    const int32_t delta = (int32_t)(((counter - (uint32_t)expected) & 0xFFFFFFu) << 8) >> 8;
    const uint64_t ticks = expected + delta - rtc2CounterAtStart + msTicksAtStart;
    return ticks * 15625 / 512;
}

void Time::startMicrosClock()
{
    if (rtc2Running.load(std::memory_order_relaxed))
        return;
    NRF_RTC2->TASKS_STOP = 1;
    NRF_RTC2->PRESCALER = 0;
    NRF_RTC2->EVTENCLR = 0xFFFFFFFFu;
    NRF_RTC2->INTENCLR = 0xFFFFFFFFu;
    NRF_RTC2->TASKS_CLEAR = 1;
    NRF_RTC2->TASKS_START = 1;
    // The counter starts on the next LFCLK edge; take the pair once it moves so both describe one instant
    const uint32_t first = NRF_RTC2->COUNTER;
    while (NRF_RTC2->COUNTER == first) {
    }
    rtc2CounterAtStart = NRF_RTC2->COUNTER;
    msTicksAtStart = msToTicks(getMillisMonotonic());
    rtc2Running.store(true, std::memory_order_release);
}

uint32_t Time::isrStamp()
{
    return NRF_RTC2->COUNTER;
}

uint64_t Time::stampToMicros(uint32_t stamp)
{
    // A stamp under 256 s old lies behind the counter by less than half its range
    const uint64_t refMs = getMillisMonotonic();
    const uint32_t now = NRF_RTC2->COUNTER;
    const uint32_t ageTicks = (now - stamp) & 0xFFFFFFu;
    return nrf52MicrosFromCounter(now, refMs) - (uint64_t)ageTicks * 15625 / 512;
}
#else
void Time::startMicrosClock() {}

uint32_t Time::isrStamp()
{
#if defined(ARDUINO_ARCH_ESP32)
    return (uint32_t)esp_timer_get_time();
#elif defined(ARDUINO_ARCH_RP2040)
    return time_us_32();
#else
    return getMillis() * 1000U;
#endif
}

uint64_t Time::stampToMicros(uint32_t stamp)
{
    const uint64_t nowUs = getMicrosMonotonic();
    return nowUs - (uint32_t)((uint32_t)nowUs - stamp);
}
#endif

uint32_t Time::getUptimeSecs()
{
    return (uint32_t)(getMillisMonotonic() / 1000);
}

void Time::serviceMonotonic()
{
    const uint32_t generation = publishedGeneration.load(std::memory_order_relaxed);
    PublishedSnapshot &active = published[generation & 1u];
    const uint32_t low = active.low.load(std::memory_order_relaxed);
    const uint32_t high = active.high.load(std::memory_order_relaxed);
    const uint64_t next = extendPublished(high, low, getMillis());

    PublishedSnapshot &inactive = published[(generation + 1u) & 1u];
    inactive.high.store((uint32_t)(next >> 32), std::memory_order_relaxed);
    inactive.low.store((uint32_t)next, std::memory_order_relaxed);
#ifdef PIO_UNIT_TESTING
    if (const auto hook = monotonicPublishHook.load(std::memory_order_relaxed))
        hook();
#endif
    publishedGeneration.store(generation + 1u, std::memory_order_release);
}

#ifdef PIO_UNIT_TESTING
void Time::resetMonotonicForTests()
{
    publishedGeneration.store(0, std::memory_order_relaxed);
    for (auto &snapshot : published) {
        snapshot.high.store(0, std::memory_order_relaxed);
        snapshot.low.store(0, std::memory_order_relaxed);
    }
    monotonicPublishHook.store(nullptr, std::memory_order_relaxed);
}

void Time::setMonotonicPublishHookForTests(MonotonicPublishHook hook)
{
    monotonicPublishHook.store(hook, std::memory_order_relaxed);
}
#endif
