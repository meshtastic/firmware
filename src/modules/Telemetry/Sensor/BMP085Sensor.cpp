#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BMP085.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BMP085Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_BMP085.h>
#include <typeinfo>

BMP085Sensor::BMP085Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BMP085, "BMP085") {}

bool BMP085Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    bmp085 = Adafruit_BMP085();
    status = bmp085.begin(dev->address.address, bus);

    initI2CSensor();
    return status;
}

bool BMP085Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;

    LOG_DEBUG("BMP085 getMetrics");
    measurement->variant.environment_metrics.temperature = bmp085.readTemperature();
    measurement->variant.environment_metrics.barometric_pressure = bmp085.readPressure() / 100.0F;

    return true;
}

#endif