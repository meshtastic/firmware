#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SHT4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SHT4XSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_SHT4x.h>

SHT4XSensor::SHT4XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHT4X, "SHT4X") {}

bool SHT4XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    uint32_t serialNumber = 0;

    status = sht4x.begin(bus);
    if (!status) {
        return status;
    }

    serialNumber = sht4x.readSerial();
    if (serialNumber != 0) {
        LOG_DEBUG("serialNumber : %x", serialNumber);
        status = 1;
    } else {
        LOG_DEBUG("Error trying to execute readSerial(): ");
        status = 0;
    }

    initI2CSensor();
    return status;
}

bool SHT4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;

    sensors_event_t humidity, temp;
    sht4x.getEvent(&humidity, &temp);
    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.relative_humidity = humidity.relative_humidity;
    return true;
}

#endif