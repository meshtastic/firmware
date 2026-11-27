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

/// A record carries its time when the clock was valid, else the gap since the previous one. The first timed record
/// after an untimed one carries the gap too, so the untimed ones can be dated back from it.
template <typename T>
void fillTelemetryRecord(meshtastic_TelemetryRecord &r, const TelemetryReading<T> &reading, bool previousUntimed = false)
{
    setTelemetryRecordMetrics(r, reading.metrics);
    r.has_time = reading.time != 0;
    r.time = reading.time;
    r.has_delta_secs = (!r.has_time || previousUntimed) && reading.deltaSecs != 0;
    r.delta_secs = r.has_delta_secs ? reading.deltaSecs : 0;
}

/// Bytes one reading adds to a batch: its record and the field header in front of it
template <typename T> size_t telemetryRecordBytes(const TelemetryReading<T> &reading)
{
    meshtastic_TelemetryRecord r = meshtastic_TelemetryRecord_init_zero;
    fillTelemetryRecord(r, reading);
    size_t size = 0;
    if (!pb_get_encoded_size(&size, &meshtastic_TelemetryRecord_msg, &r))
        return 0;
    return size + 1 + (size < 128 ? 1 : 2); // tag, length
}

/// Largest record among the newest n readings, 0 if there are none
template <typename T> size_t largestRecentTelemetryRecordBytes(TelemetryStore<T> &store, uint16_t n)
{
    size_t largest = 0;
    TelemetryReading<T> reading;
    for (uint16_t i = store.size(); i > 0 && n > 0; i--, n--) {
        if (!store.at(i - 1, reading))
            continue;
        const size_t bytes = telemetryRecordBytes(reading);
        if (bytes > largest)
            largest = bytes;
    }
    return largest;
}

/// Readings of recordBytes that fit one mesh batch, with a 10% margin for values that encode longer, and one slot
/// short of a store of storeSlots so a reading is not evicted before its batch goes (0 = no store limit)
inline uint16_t telemetryReadingsThatFit(size_t recordBytes, size_t budget, uint16_t storeSlots = 0)
{
    if (recordBytes == 0)
        return 0;
    size_t fit = budget * 9 / 10 / recordBytes;
    if (fit > TELEMETRY_MAX_READINGS_PER_MESH_PACKET)
        fit = TELEMETRY_MAX_READINGS_PER_MESH_PACKET;
    if (storeSlots > 1 && fit > storeSlots - 1u)
        fit = storeSlots - 1u;
    return fit;
}

/// Read interval so the readings of one mesh interval fit one batch of `fit`: readMs when they already do,
/// else the shortest whole-second interval that does
inline uint32_t telemetryAutofitReadMs(uint32_t readMs, uint32_t meshMs, uint16_t fit)
{
    if (fit == 0 || readMs == 0 || meshMs <= readMs || (meshMs + readMs - 1) / readMs <= fit)
        return readMs;
    const uint32_t fittedSecs = ((meshMs + fit - 1) / fit + 999) / 1000;
    return fittedSecs * 1000;
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
    bool previousUntimed = false;
    for (uint16_t i = 0; i < store.size() && taken < maxReadings; i++) {
        if (!store.at(i, reading))
            continue;
        const bool untimed = reading.time == 0;
        if (!(reading.publishedMask & ch)) {
            fillTelemetryRecord(batch->readings[taken], reading, previousUntimed);
            indices[taken++] = i;
        }
        previousUntimed = untimed;
    }

    for (; taken > 0; taken--) {
        batch->readings_count = taken;
        encodedSize = pb_encode_to_bytes(buf, bufSize, &meshtastic_TelemetryRecordHistory_msg, batch.get());
        if (encodedSize > 0)
            return taken;
    }
    return 0;
}
