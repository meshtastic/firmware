#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BMP280.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BMP280Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_BMP280.h>
#include <typeinfo>

BMP280Sensor::BMP280Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BMP280, "BMP280") {}

bool BMP280Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    bmp280 = Adafruit_BMP280(bus);
    status = bmp280.begin(dev->address.address);
    if (!status) {
        return status;
    }

    bmp280.setSampling(Adafruit_BMP280::MODE_FORCED,
                       Adafruit_BMP280::SAMPLING_X1, // Temp. oversampling
                       Adafruit_BMP280::SAMPLING_X1, // Pressure oversampling
                       Adafruit_BMP280::FILTER_OFF, Adafruit_BMP280::STANDBY_MS_1000);

    initI2CSensor();
    return status;
}

bool BMP280Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;

    LOG_DEBUG("BMP280 getMetrics");
    bmp280.takeForcedMeasurement();
    measurement->variant.environment_metrics.temperature = bmp280.readTemperature();
    measurement->variant.environment_metrics.barometric_pressure = bmp280.readPressure() / 100.0F;

    return true;
}

#endif