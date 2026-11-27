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

#ifndef DYP_A01_UART_CONTROLLED
#define DYP_A01_UART_CONTROLLED 1
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

#ifndef DYP_A01_AUTO_INTERVAL_MS
#define DYP_A01_AUTO_INTERVAL_MS 250
#endif

#ifndef DYP_A01_AUTO_STALE_MS
#define DYP_A01_AUTO_STALE_MS 2000
#endif

#ifndef DYP_A01_POWER_BOOT_MS
#define DYP_A01_POWER_BOOT_MS 100
#endif

#ifndef DYP_A01_POWER_SETTLE_MS
#define DYP_A01_POWER_SETTLE_MS 50
#endif

#ifndef DYP_A01_UART_RX
#error "HAS_DYP_A01 requires DYP_A01_UART_RX"
#endif
#ifndef DYP_A01_UART_TX
#error "HAS_DYP_A01 requires DYP_A01_UART_TX"
#endif

#if defined(ARCH_ESP32)
#ifndef DYP_A01_USE_SERIAL2
#error "DYP-A01 on ESP32 must define DYP_A01_USE_SERIAL2; Serial1 belongs to GPS"
#endif
#define DYP_A01_STREAM Serial2
#elif defined(ARCH_NRF52)
#ifndef DYP_A01_USE_SERIAL1
#error "DYP-A01 on nRF52 must define DYP_A01_USE_SERIAL1 and must not leave GPS on that UART"
#endif
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

#if defined(SEEED_SOLAR_NODE)
#if HAS_GPS
#error "Seeed Solar Node DYP-A01 uses the GNSS UART; build seeed_solar_node_dyp so HAS_GPS is 0"
#endif
static_assert(DYP_A01_UART_RX == D7, "Seeed DYP-A01 RX must be GNSS RX (D7)");
static_assert(DYP_A01_UART_TX == D6, "Seeed DYP-A01 TX must be GNSS TX (D6)");
#endif

#if defined(HELTEC_V4)
static_assert(DYP_A01_UART_RX != LORA_KCT8103L_PA_CTX && DYP_A01_UART_TX != LORA_KCT8103L_PA_CTX,
              "DYP-A01 pin collides with Heltec V4.3 PA_CTX (GPIO5)");
static_assert(DYP_A01_UART_RX != LORA_GC1109_PA_EN && DYP_A01_UART_TX != LORA_GC1109_PA_EN,
              "DYP-A01 pin collides with the LoRa FEM enable");
static_assert(DYP_A01_UART_RX != LORA_PA_POWER && DYP_A01_UART_TX != LORA_PA_POWER,
              "DYP-A01 pin collides with the LoRa FEM supply");
#ifdef I2C_SDA1
static_assert(DYP_A01_UART_RX != I2C_SDA1 && DYP_A01_UART_TX != I2C_SDA1, "DYP-A01 pin collides with Wire1 SDA");
static_assert(DYP_A01_UART_RX != I2C_SCL1 && DYP_A01_UART_TX != I2C_SCL1, "DYP-A01 pin collides with Wire1 SCL");
#endif
#ifdef PIN_BUZZER
static_assert(DYP_A01_UART_RX != PIN_BUZZER && DYP_A01_UART_TX != PIN_BUZZER, "DYP-A01 pin collides with the buzzer");
#endif
#endif

#if DYP_A01_UART_CONTROLLED
static_assert(DYP_A01_BURST_SAMPLES > 0 && DYP_A01_BURST_SAMPLES <= 255, "DYP_A01_BURST_SAMPLES out of range");
static_assert(DYP_A01_MIN_VALID_SAMPLES > 0 && DYP_A01_MIN_VALID_SAMPLES <= DYP_A01_BURST_SAMPLES,
              "DYP_A01_MIN_VALID_SAMPLES out of range");
static_assert(DYP_A01_MAX_BURST_ATTEMPTS >= 1 && DYP_A01_MAX_BURST_ATTEMPTS <= 255, "DYP_A01_MAX_BURST_ATTEMPTS out of range");
#endif

static int32_t atLeastOne(uint32_t ms)
{
    if (ms == 0)
        return 1;
    if (ms > 0x7fffffff)
        return 0x7fffffff;
    return (int32_t)ms;
}

// SENSOR_UNSET: EnvironmentMetrics.distance already carries the reading, so no new sensor enum.
DYPA01Sensor::DYPA01Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "DYP-A01") {}

