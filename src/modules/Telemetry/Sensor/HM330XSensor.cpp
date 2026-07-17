#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<Seeed_HM330X.h>)

#include "../detect/reClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "HM330XSensor.h"

HM330XSensor::HM330XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_HM330X, "HM330X"){};

bool HM330XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    _bus = bus;
    _address = dev->address.address;

#ifdef HM330X_I2C_CLOCK_SPEED
    _port = dev->address.port;
    reClockI2C.setup(_bus, _port);

    LOG_INFO("%s: attempting to reclock speed to %uHz", sensorName, HM330X_I2C_CLOCK_SPEED);
    reClockI2C.setClock(HM330X_I2C_CLOCK_SPEED);
#endif /* HM330X_I2C_CLOCK_SPEED */

    if (hm330x.init(_bus) != HM330XErrorCode::NO_ERROR) {
#ifdef HM330X_I2C_CLOCK_SPEED
        LOG_INFO("%s: restoring clock speed", sensorName);
        reClockI2C.restoreClock();
#endif /* HM330X_I2C_CLOCK_SPEED */
        LOG_WARN("%s error in sensor init", sensorName);
        return false;
    }

#ifdef HM330X_I2C_CLOCK_SPEED
    LOG_INFO("%s: restoring clock speed", sensorName);
    reClockI2C.restoreClock();
#endif /* HM330X_I2C_CLOCK_SPEED */

    status = 1;
    LOG_INFO("%s Enabled", sensorName);

    initI2CSensor();
    return true;
};

uint32_t HM330XSensor::wakeUp()
{
    state = State::ACTIVE;
    measureStarted = getTime();
    return HM330X_WARMUP_MS;
}

int32_t HM330XSensor::wakeUpTimeMs()
{
    return HM330X_WARMUP_MS;
}

bool HM330XSensor::canSleep()
{
    // Sleep not available in this sensor
    return false;
}

bool HM330XSensor::isActive()
{
    return state == State::ACTIVE;
}

int32_t HM330XSensor::pendingForReadyMs()
{
    uint32_t now;
    now = getTime();
    uint32_t sincePMMeasureStarted = (now - measureStarted) * 1000;
    LOG_DEBUG("%s: Since measure started: %ums", sensorName, sincePMMeasureStarted);

    if (sincePMMeasureStarted < HM330X_WARMUP_MS) {
        LOG_INFO("%s: not enough time passed since starting measurement", sensorName);
        return HM330X_WARMUP_MS - sincePMMeasureStarted;
    }
    return 0;
}

bool HM330XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
#ifdef HM330X_I2C_CLOCK_SPEED
    LOG_DEBUG("%s: attempting to reclock speed to %uHz", sensorName, HM330X_I2C_CLOCK_SPEED);
    reClockI2C.setClock(HM330X_I2C_CLOCK_SPEED);
#endif /* HM330X_I2C_CLOCK_SPEED */

    if (hm330x.read_sensor_value(buffer, 29)) {
        LOG_WARN("%s: read result failed", sensorName);
#ifdef HM330X_I2C_CLOCK_SPEED
        LOG_INFO("%s: restoring clock speed", sensorName);
        reClockI2C.restoreClock();
#endif /* HM330X_I2C_CLOCK_SPEED */
        return false;
    }

#ifdef HM330X_I2C_CLOCK_SPEED
    LOG_INFO("%s: restoring clock speed", sensorName);
    reClockI2C.restoreClock();
#endif /* HM330X_I2C_CLOCK_SPEED */

    if (hm330x.checksum_calc(buffer) != HM330XErrorCode::NO_ERROR) {
        LOG_ERROR("%s: Checksum error", sensorName);
        return false;
    }

    auto read16 = [](const uint8_t *data, uint8_t idx) -> uint16_t { return (data[idx] << 8) | data[idx + 1]; };

    // TODO - This is suspiciously not in terms of PM1, PM2.5 and PM10.0 relationship
    // Normally, PM1.0 > PM2.5 > PM10, but in this case it's not. Are the buckets cumulative?
    // It's not documented in the datasheet, so safely assuming that it's correct as in the datasheet
    measurement->variant.air_quality_metrics.has_pm10_standard = true;
    measurement->variant.air_quality_metrics.has_pm25_standard = true;
    measurement->variant.air_quality_metrics.has_pm100_standard = true;

    measurement->variant.air_quality_metrics.pm10_standard = read16(buffer, 4);
    measurement->variant.air_quality_metrics.pm25_standard = read16(buffer, 6);
    measurement->variant.air_quality_metrics.pm100_standard = read16(buffer, 8);

    // TODO - Add admin command to remove environmental metrics to save protobuf space
    // TODO - Decide if these should be enabled
    // measurement->variant.air_quality_metrics.has_pm10_environmental = true;
    // measurement->variant.air_quality_metrics.has_pm25_environmental = true;
    // measurement->variant.air_quality_metrics.has_pm100_environmental = true;

    // measurement->variant.air_quality_metrics.pm10_environmental = read16(buffer, 10);
    // measurement->variant.air_quality_metrics.pm25_environmental = read16(buffer, 12);
    // measurement->variant.air_quality_metrics.pm100_environmental = read16(buffer, 14);

    LOG_DEBUG("%s: Got readings: pM1p0_standard=%u, pM2p5_standard=%u, pM10p0_standard=%u", sensorName,
              measurement->variant.air_quality_metrics.pm10_standard, measurement->variant.air_quality_metrics.pm25_standard,
              measurement->variant.air_quality_metrics.pm100_standard);

    return true;
}
#endif
