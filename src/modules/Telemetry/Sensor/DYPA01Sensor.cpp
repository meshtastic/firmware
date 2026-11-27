#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "DYPA01Sensor.h"
#include "TelemetrySensor.h"

#if defined(HAS_DYP_A01)

#include "Default.h"
#include "NodeDB.h"
#include "UptimeClock.h"
#include "mesh/Throttle.h"
#include <Arduino.h>

#ifndef DYP_A01_UART_BAUD
#define DYP_A01_UART_BAUD 9600
#endif

#ifndef DYP_A01_TRIGGER_BYTE
#define DYP_A01_TRIGGER_BYTE 0x01
#endif

#ifndef DYP_A01_POST_TRIGGER_WAIT_MS
#define DYP_A01_POST_TRIGGER_WAIT_MS 60
#endif

#ifndef DYP_A01_MIN_TRIGGER_GAP_MS
#define DYP_A01_MIN_TRIGGER_GAP_MS 150
#endif

#ifndef DYP_A01_READ_WINDOW_MS
#define DYP_A01_READ_WINDOW_MS 40
#endif

#ifndef DYP_A01_MAX_BURST_ATTEMPTS
#define DYP_A01_MAX_BURST_ATTEMPTS 2
#endif

#ifndef DYP_A01_RETRY_DELAY_MS
#define DYP_A01_RETRY_DELAY_MS 500
#endif

#ifndef DYP_A01_UART_RX
#error "HAS_DYP_A01 requires DYP_A01_UART_RX"
#endif
#ifndef DYP_A01_UART_TX
#error "HAS_DYP_A01 requires DYP_A01_UART_TX"
#endif

#if defined(ARCH_ESP32)
// Serial1 is GPS on ESP32 boards; DYP uses Serial2.
#define DYP_A01_STREAM Serial2
#elif defined(ARCH_NRF52)
// Opt-in DYP builds must leave GPS off this UART (see variant).
#define DYP_A01_STREAM Serial1
#else
#error "DYP-A01 UART is only implemented for ESP32 and nRF52"
#endif

#if HAS_GPS && defined(GPS_RX_PIN) && defined(GPS_TX_PIN)
static_assert(DYP_A01_UART_RX != GPS_RX_PIN, "DYP-A01 RX is the GPS RX pin");
static_assert(DYP_A01_UART_TX != GPS_TX_PIN, "DYP-A01 TX is the GPS TX pin");
static_assert(DYP_A01_UART_RX != GPS_TX_PIN, "DYP-A01 RX is the GPS TX pin");
static_assert(DYP_A01_UART_TX != GPS_RX_PIN, "DYP-A01 TX is the GPS RX pin");
#endif

static_assert(DYP_A01_BURST_SAMPLES > 0 && DYP_A01_BURST_SAMPLES <= 255, "DYP_A01_BURST_SAMPLES out of range");
static_assert(DYP_A01_MIN_VALID_SAMPLES > 0 && DYP_A01_MIN_VALID_SAMPLES <= DYP_A01_BURST_SAMPLES,
              "DYP_A01_MIN_VALID_SAMPLES out of range");
static_assert(DYP_A01_MAX_BURST_ATTEMPTS >= 1 && DYP_A01_MAX_BURST_ATTEMPTS <= 255, "DYP_A01_MAX_BURST_ATTEMPTS out of range");

static int32_t atLeastOne(uint32_t ms)
{
    if (ms == 0)
        return 1;
    if (ms > 0x7fffffff)
        return 0x7fffffff;
    return (int32_t)ms;
}

DYPA01Sensor::DYPA01Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "DYP-A01") {}

bool DYPA01Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    (void)bus;
    (void)dev;
    LOG_INFO("Init sensor: %s", sensorName);
    openUart();
    LOG_INFO("%s: UART RX=%d TX=%d %d baud", sensorName, DYP_A01_UART_RX, DYP_A01_UART_TX, DYP_A01_UART_BAUD);
    status = 1;
    initialized = true;
    return true;
}

