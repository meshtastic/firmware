#pragma once

#include <new>
#include <stdint.h>
#include <string.h>

// No protobuf or config dependency, so this compiles on variants that exclude the sensors.

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

/// Readings the local loop took between offloads, oldest first. at() copies because a backing store
/// need not be memory; indices are logical, so do not hold one across a push().
template <typename T> class TelemetryStore
{
  public:
    virtual ~TelemetryStore() = default;

    /// False only if nothing was stored; evicting the oldest to make room is the steady state.
    virtual bool push(const T &metrics, uint32_t time) = 0;

    virtual uint16_t size() const = 0;
    virtual uint16_t capacity() const = 0;

    /// Oldest-first; false if i is out of range or the read failed.
    virtual bool at(uint16_t i, TelemetryReading<T> &out) = 0;
    virtual void markPublished(uint16_t i, TelemetryPublishChannel ch) = 0;

    bool isEmpty() const { return size() == 0; }

    bool newest(TelemetryReading<T> &out) { return size() > 0 && at(size() - 1, out); }

    /// True when there is a newest reading that ch has not taken yet
    bool hasUnpublishedNewest(TelemetryPublishChannel ch)
    {
        TelemetryReading<T> r;
        return newest(r) && !(r.publishedMask & ch);
    }

    void markNewestPublished(TelemetryPublishChannel ch)
    {
        if (size() > 0)
            markPublished(size() - 1, ch);
    }
};

/// Heap ring, lost on reset. Already PSRAM-backed past 2 KB on ESP32 via
/// heap_caps_malloc_extmem_enable() in main.cpp, so no filesystem is needed to get there.
template <typename T> class RamTelemetryStore : public TelemetryStore<T>
{
    TelemetryReading<T> *readings = nullptr;
    uint16_t slots = 0;
    uint16_t head = 0; // index into readings[] of the oldest entry
    uint16_t count = 0;

  public:
    explicit RamTelemetryStore(uint16_t capacity) : slots(capacity)
    {
        if (slots > 0)
            readings = new (std::nothrow) TelemetryReading<T>[slots];
        if (!readings)
            slots = 0; // out of memory: store nothing rather than fault on every push
    }

    ~RamTelemetryStore() override { delete[] readings; }

    RamTelemetryStore(const RamTelemetryStore &) = delete;
    RamTelemetryStore &operator=(const RamTelemetryStore &) = delete;

    bool push(const T &metrics, uint32_t time) override
    {
        if (slots == 0)
            return false;

        const uint16_t writeIdx = (head + count) % slots;
        readings[writeIdx].metrics = metrics;
        readings[writeIdx].time = time;
        readings[writeIdx].publishedMask = 0;

        if (count < slots)
            count++;
        else
            head = (head + 1) % slots; // the write above overwrote the old head

        return true;
    }

    uint16_t size() const override { return count; }
    uint16_t capacity() const override { return slots; }

    bool at(uint16_t i, TelemetryReading<T> &out) override
    {
        if (i >= count)
            return false;
        out = readings[(head + i) % slots];
        return true;
    }

    void markPublished(uint16_t i, TelemetryPublishChannel ch) override
    {
        if (i < count)
            readings[(head + i) % slots].publishedMask |= ch;
    }
};
