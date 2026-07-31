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
// The wrap carry, published by Time::serviceMonotonic() and read by everyone else. Split into two
// 32-bit atomics behind a sequence counter: a 64-bit store is not atomic on a 32-bit MCU and the
// halves must be read as a matched pair. Odd sequence = publish in progress.
std::atomic<uint32_t> publishSeq{0};
std::atomic<uint32_t> publishedHigh{0}; // wraps counted as of the last publish
std::atomic<uint32_t> publishedLow{0};  // getMillis() at the last publish

// Seqlock read. Single writer, so this only ever retries against a publish in flight.
void readPublished(uint32_t &high, uint32_t &low)
{
    for (;;) {
        const uint32_t before = publishSeq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        high = publishedHigh.load(std::memory_order_relaxed);
        low = publishedLow.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (publishSeq.load(std::memory_order_relaxed) == before)
            return;
    }
}
} // namespace

uint64_t Time::getMillisMonotonic()
{
    uint32_t high, low;
    readPublished(high, low);
    // Elapsed since the publish. Unsigned subtraction is exact across the wrap for any gap under
    // 49.7 days, so the reader neither inspects the boundary nor writes anything back.
    return ((((uint64_t)high << 32) | low) + (uint32_t)(getMillis() - low));
}

uint32_t Time::getUptimeSecs()
{
    return (uint32_t)(getMillisMonotonic() / 1000);
}

void Time::serviceMonotonic()
{
    const uint32_t low = publishedLow.load(std::memory_order_relaxed);
    const uint32_t high = publishedHigh.load(std::memory_order_relaxed);
    const uint64_t next = ((((uint64_t)high << 32) | low) + (uint32_t)(getMillis() - low));

    const uint32_t seq = publishSeq.load(std::memory_order_relaxed);
    publishSeq.store(seq + 1, std::memory_order_relaxed); // odd: publish in progress
    std::atomic_thread_fence(std::memory_order_release);
    publishedHigh.store((uint32_t)(next >> 32), std::memory_order_relaxed);
    publishedLow.store((uint32_t)next, std::memory_order_relaxed);
    publishSeq.store(seq + 2, std::memory_order_release); // even: stable
}

#ifdef PIO_UNIT_TESTING
void Time::resetMonotonicForTests()
{
    publishSeq.store(0, std::memory_order_relaxed);
    publishedHigh.store(0, std::memory_order_relaxed);
    publishedLow.store(0, std::memory_order_relaxed);
}
#endif