bool DYPA01Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    (void)bus;
    (void)dev;
    LOG_INFO("Init sensor: %s", sensorName);
    bootStartedMs = Time::getMillis();
    enablePeripheralPower();
    openUart();
#if defined(DYP_A01_USE_SERIAL2)
    LOG_INFO("%s: UART Serial2 RX=%d TX=%d %d baud (GPS keeps Serial1)", sensorName, DYP_A01_UART_RX, DYP_A01_UART_TX,
             DYP_A01_UART_BAUD);
#elif defined(DYP_A01_USE_SERIAL1)
    LOG_INFO("%s: UART Serial1 RX=%d TX=%d %d baud (GPS disabled on this build)", sensorName, DYP_A01_UART_RX, DYP_A01_UART_TX,
             DYP_A01_UART_BAUD);
#endif
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

void DYPA01Sensor::enablePeripheralPower()
{
#ifdef DYP_A01_POWER_EN
    if (peripheralPowerOn)
        return;
#if !HAS_GPS
#ifdef PIN_GPS_STANDBY
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    digitalWrite(PIN_GPS_STANDBY, GPS_STANDBY_ACTIVE);
#endif
#ifdef PIN_GPS_RESET
    // Active level holds the onboard GNSS in reset so it cannot drive this UART.
    pinMode(PIN_GPS_RESET, OUTPUT);
    digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE);
#endif
#endif
    pinMode(DYP_A01_POWER_EN, OUTPUT);
    digitalWrite(DYP_A01_POWER_EN, DYP_A01_POWER_EN_ACTIVE);
    peripheralPowerOn = true;
    bootStartedMs = Time::getMillis();
    phase = Phase::Boot;
#else
    phase = Phase::Idle;
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
        if (parsed == DypA01Parse::Rejected && !Throttle::isWithinTimespanMs(lastRejectLogMs, 1000)) {
            lastRejectLogMs = Time::getMillis();
            LOG_DEBUG("%s: rejected UART frame", sensorName);
        }
        if (parsed == DypA01Parse::Accepted && !capturedThisTrigger && validSamples < DYP_A01_BURST_SAMPLES) {
            samples[validSamples++] = sampleMm;
            capturedThisTrigger = true;
            lastValidFrameMs = Time::getMillis();
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
    // Nine samples at a 150 ms gap, plus one retry, is about 1.4-3.2 s. That wait is
    // returned to the scheduler; getMetrics() only copies the finished median.
    for (uint8_t step = 0; step < 6; step++) {
        switch (phase) {
        case Phase::Boot: {
            const uint32_t bootMs = DYP_A01_POWER_SETTLE_MS + DYP_A01_POWER_BOOT_MS;
            if (!Throttle::hasElapsed(bootStartedMs, bootMs))
                return atLeastOne(Throttle::remainingMs(bootStartedMs, bootMs));
            phase = Phase::Idle;
            break;
        }
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

int32_t DYPA01Sensor::runAuto()
{
    while (DYP_A01_STREAM.available() > 0) {
        float sampleMm = 0;
        const DypA01Parse parsed = parser.push((uint8_t)DYP_A01_STREAM.read(), sampleMm);
        if (parsed == DypA01Parse::Accepted) {
            lastDistanceMm = sampleMm;
            hasValidReading = true;
            lastValidFrameMs = Time::getMillis();
        } else if (parsed == DypA01Parse::Rejected && !Throttle::isWithinTimespanMs(lastRejectLogMs, 1000)) {
            lastRejectLogMs = Time::getMillis();
            LOG_DEBUG("%s: rejected UART frame", sensorName);
        }
    }
    if (hasValidReading && Throttle::hasElapsed(lastValidFrameMs, DYP_A01_AUTO_STALE_MS))
        hasValidReading = false;
    return DYP_A01_AUTO_INTERVAL_MS;
}

int32_t DYPA01Sensor::runOnce()
{
    if (!initialized)
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
#if DYP_A01_UART_CONTROLLED
    return runControlled();
#else
    return runAuto();
#endif
}

bool DYPA01Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!hasValidReading) {
        if (!Throttle::isWithinTimespanMs(lastNoMetricsLogMs, 5000)) {
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

int32_t DYPA01Sensor::runOnce()
{
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

bool DYPA01Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    (void)measurement;
    return false;
}

#endif // HAS_DYP_A01

#endif // !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
