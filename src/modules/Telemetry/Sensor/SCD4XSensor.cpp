#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SCD4XSensor.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd4x.h>

#define SCD4X_NO_ERROR 0

SCD4XSensor::SCD4XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SCD4X, "SCD4X") {}

int32_t SCD4XSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    uint16_t error;

    scd4x.begin(*nodeTelemetrySensorsMap[sensorType].second,
        nodeTelemetrySensorsMap[sensorType].first);

    delay(30);
    // Ensure sensor is in clean state
    error = scd4x.wakeUp();
    if (error != SCD4X_NO_ERROR) {
        LOG_INFO("Error trying to execute wakeUp()");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Stop periodic measurement
    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_INFO("Error trying to stopPeriodicMeasurement()");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // TODO - Decide if using Periodic mesaurement or singleshot
    // status = scd4x.startLowPowerPeriodicMeasurement();

    if (!scd4x.startLowPowerPeriodicMeasurement()) {
        status = 1;
    } else {
        status = 0;
    }
    return initI2CSensor();
}

void SCD4XSensor::setup() {}

bool SCD4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t co2, error;
    float temperature;
    float humidity;

    error = scd4x.readMeasurement(co2, temperature, humidity);
    if (error != SCD4X_NO_ERROR || co2 == 0) {
        LOG_DEBUG("Skipping invalid SCD4X measurement.");
        return false;
    } else {
        measurement->variant.air_quality_metrics.has_co2_temperature = true;
        measurement->variant.air_quality_metrics.has_co2_humidity = true;
        measurement->variant.air_quality_metrics.has_co2 = true;
        measurement->variant.air_quality_metrics.co2_temperature = temperature;
        measurement->variant.air_quality_metrics.co2_humidity = humidity;
        measurement->variant.air_quality_metrics.co2 = co2;
        return true;
    }
}

#endif