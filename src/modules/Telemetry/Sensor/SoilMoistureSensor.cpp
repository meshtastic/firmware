#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<I2CSoilMoistureSensor.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SoilMoistureSensor.h"
#include "TelemetrySensor.h"
#include "main.h"

SoilMoistureSensor::SoilMoistureSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_CUSTOM, "SoilMoisture") {}

int32_t SoilMoistureSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);

    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Initialize using the I2C bus assigned to this sensor type
    TwoWire *wirePort = nodeTelemetrySensorsMap[sensorType].second;
    uint8_t i2cAddr = nodeTelemetrySensorsMap[sensorType].first;

    wirePort->begin();
    soilSensor.begin(i2cAddr, *wirePort);

    status = 1; // sensor OK
    return initI2CSensor();
}

void SoilMoistureSensor::setup()
{
    // No special setup required for this sensor
    LOG_INFO("Soil Moisture sensor setup complete");
}

bool SoilMoistureSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->which_variant = meshtastic_Telemetry_environment_metrics_tag;

    auto &env = measurement->variant.environment_metrics;

    env.has_temperature = true;
    env.has_illuminance = true;
    env.has_soil_moisture = true;

    env.temperature = soilSensor.getTemperature();
    env.illuminance = soilSensor.getLight();
    env.soil_moisture = soilSensor.getCapacitance();

    return true;
}

#endif
