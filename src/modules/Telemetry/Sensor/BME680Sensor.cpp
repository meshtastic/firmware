#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BME680.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BME680Sensor.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "TelemetrySensor.h"
#include "UptimeClock.h"
#include "gps/RTC.h"
#include "mesh/Throttle.h"

#include <math.h>

BME680Sensor::BME680Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BME680, "BME680") {}

bool BME680Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    status = 0;

    bme680 = makeBME680(bus);

    if (!bme680->begin(dev->address.address)) {
        LOG_ERROR("Init sensor: %s failed at begin()", sensorName);
        return status;
    }

    // Acquisition profile, stated explicitly (these match the library defaults):
    // the heater setting determines power draw, ~0.25% duty at one sample/min
    bme680->setTemperatureOversampling(BME680_OS_8X);
    bme680->setHumidityOversampling(BME680_OS_2X);
    bme680->setPressureOversampling(BME680_OS_4X);
    bme680->setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme680->setGasHeater(320, 150); // 320 degC for 150 ms

    status = 1;
    loadState();
    LOG_INFO("Init sensor: %s (open IAQ estimator)", sensorName);

    initI2CSensor();
    return status;
}

int32_t BME680Sensor::runOnce()
{
    uint32_t now = Time::getMillis();

    if (readingInFlight) {
        if (!Throttle::deadlinePassedAt(now, readingDoneAtMs))
            return readingDoneAtMs - now;
        captureSample();
        return SAMPLE_INTERVAL_MS;
    }

    if (haveSample && Throttle::isWithinTimespanMs(lastSampleMs, SAMPLE_INTERVAL_MS))
        return SAMPLE_INTERVAL_MS - (now - lastSampleMs);

    uint32_t doneAt = bme680->beginReading();
    if (doneAt == 0) {
        LOG_WARN("%s beginReading() failed", sensorName);
        return SAMPLE_INTERVAL_MS;
    }
    readingInFlight = true;
    readingDoneAtMs = doneAt;
    return Throttle::deadlinePassedAt(now, doneAt) ? 1 : (int32_t)(doneAt - now);
}

/// Complete the reading (in flight or synchronous), feed the estimator, refresh the cache
void BME680Sensor::captureSample()
{
    readingInFlight = false;
    // endReading() completes the in-flight conversion, or starts and finishes
    // a fresh one when none is pending (performReading() is an alias for it in
    // Adafruit_BME680; a failed first call resets the conversion, so the second
    // call is a genuine one-shot retry). Worst case each call waits ~2x the
    // remaining TPHG cycle, so a synchronous read costs a few hundred ms.
    if (!bme680->endReading() && !bme680->performReading()) {
        LOG_WARN("%s reading failed", sensorName);
        return;
    }

    lastTemperature = bme680->temperature;
    lastHumidity = bme680->humidity;
    lastPressureHPa = bme680->pressure / 100.0F;
    lastGasOhms = (float)bme680->gas_resistance;
    haveSample = true;
    lastSampleMs = Time::getMillis();

    uint16_t iaq;
    if (iaqEstimator.update(lastGasOhms, lastHumidity, &iaq)) {
        lastIaq = iaq;
        lastIaqValid = true;
        lastIaqMs = lastSampleMs;
    } else if (isfinite(lastGasOhms) && lastGasOhms > 0.0f) {
        // Valid gas sample but the estimator has no output yet (warm-up/burn-in)
        lastIaqValid = false;
    } else if (lastIaqValid && !Throttle::isWithinTimespanMs(lastIaqMs, IAQ_CARRY_MS)) {
        // Heater-unstable cycles (gas reported as 0) may ride on the previous
        // IAQ briefly, but a persistently gasless sensor stops reporting IAQ
        lastIaqValid = false;
    }

    maybeSaveState();
}

bool BME680Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!haveSample || !Throttle::isWithinTimespanMs(lastSampleMs, SAMPLE_FRESH_MS))
        captureSample();
    // A failed refresh must not freeze the last reading on the wire: publish
    // only while the cache is genuinely fresh
    if (!haveSample || !Throttle::isWithinTimespanMs(lastSampleMs, SAMPLE_FRESH_MS))
        return false;

    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;

    measurement->variant.environment_metrics.temperature = lastTemperature;
    measurement->variant.environment_metrics.relative_humidity = lastHumidity;
    measurement->variant.environment_metrics.barometric_pressure = lastPressureHPa;

    // A heater-unstable cycle reports gas_resistance 0; suppress the field
    // rather than broadcasting a bogus 0 kOhm point
    if (isfinite(lastGasOhms) && lastGasOhms > 0.0f) {
        measurement->variant.environment_metrics.has_gas_resistance = true;
        // Fleet convention is kOhm on the wire (despite the proto comment saying MOhm)
        measurement->variant.environment_metrics.gas_resistance = lastGasOhms / 1000.0f;
    }

    if (lastIaqValid) {
        measurement->variant.environment_metrics.has_iaq = true;
        measurement->variant.environment_metrics.iaq = lastIaq;
    }
    return true;
}

