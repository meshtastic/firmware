#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SCD4XSensor.h"
#include "TelemetrySensor.h"
#include <SensirionI2CScd4x.h>

SCD4XSensor::SCD4XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SCD4X, "SCD4X") {}

int32_t SCD4XSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    // scd4x = SensirionI2CScd4x(nodeTelemetrySensorsMap[sensorType].second);
    // status = scd4x.begin(nodeTelemetrySensorsMap[sensorType].first);
    scd4x.begin(*nodeTelemetrySensorsMap[sensorType].second);
    scd4x.stopPeriodicMeasurement();
    status = scd4x.startLowPowerPeriodicMeasurement();
    if (status == 0) {
        status = 1;
    } else {
        status = 0;
    }
    return initI2CSensor();
}

void SCD4XSensor::setup()
{

}

bool SCD4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t co2, error;
    float temperature, humidity;
    error = scd4x.readMeasurement(
        co2, temperature, humidity
    );
    if (error || co2 == 0) {
        LOG_DEBUG("Skipping invalid SCD4X measurement.\n");
        return false;
    } else {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.has_co2 = true;
        measurement->variant.environment_metrics.temperature = temperature;
        measurement->variant.environment_metrics.relative_humidity = humidity;
        measurement->variant.environment_metrics.co2 = co2;
        return true;
    }
}

#endif