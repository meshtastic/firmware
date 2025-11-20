/* #include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<I2CSoilMoistureSensor.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SoilMoistureSensor.h"
#include "TelemetrySensor.h"
#include <I2CSoilMoistureSensor.h>

SoilMoistureSensor::SoilMoistureSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "SoilMoisture") {}

int32_t SoilMoistureSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);

    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Get I2C bus and address from the sensor map
    TwoWire *wirePort = nodeTelemetrySensorsMap[sensorType].second;
    uint8_t i2cAddr = nodeTelemetrySensorsMap[sensorType].first;

    LOG_DEBUG("Initializing soil moisture sensor at address 0x%02X\n", i2cAddr);

    // Initialize the sensor
    soilSensor.begin(true);

    // Verify sensor is working by reading version
    uint8_t version = soilSensor.getVersion();
    if (version > 0) {
        LOG_INFO("Soil Moisture Sensor found, version: 0x%02X\n", version);
        status = 1;
    } else {
        LOG_WARN("Soil Moisture Sensor not responding\n");
        status = 0;
    }

    return initI2CSensor();
}

void SoilMoistureSensor::setup()
{
    LOG_DEBUG("Soil moisture sensor setup complete\n");
    // No special setup needed for this sensor
}

bool SoilMoistureSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!isInitialized() || !isRunning()) {
        return false;
    }

    // Check if sensor is busy
    if (soilSensor.isBusy()) {
        LOG_DEBUG("Soil sensor busy, skipping read\n");
        return false;
    }

    measurement->which_variant = meshtastic_Telemetry_environment_metrics_tag;
    auto &env = measurement->variant.environment_metrics;

    // Read soil moisture capacitance
    unsigned int capacitance = soilSensor.getCapacitance();
    
    // Read temperature (sensor returns temperature * 10)
    int rawTemp = soilSensor.getTemperature();
    float temperature = rawTemp / 10.0f;

    LOG_DEBUG("Soil - Capacitance: %d, Temperature: %.1f°C\n", capacitance, temperature);

    // Set the metrics
    env.has_temperature = true;
    env.temperature = temperature;
    
    // Store soil moisture data
    // Using relative_humidity field temporarily for moisture percentage
    float moisturePercent = map(constrain(capacitance, 250, 1800), 250, 1800, 0, 100);
    env.has_relative_humidity = true;
    env.relative_humidity = moisturePercent;

    return true;
}

#endif */

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_seesaw.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SoilMoistureSensor.h"
#include "TelemetrySensor.h"
#include "main.h"

#endif