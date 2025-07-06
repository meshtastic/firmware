#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SensirionI2CSen5x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SEN5XSensor.h"
#include "TelemetrySensor.h"
#include <SensirionI2CSen5x.h>

SEN5XSensor::SEN5XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SEN5X, "SEN5X") {}

int32_t SEN5XSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    sen5x.begin(*nodeTelemetrySensorsMap[sensorType].second);

    delay(25); // without this there is an error on the deviceReset function (NOT WORKING)

    uint16_t error;
    char errorMessage[256];
    error = sen5x.deviceReset();
    if (error) {
        LOG_INFO("Error trying to execute deviceReset(): ");
        errorToString(error, errorMessage, 256);
        LOG_INFO(errorMessage);
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    error = sen5x.startMeasurement();
    if (error) {
        LOG_INFO("Error trying to execute startMeasurement(): ");
        errorToString(error, errorMessage, 256);
        LOG_INFO(errorMessage);
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    } else {
        status = 1;
    }

    return initI2CSensor();
}

void SEN5XSensor::setup()
{
#ifdef SEN5X_ENABLE_PIN
    pinMode(SEN5X_ENABLE_PIN, OUTPUT);
#endif /* SEN5X_ENABLE_PIN */
}

#ifdef SEN5X_ENABLE_PIN
void SEN5XSensor::sleep() {
    digitalWrite(SEN5X_ENABLE_PIN, LOW);
    state = State::IDLE;
}

uint32_t SEN5XSensor::wakeUp() {
    digitalWrite(SEN5X_ENABLE_PIN, HIGH);
    state = State::ACTIVE;
    return SEN5X_WARMUP_MS;
}
#endif /* SEN5X_ENABLE_PIN */

bool SEN5XSensor::isActive() {
    return state == State::ACTIVE;
}

bool SEN5XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t error;
    char errorMessage[256];

    // Read Measurement
    float massConcentrationPm1p0;
    float massConcentrationPm2p5;
    float massConcentrationPm4p0;
    float massConcentrationPm10p0;
    float ambientHumidity;
    float ambientTemperature;
    float vocIndex;
    float noxIndex;

    error = sen5x.readMeasuredValues(
        massConcentrationPm1p0, massConcentrationPm2p5, massConcentrationPm4p0,
        massConcentrationPm10p0, ambientHumidity, ambientTemperature, vocIndex,
        noxIndex);

    if (error) {
        LOG_INFO("Error trying to execute readMeasuredValues(): ");
        errorToString(error, errorMessage, 256);
        LOG_INFO(errorMessage);
        return false;
    }

    measurement->variant.air_quality_metrics.has_pm10_standard = true;
    measurement->variant.air_quality_metrics.pm10_standard = massConcentrationPm1p0;
    measurement->variant.air_quality_metrics.has_pm25_standard = true;
    measurement->variant.air_quality_metrics.pm25_standard = massConcentrationPm2p5;
    measurement->variant.air_quality_metrics.has_pm100_standard = true;
    measurement->variant.air_quality_metrics.pm100_standard = massConcentrationPm10p0;

    return true;
}

#endif