// See UptimeClock.h for the full contract.
#include "UptimeClock.h"
#include <Arduino.h>
#include <atomic>

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

// Extend a published (high, low) snapshot to `now`. Unsigned subtraction is exact across the wrap
// for any gap under 49.7 days, so neither caller inspects the boundary. One copy: the reader and
// the writer must agree on this arithmetic exactly, or the carry drifts from what readers report.
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
