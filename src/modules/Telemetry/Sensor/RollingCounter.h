#pragma once

#include "UptimeClock.h"
#include "mesh/Throttle.h"
#include <string.h>

/**
 * Sliding-window event counter in fixed memory, one counter per bucket. The spare bucket and the
 * weighted oldest bucket are what hold sum() at exactly WindowMs rather than a bucket either way.
 *   RollingCounter<60UL * 60 * 1000, 5UL * 60 * 1000> strikes; // last hour in 5min steps
 */
template <uint32_t WindowMs, uint32_t BucketMs> class RollingCounter
{
    static_assert(BucketMs > 0, "BucketMs must be non-zero");
    static_assert(WindowMs % BucketMs == 0, "WindowMs must be a whole number of buckets");
    static_assert(WindowMs / BucketMs + 1 <= UINT8_MAX, "too many buckets");

    // One more than WindowMs needs, so the oldest is never recycled while still in the window.
    static constexpr uint8_t BUCKETS = WindowMs / BucketMs + 1;

  public:
    /// Record events happening now.
    void add(uint32_t events = 1)
    {
        advance();
        counts[head] += events;
    }

    /// Events within the last WindowMs.
    uint32_t sum()
    {
        advance();

        // The current bucket plus every fully enclosed one: WindowMs - BucketMs, plus however
        // far the current bucket has filled.
        uint32_t total = 0;
        for (uint8_t age = 0; age <= BUCKETS - 2; age++)
            total += counts[(head + BUCKETS - age) % BUCKETS];

        // The oldest bucket covers the remainder. Counting only the part still inside is what
        // holds the total at exactly WindowMs as the current bucket fills.
        uint32_t elapsed = Time::getMillis() - bucketStartMs;
        uint32_t inWindow = elapsed < BucketMs ? BucketMs - elapsed : 0;
        // 64-bit: the product overflows 32 bits once a bucket holds more than 2^32 / BucketMs
        // events, which is only ~14k at a 5 minute width.
        total += (uint32_t)(((uint64_t)counts[(head + 1) % BUCKETS] * inWindow + BucketMs / 2) / BucketMs);
        return total;
    }

    void reset()
    {
        memset(counts, 0, sizeof(counts));
        head = 0;
        bucketStartMs = Time::getMillis();
        started = true;
    }

  private:
    void advance()
    {
        if (!started) {
            reset();
            return;
        }
        if (!Throttle::hasElapsed(bucketStartMs, BucketMs))
            return;

        uint32_t steps = (Time::getMillis() - bucketStartMs) / BucketMs;
        if (steps >= BUCKETS) { // idle longer than the ring, nothing survives
            reset();
            return;
        }
        bucketStartMs += steps * BucketMs;
        while (steps--) {
            head = (head + 1) % BUCKETS;
            counts[head] = 0;
        }
    }

    uint32_t counts[BUCKETS] = {};
    uint32_t bucketStartMs = 0;
    uint8_t head = 0;
    // Explicit rather than bucketStartMs == 0, which is a real time value after a rollover.
    bool started = false;
};
