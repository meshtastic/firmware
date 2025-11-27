#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BME280.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BME280Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME280.h>
#include <typeinfo>

BME280Sensor::BME280Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BME280, "BME280") {}

bool BME280Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = bme280.begin(dev->address.address, bus);
    if (!status) {
        return status;
    }

    bme280.setSampling(Adafruit_BME280::MODE_FORCED,
                       Adafruit_BME280::SAMPLING_X1, // Temp. oversampling
                       Adafruit_BME280::SAMPLING_X1, // Pressure oversampling
                       Adafruit_BME280::SAMPLING_X1, // Humidity oversampling
                       Adafruit_BME280::FILTER_OFF, Adafruit_BME280::STANDBY_MS_1000);

    initI2CSensor();
    return status;
}

bool BME280Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;

    LOG_DEBUG("BME280 getMetrics");
    bme280.takeForcedMeasurement();
    measurement->variant.environment_metrics.temperature = bme280.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = bme280.readHumidity();
    measurement->variant.environment_metrics.barometric_pressure = bme280.readPressure() / 100.0F;

    return true;
}
#endif