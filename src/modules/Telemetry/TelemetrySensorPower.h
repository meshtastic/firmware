#pragma once

#include "DebugConfiguration.h"
#include <stdint.h>

// Warm-up and sleep of sensors that support it, for any telemetry module. Templated on the
// container so it does not depend on how a module holds its sensors.

/// How long before a read the slowest sleeping sensor has to be woken
template <typename Sensors> uint32_t sensorWakeLeadMs(Sensors &sensors)
{
    uint32_t lead = 0;
    for (auto *sensor : sensors) {
        if (sensor->canSleep() && !sensor->isActive() && sensor->wakeUpTimeMs() > 0 && (uint32_t)sensor->wakeUpTimeMs() > lead)
            lead = sensor->wakeUpTimeMs();
    }
    return lead;
}

/// Wake each sleeping sensor once its warm-up no longer fits before the read.
/// @return ms until the next wake-up or the read; when the read is due, ms until the slowest sensor
/// is ready, 0 = read now.
template <typename Sensors> uint32_t prepareSensorsForRead(Sensors &sensors, uint32_t msUntilRead)
{
    uint32_t waitMs = msUntilRead;
    uint32_t pendingMs = 0;

    for (auto *sensor : sensors) {
        if (!sensor->canSleep())
            continue;

        if (!sensor->isActive()) {
            const uint32_t lead = sensor->wakeUpTimeMs() > 0 ? sensor->wakeUpTimeMs() : 0;
            if (lead < msUntilRead) {
                if (msUntilRead - lead < waitMs)
                    waitMs = msUntilRead - lead;
                continue;
            }
            LOG_DEBUG("Waking %s, %ums ahead of read", sensor->sensorName, (unsigned)msUntilRead);
            sensor->wakeUp();
        }

        int32_t pending = sensor->pendingForReadyMs();
        if (!sensor->isActive()) {
            // Finished a maintenance cycle (e.g. SEN5X fan cleaning) and went idle: start measuring again
            LOG_DEBUG("Re-waking %s after it went idle", sensor->sensorName);
            sensor->wakeUp();
            pending = sensor->pendingForReadyMs();
        }
        if (pending > 0 && (uint32_t)pending > pendingMs)
            pendingMs = pending;
    }

    return msUntilRead ? waitMs : pendingMs;
}

/// Sleep each sensor that can warm up again before its next read
template <typename Sensors> void sleepSensorsUntilNextRead(Sensors &sensors, uint32_t msUntilNextRead)
{
    for (auto *sensor : sensors) {
        if (!sensor->canSleep() || !sensor->isActive())
            continue;
        if (sensor->wakeUpTimeMs() < 0 || (uint32_t)sensor->wakeUpTimeMs() < msUntilNextRead) {
            LOG_DEBUG("Sleep %s until next read in %ums", sensor->sensorName, (unsigned)msUntilNextRead);
            sensor->sleep();
        } else {
            LOG_DEBUG("Keep %s awake, warm-up outlasts next read in %ums", sensor->sensorName, (unsigned)msUntilNextRead);
        }
    }
}
