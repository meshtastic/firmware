
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<ClosedCube_OPT3001.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "HDC1080Sensor.h"
#include "TelemetrySensor.h"
#include <ClosedCube_HDC1080.h>

HDC1080Sensor::HDC1080Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_HDC1080, "HDC1080") {}

bool HDC1080Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    hdc1080.begin(dev->address.address);

    status = (hdc1080.readManufacturerId() == 21577);

    initI2CSensor();
    return status;
}

bool HDC1080Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.temperature = hdc1080.readTemperature();

    measurement->variant.environment_metrics.has_relative_humidity = true;
    measurement->variant.environment_metrics.relative_humidity = hdc1080.readHumidity();

    return true;
}

#endif