void DYPA01Sensor::openUart()
{
#if defined(ARCH_ESP32)
    DYP_A01_STREAM.end();
    DYP_A01_STREAM.begin(DYP_A01_UART_BAUD, SERIAL_8N1, DYP_A01_UART_RX, DYP_A01_UART_TX);
#elif defined(ARCH_NRF52)
    DYP_A01_STREAM.setPins(DYP_A01_UART_RX, DYP_A01_UART_TX);
    DYP_A01_STREAM.begin(DYP_A01_UART_BAUD);
#endif
}

void DYPA01Sensor::flushInput()
{
    while (DYP_A01_STREAM.available() > 0)
        (void)DYP_A01_STREAM.read();
    parser.reset();
}

int32_t DYPA01Sensor::armTrigger()
{
    flushInput();
    capturedThisTrigger = false;
    DYP_A01_STREAM.write((uint8_t)DYP_A01_TRIGGER_BYTE);
    triggerSentMs = Time::getMillis();
    phase = Phase::PostTrigger;
    return atLeastOne(DYP_A01_POST_TRIGGER_WAIT_MS);
}

uint32_t DYPA01Sensor::telemetryIntervalMs() const
{
    return Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_update_interval,
                                             default_telemetry_broadcast_interval_secs);
}

static uint32_t controlledBudgetMs()
{
    return (uint32_t)DYP_A01_MAX_BURST_ATTEMPTS * DYP_A01_BURST_SAMPLES * DYP_A01_MIN_TRIGGER_GAP_MS +
           (uint32_t)(DYP_A01_MAX_BURST_ATTEMPTS - 1) * DYP_A01_RETRY_DELAY_MS;
}

int32_t DYPA01Sensor::idleWait()
{
    const uint32_t intervalMs = telemetryIntervalMs();
    const uint32_t budgetMs = controlledBudgetMs();
    if (!burstCompleted || intervalMs <= budgetMs)
        return 1;
    return atLeastOne(Throttle::remainingMs(lastBurstDoneMs, intervalMs - budgetMs));
}

bool DYPA01Sensor::burstIsDue()
{
    if (!burstCompleted)
        return true;
    const uint32_t intervalMs = telemetryIntervalMs();
    const uint32_t budgetMs = controlledBudgetMs();
    if (intervalMs <= budgetMs)
        return true;
    return Throttle::hasElapsed(lastBurstDoneMs, intervalMs - budgetMs);
}

bool DYPA01Sensor::takeSampleIfReady()
{
    while (DYP_A01_STREAM.available() > 0) {
        float sampleMm = 0;
        const DypA01Parse parsed = parser.push((uint8_t)DYP_A01_STREAM.read(), sampleMm);
        if (parsed == DypA01Parse::Rejected && Throttle::hasElapsed(lastRejectLogMs, 1000)) {
            lastRejectLogMs = Time::getMillis();
            LOG_DEBUG("%s: rejected UART frame", sensorName);
        }
        if (parsed == DypA01Parse::Accepted && !capturedThisTrigger && validSamples < DYP_A01_BURST_SAMPLES) {
            samples[validSamples++] = sampleMm;
            capturedThisTrigger = true;
        }
    }
    return capturedThisTrigger || Throttle::hasElapsed(readWindowStartMs, DYP_A01_READ_WINDOW_MS);
}

int32_t DYPA01Sensor::finishBurst()
{
    const uint32_t now = Time::getMillis();
    if (dypA01BurstPublishable(validSamples)) {
        lastDistanceMm = dypA01MedianMm(samples, validSamples);
        hasValidReading = true;
        LOG_INFO("%s: distance=%.0f mm (median %u/%u, min=%.0f max=%.0f)", sensorName, (double)lastDistanceMm, validSamples,
                 DYP_A01_BURST_SAMPLES, (double)samples[0], (double)samples[validSamples - 1]);
    } else if (attempt < DYP_A01_MAX_BURST_ATTEMPTS) {
        attempt++;
        LOG_DEBUG("%s: burst %u/%u valid, retrying", sensorName, validSamples, DYP_A01_BURST_SAMPLES);
        retryStartedMs = now;
        phase = Phase::Retry;
        return atLeastOne(DYP_A01_RETRY_DELAY_MS);
    } else {
        hasValidReading = false;
        LOG_DEBUG("%s: no distance (disconnected or too few valid frames)", sensorName);
    }

    burstCompleted = true;
    lastBurstDoneMs = now;
    phase = Phase::Idle;
    return idleWait();
}

