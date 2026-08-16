#pragma once

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "mesh/generated/meshtastic/telemetry.pb.h"

enum TelemetryPublishChannel : uint8_t {
    TELEMETRY_PUBLISHED_MESH = 1 << 0,
    TELEMETRY_PUBLISHED_MQTT = 1 << 1,
    TELEMETRY_PUBLISHED_PHONE = 1 << 2,
};

template <typename T> struct BufferedReading {
    T metrics;
    uint32_t time = 0;         // seconds since 1970 (or 0/unset)
    uint8_t publishedMask = 0; // which channel(s) have consumed this reading
};

template <typename T, uint8_t N> class TelemetryHistoryBuffer
{
    BufferedReading<T> readings[N]{};
    // head = index (into readings[]) of the oldest reading
    uint8_t head = 0;
    uint8_t count = 0;

  public:
    void push(const T &reading, uint32_t time)
    {
        uint8_t writeIdx = (head + count) % N;
        readings[writeIdx].metrics = reading;
        readings[writeIdx].time = time;
        readings[writeIdx].publishedMask = 0;
        if (count < N)
            count++;
        else
            head = (head + 1) % N; // when full, the write above evicted the old head, advance past it
    }

    uint8_t size() const { return count; }
    bool isEmpty() const { return count == 0; }

    const BufferedReading<T> &at(uint8_t i) const { return readings[(head + i) % N]; } // oldest-first

    void markPublished(uint8_t i, TelemetryPublishChannel ch) { readings[(head + i) % N].publishedMask |= ch; }

    /// Convenience for tagging the newest slot
    void markMostRecent(TelemetryPublishChannel ch)
    {
        if (count > 0)
            markPublished(count - 1, ch);
    }

    void clear()
    {
        head = 0;
        count = 0;
    }
};

// Maps a concrete metrics type onto meshtastic_TelemetryRecord's oneof
inline void assignTelemetryRecord(meshtastic_TelemetryRecord &r, const meshtastic_EnvironmentMetrics &m, uint32_t time)
{
    r.which_telemetry_variant = meshtastic_TelemetryRecord_environment_metrics_tag;
    r.telemetry_variant.environment_metrics = m;
    r.time = time;
}

inline void assignTelemetryRecord(meshtastic_TelemetryRecord &r, const meshtastic_PowerMetrics &m, uint32_t time)
{
    r.which_telemetry_variant = meshtastic_TelemetryRecord_power_metrics_tag;
    r.telemetry_variant.power_metrics = m;
    r.time = time;
}

inline void assignTelemetryRecord(meshtastic_TelemetryRecord &r, const meshtastic_AirQualityMetrics &m, uint32_t time)
{
    r.which_telemetry_variant = meshtastic_TelemetryRecord_air_quality_metrics_tag;
    r.telemetry_variant.air_quality_metrics = m;
    r.time = time;
}
#endif
