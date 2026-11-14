#pragma once

#include "TelemetryStore.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "mesh/mesh-pb-constants.h"
#include <memory>
#include <new>

// TelemetryRecordHistory batches of stored readings, for SENSOR nodes that publish in bulk.

#ifndef TELEMETRY_MAX_READINGS_PER_MESH_PACKET
#define TELEMETRY_MAX_READINGS_PER_MESH_PACKET 10
#endif

static constexpr uint16_t TELEMETRY_MAX_READINGS_PER_BATCH =
    sizeof(meshtastic_TelemetryRecordHistory::readings) / sizeof(meshtastic_TelemetryRecordHistory::readings[0]);

inline void setTelemetryRecordMetrics(meshtastic_TelemetryRecord &r, const meshtastic_EnvironmentMetrics &m)
{
    r.which_telemetry_variant = meshtastic_TelemetryRecord_environment_metrics_tag;
    r.telemetry_variant.environment_metrics = m;
}

inline void setTelemetryRecordMetrics(meshtastic_TelemetryRecord &r, const meshtastic_AirQualityMetrics &m)
{
    r.which_telemetry_variant = meshtastic_TelemetryRecord_air_quality_metrics_tag;
    r.telemetry_variant.air_quality_metrics = m;
}

inline void setTelemetryRecordMetrics(meshtastic_TelemetryRecord &r, const meshtastic_PowerMetrics &m)
{
    r.which_telemetry_variant = meshtastic_TelemetryRecord_power_metrics_tag;
    r.telemetry_variant.power_metrics = m;
}

inline void setTelemetryMetrics(meshtastic_Telemetry &t, const meshtastic_EnvironmentMetrics &m)
{
    t.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    t.variant.environment_metrics = m;
}

inline void setTelemetryMetrics(meshtastic_Telemetry &t, const meshtastic_AirQualityMetrics &m)
{
    t.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
    t.variant.air_quality_metrics = m;
}

inline void setTelemetryMetrics(meshtastic_Telemetry &t, const meshtastic_PowerMetrics &m)
{
    t.which_variant = meshtastic_Telemetry_power_metrics_tag;
    t.variant.power_metrics = m;
}

/// A stored reading as a single Telemetry, the way it goes out when readings are not batched
template <typename T> meshtastic_Telemetry toTelemetry(const TelemetryReading<T> &reading)
{
    meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
    setTelemetryMetrics(t, reading.metrics);
    t.time = reading.time;
    return t;
}

/// A record carries its time when the clock was valid, else the gap since the previous one
template <typename T> void fillTelemetryRecord(meshtastic_TelemetryRecord &r, const TelemetryReading<T> &reading)
{
    setTelemetryRecordMetrics(r, reading.metrics);
    r.has_time = reading.time != 0;
    r.time = reading.time;
    r.has_delta_secs = !r.has_time && reading.deltaSecs != 0;
    r.delta_secs = r.has_delta_secs ? reading.deltaSecs : 0;
}

/// Encode the oldest readings ch has not taken, up to maxReadings, dropping the newest of them until
/// the batch fits buf. Their store indices go to indices[], which holds TELEMETRY_MAX_READINGS_PER_BATCH.
/// @return readings encoded, 0 if there were none or nothing fits
template <typename T>
uint16_t encodeTelemetryBatch(TelemetryStore<T> &store, TelemetryPublishChannel ch, uint16_t maxReadings, uint8_t *buf,
                              size_t bufSize, size_t &encodedSize, uint16_t *indices)
{
    if (maxReadings > TELEMETRY_MAX_READINGS_PER_BATCH)
        maxReadings = TELEMETRY_MAX_READINGS_PER_BATCH;

    // ~3.8 KB at full size, too much for a module's stack
    std::unique_ptr<meshtastic_TelemetryRecordHistory> batch(new (std::nothrow) meshtastic_TelemetryRecordHistory);
    if (!batch)
        return 0;
    *batch = meshtastic_TelemetryRecordHistory_init_zero;

    uint16_t taken = 0;
    TelemetryReading<T> reading;
    for (uint16_t i = 0; i < store.size() && taken < maxReadings; i++) {
        if (!store.at(i, reading) || (reading.publishedMask & ch))
            continue;
        fillTelemetryRecord(batch->readings[taken], reading);
        indices[taken++] = i;
    }

    for (; taken > 0; taken--) {
        batch->readings_count = taken;
        encodedSize = pb_encode_to_bytes(buf, bufSize, &meshtastic_TelemetryRecordHistory_msg, batch.get());
        if (encodedSize > 0)
            return taken;
    }
    return 0;
}
