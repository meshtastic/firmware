#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SHTC3.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SHTC3Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_SHTC3.h>

SHTC3Sensor::SHTC3Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHTC3, "SHTC3") {}

bool SHTC3Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = shtc3.begin(bus);

    initI2CSensor();
    return status;
}

bool SHTC3Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;

    sensors_event_t humidity, temp;
    shtc3.getEvent(&humidity, &temp);

    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.relative_humidity = humidity.relative_humidity;

    return true;
}

#endif