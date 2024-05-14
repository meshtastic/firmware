#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SHT4XSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_SHT4x.h>

SHT4XSensor::SHT4XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHT4X, "SHT4X") {}

int32_t SHT4XSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    uint32_t serialNumber = 0;

    sht4x.begin(nodeTelemetrySensorsMap[sensorType].second);

    serialNumber = sht4x.readSerial();
    if (serialNumber != 0) {
        LOG_DEBUG("serialNumber : %x\n", serialNumber);
        status = 1;
    } else {
        LOG_DEBUG("Error trying to execute readSerial(): ");
        status = 0;
    }

    return initI2CSensor();
}

void SHT4XSensor::setup()
{
    // Set up oversampling and filter initialization
}

bool SHT4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    sensors_event_t humidity, temp;
    sht4x.getEvent(&humidity, &temp);
    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.relative_humidity = humidity.relative_humidity;
    return true;
}

#endif