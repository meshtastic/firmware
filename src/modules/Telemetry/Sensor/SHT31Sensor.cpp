#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SHT31.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SHT31Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_SHT31.h>

SHT31Sensor::SHT31Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHT31, "SHT31") {}

bool SHT31Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    sht31 = Adafruit_SHT31(bus);
    status = sht31.begin(dev->address.address);
    initI2CSensor();
    return status;
}

bool SHT31Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;
    measurement->variant.environment_metrics.temperature = sht31.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = sht31.readHumidity();

    return true;
}

#endif