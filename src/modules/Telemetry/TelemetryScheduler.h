#pragma once

#include <initializer_list>
#include <stdint.h>

/// Cadences of one telemetry module, in ms. readMs paces stored readings, phoneMs paces reads while a
/// client is connected, meshMs paces what goes on air, mqttMs the direct uplink (0 = off).
/// publishOnReads: mesh and mqtt go out only in a read's wake-up, so a deep-sleeping node does not wake for them.
struct TelemetryCadence {
    uint32_t readMs;
    uint32_t phoneMs;
    uint32_t meshMs;
    uint32_t mqttMs;
    bool publishOnReads = false;
};

/// When a telemetry module reads its sensors and publishes. Pure: every input is passed in, so the
/// policy is testable without hardware. Stamps are millis() values, 0 = never.
class TelemetryScheduler
{
  public:
    uint32_t lastStoredMs = 0; // last read on the stored cadence, valid or not
    uint32_t lastValidMs = 0;  // last read that went into the store, for deltaSecs
    uint32_t lastReadMs = 0;   // last read of any kind
    uint32_t lastMqttMs = 0;
    uint32_t lastMeshSentMs = 0;    // kept here too: TransmitHistory's stamp is boot-relative until the clock is set
    uint32_t lastMeshAttemptMs = 0; // last time the mesh was due but nothing went out
    uint32_t meshRetryMs = 0;       // how long that skip holds the mesh back

    /// A due mesh publish blocked by the airtime limit retries after this
    static constexpr uint32_t MESH_RETRY_MS = 60 * 1000;

    static uint32_t remaining(uint32_t nowMs, uint32_t lastMs, uint32_t intervalMs)
    {
        if (lastMs == 0)
            return 0;
        const uint32_t elapsed = nowMs - lastMs;
        return elapsed >= intervalMs ? 0 : intervalMs - elapsed;
    }

    /// ms until the next read, 0 = due. The stored cadence always runs; the phone one only while connected.
    uint32_t msUntilRead(uint32_t nowMs, const TelemetryCadence &c, bool phoneConnected) const
    {
        uint32_t ms = remaining(nowMs, lastStoredMs, c.readMs);
        if (phoneConnected && c.phoneMs) {
            const uint32_t phone = remaining(nowMs, lastReadMs, c.phoneMs);
            if (phone < ms)
                ms = phone;
        }
        return ms;
    }

    /// A read now lands on the stored cadence, rather than only feeding the phone
    bool readIsStored(uint32_t nowMs, const TelemetryCadence &c) const { return remaining(nowMs, lastStoredMs, c.readMs) == 0; }

    /// Seconds since the previous stored reading, 0 when there is none
    uint32_t deltaSecs(uint32_t nowMs) const { return lastValidMs ? (nowMs - lastValidMs) / 1000 : 0; }

    /// readMs keeps stored reads on a fixed grid: a read that finishes late (warm-up, I2C) does not push
    /// every later one back. A read more than an interval late restarts the grid, in phase with anchorMs
    /// (another module's grid stamp, 0 = none) so the modules of a node read in the same wake-up.
    void onRead(uint32_t nowMs, bool stored, bool valid, uint32_t readMs = 0, uint32_t anchorMs = 0)
    {
        nowMs = nowMs ? nowMs : 1;
        lastReadMs = nowMs;
        if (stored) {
            // Stamped either way, so a failing sensor retries on the cadence rather than every poll
            const uint32_t elapsed = nowMs - lastStoredMs;
            const bool onGrid = lastStoredMs && readMs && elapsed >= readMs && elapsed - readMs < readMs;
            if (onGrid)
                lastStoredMs += readMs;
            else if (anchorMs && readMs)
                lastStoredMs = nowMs - (nowMs - anchorMs) % readMs;
            else
                lastStoredMs = nowMs;
            if (!lastStoredMs)
                lastStoredMs = 1;
            if (valid)
                lastValidMs = nowMs; // the real gap, for deltaSecs
        }
    }

    /// A stored read is due within MESH_ALIGN_MS: the mesh waits for it so the batch carries it
    bool storedReadSoon(uint32_t nowMs, const TelemetryCadence &c) const
    {
        const uint32_t ms = remaining(nowMs, lastStoredMs, c.readMs);
        return ms > 0 && ms <= MESH_ALIGN_MS;
    }

    static constexpr uint32_t MESH_ALIGN_MS = 60 * 1000;

    uint32_t msUntilMesh(uint32_t nowMs, uint32_t lastMeshMs, uint32_t meshMs) const
    {
        const uint32_t due = remaining(nowMs, lastMeshMs, meshMs);
        const uint32_t retry = remaining(nowMs, lastMeshAttemptMs, meshRetryMs);
        return due > retry ? due : retry;
    }

    /// Due now, or for publishOnReads within PUBLISH_SLACK_MS: the read wake-up that comes just before it carries it.
    /// The slack is for the interval only, a retry backoff always runs out in full.
    bool meshDue(uint32_t nowMs, uint32_t lastMeshMs, const TelemetryCadence &c) const
    {
        return remaining(nowMs, lastMeshMs, c.meshMs) <= publishSlackMs(c) &&
               remaining(nowMs, lastMeshAttemptMs, meshRetryMs) == 0;
    }

