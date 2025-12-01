#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_seesaw.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SoilMoistureSensor.h"
#include "TelemetrySensor.h"
#include "main.h"

SoilMoistureSensor::SoilMoistureSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_CUSTOM_SENSOR, "SoilMoisture") 
{
}

int32_t SoilMoistureSensor::runOnce()
{
    // Check if already initialized
    if (hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // If scanner found the device, map it to our sensor type
        nodeTelemetrySensorsMap[sensorType].first = 0x39;
        LOG_INFO("Mapped Soil Moisture Sensor at 0x%x", 0x39);

    // Now proceed with initialization using the mapped address
    LOG_INFO("Init SoilMoistureSensor: %s", sensorName);
    delay(100);

    uint8_t addr = nodeTelemetrySensorsMap[sensorType].first;
    if (!ss.begin(addr)) { 
        LOG_ERROR("SoilMoistureSensor init failed at 0x%x", addr);
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    ss.pinMode(0, INPUT);
    
    status = addr;  // Set status for initI2CSensor
    return initI2CSensor();
}

void SoilMoistureSensor::setup()
{
}

bool SoilMoistureSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t raw_cap = ss.touchRead(0); 
    
    float moisture_percent = map(raw_cap, 200, 2000, 0, 100); 
    moisture_percent = constrain(moisture_percent, 0.0, 100.0);

    measurement->variant.environment_metrics.has_soil_moisture = true;
    measurement->variant.environment_metrics.soil_moisture = moisture_percent;

    return true;
}

#endif