int32_t DYPA01Sensor::runControlled()
{
    // Nine samples at a 150 ms gap, plus one retry, is about 1.4-3.2 s when paced by the
    // scheduler. getMetrics() finishes an in-flight or due burst with delay() before publish.
    for (uint8_t step = 0; step < 6; step++) {
        switch (phase) {
        case Phase::Idle:
            if (!burstIsDue())
                return idleWait();
            attempt = 1;
            validSamples = 0;
            triggersThisBurst = 0;
            return armTrigger();
        case Phase::PostTrigger:
            if (!Throttle::hasElapsed(triggerSentMs, DYP_A01_POST_TRIGGER_WAIT_MS))
                return atLeastOne(Throttle::remainingMs(triggerSentMs, DYP_A01_POST_TRIGGER_WAIT_MS));
            readWindowStartMs = Time::getMillis();
            phase = Phase::Read;
            break;
        case Phase::Read:
            if (!takeSampleIfReady())
                return 10;
            phase = Phase::Gap;
            break;
        case Phase::Gap:
            if (!Throttle::hasElapsed(triggerSentMs, DYP_A01_MIN_TRIGGER_GAP_MS))
                return atLeastOne(Throttle::remainingMs(triggerSentMs, DYP_A01_MIN_TRIGGER_GAP_MS));
            triggersThisBurst++;
            if (triggersThisBurst < DYP_A01_BURST_SAMPLES)
                return armTrigger();
            return finishBurst();
        case Phase::Retry:
            if (!Throttle::hasElapsed(retryStartedMs, DYP_A01_RETRY_DELAY_MS))
                return atLeastOne(Throttle::remainingMs(retryStartedMs, DYP_A01_RETRY_DELAY_MS));
            validSamples = 0;
            triggersThisBurst = 0;
            return armTrigger();
        }
    }
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

int32_t DYPA01Sensor::runOnce()
{
    if (!initialized)
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    return runControlled();
}

bool DYPA01Sensor::ensureBurstForPublish()
{
    // Fresh cache from this interval: publish without blocking.
    if (phase == Phase::Idle && hasValidReading && !burstIsDue())
        return true;

    // Publishing with no usable cache must not wait out the telemetry interval.
    if (phase == Phase::Idle && !burstIsDue())
        burstCompleted = false;

    const uint32_t startedMs = Time::getMillis();
    const uint32_t budgetMs = controlledBudgetMs() + 500;
    while (!Throttle::hasElapsed(startedMs, budgetMs)) {
        const int32_t waitMs = runControlled();
        if (phase == Phase::Idle && burstCompleted)
            return hasValidReading;
        if (waitMs > 0)
            delay((uint32_t)waitMs);
    }
    return hasValidReading;
}

bool DYPA01Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!ensureBurstForPublish()) {
        if (Throttle::hasElapsed(lastNoMetricsLogMs, 5000)) {
            lastNoMetricsLogMs = Time::getMillis();
            LOG_DEBUG("%s: not publishing distance", sensorName);
        }
        return false;
    }

    measurement->variant.environment_metrics.has_distance = true;
    measurement->variant.environment_metrics.distance = lastDistanceMm;
    return true;
}

#else // !HAS_DYP_A01

DYPA01Sensor::DYPA01Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "DYP-A01") {}

bool DYPA01Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    (void)bus;
    (void)dev;
    return false;
}

bool DYPA01Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    (void)measurement;
    return false;
}

#endif // HAS_DYP_A01

#endif // !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
