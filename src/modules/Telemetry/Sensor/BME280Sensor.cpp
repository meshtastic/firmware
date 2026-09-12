#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BME280.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BME280Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME280.h>
#include <typeinfo>

BME280Sensor::BME280Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BME280, "BME280") {}

static void setSensorConfigs(Adafruit_BME280 *bme280)
{
    bme280->setSampling(Adafruit_BME280::MODE_FORCED,
                       Adafruit_BME280::SAMPLING_X1, // Temp. oversampling
                       Adafruit_BME280::SAMPLING_X1, // Pressure oversampling
                       Adafruit_BME280::SAMPLING_X1, // Humidity oversampling
                       Adafruit_BME280::FILTER_OFF, Adafruit_BME280::STANDBY_MS_1000);
}

bool BME280Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    bus->setTimeout(50);
    LOG_INFO("Init sensor: %s", sensorName);
    status = bme280.begin(dev->address.address, bus);
    if (!status) {
        return status;
    }

    setSensorConfigs(&bme280);

    initI2CSensor();
    return status;
}

static void getSensorData(meshtastic_Telemetry *measurement, Adafruit_BME280 *bme280)
{
    measurement->variant.environment_metrics.temperature = bme280->readTemperature();
    measurement->variant.environment_metrics.relative_humidity = bme280->readHumidity();
    measurement->variant.environment_metrics.barometric_pressure = bme280->readPressure() / 100.0F;
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;
}

bool BME280Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("BME280 getMetrics");
    if(bme280.takeForcedMeasurement())
    {
        getSensorData(measurement, &bme280);
        return true;
    }
    else
    {
        LOG_WARN("BME280 measurement failed, attempting reset.");
        if(bme280.init())
        {
            setSensorConfigs(&bme280);
            LOG_DEBUG("BME280 reset success, getMetrics");
            if(bme280.takeForcedMeasurement())
            {
                getSensorData(measurement, &bme280);
                return true;
            }
            else
            {
                LOG_WARN("BME280 measurement failed again.");
            }
        }
        else
        {
            LOG_WARN("BME280 reset/reinit failed.");
        }
    }
    return false;
}
#endif