    /// Wake-ups drift by a few seconds against a publish interval that is a multiple of the read one
    static constexpr uint32_t PUBLISH_SLACK_MS = 60 * 1000;
    static uint32_t publishSlackMs(const TelemetryCadence &c) { return c.publishOnReads ? PUBLISH_SLACK_MS : 0; }

    /// The mesh was due but nothing went out: hold it for retryMs instead of retrying every poll
    void onMeshSkipped(uint32_t nowMs, uint32_t retryMs)
    {
        lastMeshAttemptMs = nowMs ? nowMs : 1;
        meshRetryMs = retryMs;
    }

    /// Nothing new to publish: hold the mesh until the next stored reading, at least the airtime retry
    uint32_t meshRetryWhenEmpty(uint32_t nowMs, const TelemetryCadence &c) const
    {
        const uint32_t nextStored = remaining(nowMs, lastStoredMs, c.readMs);
        return nextStored > MESH_RETRY_MS ? nextStored : MESH_RETRY_MS;
    }

    void onMeshSent(uint32_t nowMs)
    {
        lastMeshSentMs = nowMs ? nowMs : 1;
        lastMeshAttemptMs = 0;
    }

    /// Our own stamp when there is one (it survives deep sleep), else the caller's (it survives reboot)
    uint32_t lastMeshMs(uint32_t fallbackMs) const { return lastMeshSentMs ? lastMeshSentMs : fallbackMs; }

    bool mqttDue(uint32_t nowMs, const TelemetryCadence &c) const
    {
        return c.mqttMs && remaining(nowMs, lastMqttMs, c.mqttMs) <= publishSlackMs(c);
    }

    void onMqttPublished(uint32_t nowMs) { lastMqttMs = nowMs ? nowMs : 1; }

    /// ms until the module next has work: the read minus the sensor warm-up lead, the mesh or the mqtt publish
    /// (publishOnReads: only the read, the publishes ride on it)
    uint32_t msUntilNextAction(uint32_t nowMs, const TelemetryCadence &c, bool phoneConnected, uint32_t lastMeshMs,
                               uint32_t readLeadMs) const
    {
        const uint32_t read = msUntilRead(nowMs, c, phoneConnected);
        uint32_t ms = read > readLeadMs ? read - readLeadMs : 0;
        if (c.publishOnReads)
            return ms;
        uint32_t mesh = msUntilMesh(nowMs, lastMeshMs, c.meshMs);
        if (mesh == 0 && storedReadSoon(nowMs, c))
            mesh = remaining(nowMs, lastStoredMs, c.readMs);
        if (mesh < ms)
            ms = mesh;
        if (c.mqttMs) {
            const uint32_t mqtt = remaining(nowMs, lastMqttMs, c.mqttMs);
            if (mqtt < ms)
                ms = mqtt;
        }
        return ms;
    }

    /// Ages of the stamps, so they can be carried across a deep sleep that restarts millis()
    struct Ages {
        uint32_t stored, valid, mqtt, mesh; // ms before the reference point, UINT32_MAX = never
    };

    Ages agesAt(uint32_t nowMs) const
    {
        return {age(nowMs, lastStoredMs), age(nowMs, lastValidMs), age(nowMs, lastMqttMs), age(nowMs, lastMeshSentMs)};
    }

    /// Rebuild the stamps from ages measured at nowMs (after adding the time spent asleep)
    void restoreAges(uint32_t nowMs, const Ages &a)
    {
        lastStoredMs = fromAge(nowMs, a.stored);
        lastValidMs = fromAge(nowMs, a.valid);
        lastMqttMs = fromAge(nowMs, a.mqtt);
        lastMeshSentMs = fromAge(nowMs, a.mesh);
    }

    /// Kept in memory that survives deep sleep (RTC_DATA_ATTR on ESP32), so the cadence and deltaSecs
    /// carry on after a timer wake-up instead of restarting with millis()
    struct Retained {
        static constexpr uint32_t MAGIC = 0x54534348; // "TSCH"
        uint32_t magic;
        Ages ages;        // at the save
        uint64_t clockUs; // caller's clock at the save (ESP32: RTC us, nRF52: uptime ms), to measure the time away
    };

    void saveForSleep(Retained &r, uint32_t nowMs, uint64_t clockUs) const
    {
        r.ages = agesAt(nowMs);
        r.clockUs = clockUs;
        r.magic = Retained::MAGIC;
    }

    /// msSinceSave is measured, not planned: pre-sleep work, boot and timer drift all count.
    /// One-shot: the retained state is consumed, so a later cold boot cannot replay it.
    bool restoreAfterSleep(Retained &r, uint32_t nowMs, uint32_t msSinceSave)
    {
        if (r.magic != Retained::MAGIC)
            return false;
        Ages a = r.ages;
        for (uint32_t *v : {&a.stored, &a.valid, &a.mqtt, &a.mesh}) {
            if (*v != UINT32_MAX)
                *v = (UINT32_MAX - *v > msSinceSave) ? *v + msSinceSave : UINT32_MAX - 1;
        }
        restoreAges(nowMs, a);
        r.magic = 0;
        return true;
    }

  private:
    static uint32_t age(uint32_t nowMs, uint32_t stampMs) { return stampMs ? nowMs - stampMs : UINT32_MAX; }
    static uint32_t fromAge(uint32_t nowMs, uint32_t ageMs)
    {
        if (ageMs == UINT32_MAX)
            return 0;
        const uint32_t stamp = nowMs - ageMs;
        return stamp ? stamp : 1;
    }
};