void BME680Sensor::loadState()
{
#ifdef FSCom
    BME680IaqState state;
    bool haveBlob = false;

    spiLock->lock();
    auto file = FSCom.open(stateFileName, FILE_O_READ);
    if (file) {
        haveBlob = file.read((uint8_t *)&state, sizeof(state)) == sizeof(state);
        file.close();
    }
    // One-time cleanup of the proprietary-BSEC calibration blob from older firmware
    if (FSCom.exists(legacyBsecStateFileName) && FSCom.remove(legacyBsecStateFileName))
        LOG_INFO("%s removed legacy state file %s", sensorName, legacyBsecStateFileName);
    spiLock->unlock();

    if (!haveBlob) {
        LOG_INFO("No %s state found (File: %s)", sensorName, stateFileName);
        return;
    }
    if (iaqEstimator.restore(state, getValidTime(RTCQuality::RTCQualityDevice))) {
        lastPersistedSampleCount = iaqEstimator.samplesFed();
        lastPersistedWarmup = iaqEstimator.warmupLeft();
        lastSaveEpochSecs = state.savedAtSecs;
        LOG_INFO("%s IAQ state restored from %s (%u samples)", sensorName, stateFileName, iaqEstimator.samplesFed());
    } else {
        LOG_INFO("%s IAQ state in %s rejected (stale or invalid), starting fresh", sensorName, stateFileName);
    }
#else
    LOG_ERROR("Filesystem not implemented");
#endif
}

void BME680Sensor::maybeSaveState()
{
    if (!iaqEstimator.ready()) {
        // Persist warm-up/burn-in progress whenever it advances, so a
        // deep-sleeping SENSOR node (one sample per wake, RAM wiped between)
        // still converges. Bounded to ~33 writes over the sensor's lifetime.
        if (iaqEstimator.samplesFed() != lastPersistedSampleCount || iaqEstimator.warmupLeft() != lastPersistedWarmup)
            saveState();
        return;
    }

    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice);
    if (nowSecs != 0 && lastSaveEpochSecs != 0) {
        // RTC available: gate on wall-clock age so short deep-sleep wakes don't
        // rewrite flash every time
        if (nowSecs >= lastSaveEpochSecs && (nowSecs - lastSaveEpochSecs) < STATE_SAVE_PERIOD_SECS)
            return;
    } else {
        // No RTC: gate on the persisted sample count (it survives reboots, so
        // deep-sleeping RTC-less nodes still refresh their baseline every
        // ~STATE_SAVE_PERIOD_MS worth of samples) with an uptime cadence as a
        // secondary trigger for always-on nodes
        if (iaqEstimator.samplesFed() - lastPersistedSampleCount < STATE_SAVE_PERIOD_MS / SAMPLE_INTERVAL_MS &&
            !Throttle::hasElapsed(lastStateSaveMs, STATE_SAVE_PERIOD_MS))
            return;
    }
    saveState();
}

void BME680Sensor::saveState()
{
#ifdef FSCom
    BME680IaqState state;
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice);
    iaqEstimator.serialize(&state, nowSecs);

    // SafeFile takes the SPI lock itself; fullAtomic keeps the old state file
    // in place until the verified replacement is renamed over it, so a power
    // loss mid-save can't lose the banked burn-in progress (the blob is 24
    // bytes, so the atomic path costs nothing)
    auto file = SafeFile(stateFileName, true);
    file.write((uint8_t *)&state, sizeof(state));
    if (file.close()) {
        lastPersistedSampleCount = iaqEstimator.samplesFed();
        lastPersistedWarmup = iaqEstimator.warmupLeft();
        lastSaveEpochSecs = nowSecs;
        lastStateSaveMs = Time::getMillis();
        LOG_DEBUG("%s state write to %s", sensorName, stateFileName);
    } else {
        LOG_WARN("Can't write %s state (File: %s)", sensorName, stateFileName);
    }
#else
    LOG_ERROR("Filesystem not implemented");
#endif
}

#endif
