#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_seesaw.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SoilMoistureSensor.h"
#include "TelemetrySensor.h"
#include "main.h"

SoilMoistureSensor::SoilMoistureSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_SHTC3, "SoilMoisture") // temporary type
{
}

int32_t SoilMoistureSensor::runOnce()
{
    LOG_INFO("Init SoilMoistureSensor: %s", sensorName);

    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (!ss.begin(0x36)) { // default I2C address for STEMMA Soil Sensor
        LOG_ERROR("SoilMoistureSensor not detected at I2C 0x36");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Initialize capacitive touch module for moisture reading
    ss.pinMode(0, INPUT);
    return initI2CSensor();
}

void SoilMoistureSensor::setup()
{
    LOG_INFO("SoilMoistureSensor setup complete");
}

bool SoilMoistureSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_soil_moisture = true;

    uint16_t moisture = ss.touchRead(0); // read capacitive moisture value from channel 0
    measurement->variant.environment_metrics.soil_moisture = moisture;

    return true;
}

#endif
