#pragma once

#include <stdint.h>

// Deliberately free of any protobuf or configuration dependency: T is a template parameter, so this
// header compiles on every variant, including those that exclude the telemetry sensors entirely.

/// Which consumers have already taken a given reading
enum TelemetryPublishChannel : uint8_t {
    TELEMETRY_PUBLISHED_MESH = 1 << 0,
    TELEMETRY_PUBLISHED_PHONE = 1 << 1,
};

template <typename T> struct TelemetryReading {
    T metrics{};
    uint32_t time = 0;         // seconds since 1970 when captured, 0 if the clock was not valid then
    uint8_t publishedMask = 0; // which consumers have taken it
};

/**
 * Fixed-size ring of the local loop's most recent readings, oldest first.
 *
 * The local device-to-phone loop reads faster than the on-air offload publishes, so readings pile up
 * in between. Each entry carries its own capture time and a note of which consumers have taken it,
 * so a consumer running on its own cadence picks up exactly what it has not seen and a reading
 * published late is never restamped as fresher than it is.
 *
 * Overwriting the oldest entry once full is the normal steady state, not a fault: the offload
 * samples the loop rather than draining it.
 */
template <typename T, uint8_t N> class TelemetryHistory
{
    static_assert(N > 0, "a history of nothing is just a missing reading");

    TelemetryReading<T> readings[N]{};
    uint8_t head = 0; // index into readings[] of the oldest entry
    uint8_t count = 0;

  public:
    void push(const T &metrics, uint32_t time)
    {
        const uint8_t writeIdx = (head + count) % N;
        readings[writeIdx].metrics = metrics;
        readings[writeIdx].time = time;
        readings[writeIdx].publishedMask = 0;

        if (count < N)
            count++;
        else
            head = (head + 1) % N; // the write above overwrote the old head, step past it
    }

    uint8_t size() const { return count; }
    bool isEmpty() const { return count == 0; }
    static constexpr uint8_t capacity() { return N; }

    /// Oldest-first. Caller must check size() first; i >= size() is undefined.
    const TelemetryReading<T> &at(uint8_t i) const { return readings[(head + i) % N]; }
    void markPublished(uint8_t i, TelemetryPublishChannel ch) { readings[(head + i) % N].publishedMask |= ch; }

    const TelemetryReading<T> &newest() const { return at(count - 1); }
    void markNewestPublished(TelemetryPublishChannel ch) { markPublished(count - 1, ch); }

    /// True when there is a newest reading that ch has not taken yet
    bool hasUnpublishedNewest(TelemetryPublishChannel ch) const { return count > 0 && !(newest().publishedMask & ch); }
};
