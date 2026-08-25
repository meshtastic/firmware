#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cSfa3x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SFA30Sensor.h"

SFA30Sensor::SFA30Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SFA30, "SFA30"){};

bool SFA30Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    _bus = bus;
    _address = dev->address.address;

#ifdef SFA30_I2C_CLOCK_SPEED
    _port = dev->address.port;
    reClockI2C.setup(_bus, _port);
    LOG_INFO("%s: reclock speed %uHz", sensorName, SFA30_I2C_CLOCK_SPEED);
    ReClockI2CGuard clockGuard(reClockI2C, SFA30_I2C_CLOCK_SPEED);
#endif /* SFA30_I2C_CLOCK_SPEED */

    sfa30.begin(*_bus, _address);
    delay(20);

    if (this->isError(sfa30.deviceReset())) {
        return false;
    }

    state = State::IDLE;
    if (this->isError(sfa30.startContinuousMeasurement())) {
        return false;
    }

    LOG_INFO("%s starting measurement", sensorName);

    status = 1;
    state = State::ACTIVE;
    measureStarted = millis();
    LOG_INFO("%s Enabled", sensorName);

    initI2CSensor();
    return true;
};

bool SFA30Sensor::isError(uint16_t response)
{
    if (response == SFA30_NO_ERROR)
        return false;

    // TODO - Check error to char conversion
    LOG_ERROR("%s: %u", sensorName, response);
    return true;
}

void SFA30Sensor::sleep()
{
#ifdef SFA30_I2C_CLOCK_SPEED
    LOG_DEBUG("%s: reclock speed %uHz", sensorName, SFA30_I2C_CLOCK_SPEED);
    ReClockI2CGuard clockGuard(reClockI2C, SFA30_I2C_CLOCK_SPEED);
#endif /* SFA30_I2C_CLOCK_SPEED */

    // Note - not recommended for this sensor on a periodic basis
    if (this->isError(sfa30.stopMeasurement())) {
        LOG_ERROR("%s: Can't stop measurement", sensorName);
    };

    LOG_DEBUG("%s: stop measurement", sensorName);
    state = State::IDLE;
    measureStarted = 0;
}

uint32_t SFA30Sensor::wakeUp()
{
#ifdef SFA30_I2C_CLOCK_SPEED
    LOG_DEBUG("%s: reclock speed %uHz", sensorName, SFA30_I2C_CLOCK_SPEED);
    ReClockI2CGuard clockGuard(reClockI2C, SFA30_I2C_CLOCK_SPEED);
#endif /* SFA30_I2C_CLOCK_SPEED */

    LOG_DEBUG("Waking %s", sensorName);
    if (this->isError(sfa30.startContinuousMeasurement())) {
        return 0;
    }

    state = State::ACTIVE;
    measureStarted = millis();
    return SFA30_WARMUP_MS;
}

int32_t SFA30Sensor::wakeUpTimeMs()
{
    return SFA30_WARMUP_MS;
}

bool SFA30Sensor::canSleep()
{
    // Sleep is disabled in this sensor because readings are not tested with periodic sleep
    // with such low power consumption, prefered to keep it active
    return false;
}

bool SFA30Sensor::isActive()
{
    return state == State::ACTIVE;
}

int32_t SFA30Sensor::pendingForReadyMs()
{
    uint32_t sinceHchoMeasureStarted = millis() - measureStarted;
    LOG_DEBUG("%s: Since measure started: %ums", sensorName, sinceHchoMeasureStarted);

    if (sinceHchoMeasureStarted < SFA30_WARMUP_MS) {
        LOG_INFO("%s: not enough time passed since starting measurement", sensorName);
        return SFA30_WARMUP_MS - sinceHchoMeasureStarted;
    }
    return 0;
}

bool SFA30Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float hcho = 0.0;
    float humidity = 0.0;
    float temperature = 0.0;

#ifdef SFA30_I2C_CLOCK_SPEED
    LOG_DEBUG("%s: reclock speed %uHz", sensorName, SFA30_I2C_CLOCK_SPEED);
    ReClockI2CGuard clockGuard(reClockI2C, SFA30_I2C_CLOCK_SPEED);
#endif /* SFA30_I2C_CLOCK_SPEED */

    if (this->isError(sfa30.readMeasuredValues(hcho, humidity, temperature))) {
        LOG_WARN("%s: No values", sensorName);
        return false;
    }

    measurement->variant.air_quality_metrics.has_form_temperature = true;
    measurement->variant.air_quality_metrics.has_form_humidity = true;
    measurement->variant.air_quality_metrics.has_form_formaldehyde = true;

    measurement->variant.air_quality_metrics.form_temperature = temperature;
    measurement->variant.air_quality_metrics.form_humidity = humidity;
    measurement->variant.air_quality_metrics.form_formaldehyde = hcho;

    LOG_DEBUG("Got %s readings: hcho=%.2f, hcho_temp=%.2f, hcho_hum=%.2f", sensorName, hcho, temperature, humidity);

    return true;
}
#endif
