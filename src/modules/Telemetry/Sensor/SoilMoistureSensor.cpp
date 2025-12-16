#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_seesaw.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SoilMoistureSensor.h"
#include "TelemetrySensor.h"
#include "main.h"

SoilMoistureSensor::SoilMoistureSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_ADA4026, "SoilMoisture") 
{
}

int32_t SoilMoistureSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    uint8_t addr = nodeTelemetrySensorsMap[sensorType].first;
    
    ss.begin(addr);
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
