#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SoilMoisture.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SoilMoistureSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_SoilMoisture.h>

SoilMoistureSensor::SoilMoistureSensor()
  : TelemetrySensor(meshtastic_TelemetrySensorType_SOIL_MOISTURE, "SoilMoisture") {}

int32_t SoilMoistureSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);

    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Connect to sensor on correct I2C bus
    status = soil.begin(nodeTelemetrySensorsMap[sensorType].second);

    return initI2CSensor();
}

void SoilMoistureSensor::setup()
{
   
    soil.setMode(MODE_CONTINUOUS);
}

bool SoilMoistureSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_soil_moisture = true;

    float moisture = soil.readMoisture();  

    measurement->variant.environment_metrics.soil_moisture = moisture;

    return true;
}

#endif
