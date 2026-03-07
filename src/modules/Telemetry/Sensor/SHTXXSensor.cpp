#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SHTSensor.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SHTXXSensor.h"
#include "TelemetrySensor.h"
#include <SHTSensor.h>

SHTXXSensor::SHTXXSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHTXX, "SHTXX") {}

void SHTXXSensor::getSensorVariant(SHTSensor::SHTSensorType sensorType)
{
    switch (sensorType) {
    case SHTSensor::SHTSensorType::SHT2X:
        sensorVariant = "SHT2x";
        break;

    case SHTSensor::SHTSensorType::SHT3X:
    case SHTSensor::SHTSensorType::SHT85:
        sensorVariant = "SHT3x/SHT85";
        break;

    case SHTSensor::SHTSensorType::SHT3X_ALT:
        sensorVariant = "SHT3x";
        break;

    case SHTSensor::SHTSensorType::SHTW1:
    case SHTSensor::SHTSensorType::SHTW2:
    case SHTSensor::SHTSensorType::SHTC1:
    case SHTSensor::SHTSensorType::SHTC3:
        sensorVariant = "SHTC1/SHTC3/SHTW1/SHTW2";
        break;

    case SHTSensor::SHTSensorType::SHT4X:
        sensorVariant = "SHT4x";
        break;

    default:
        sensorVariant = "Unknown";
        break;
    }
}

bool SHTXXSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    _bus = bus;
    _address = dev->address.address;

    if (sht.init()) {
        LOG_INFO("%s: init(): success", sensorName);
        getSensorVariant(sht.mSensorType);
        LOG_INFO("%s Sensor detected: %s", sensorName, sensorVariant);
        status = 1;
    } else {
        LOG_ERROR("%s: init(): failed\n", sensorName);
    }

    initI2CSensor();
    return status;
}

bool SHTXXSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (sht.readSample()) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.temperature = sht.getTemperature();
        measurement->variant.environment_metrics.relative_humidity = sht.getHumidity();

        LOG_INFO("%s: Got: temp:%fdegC, hum:%f%rh", sensorName, measurement->variant.environment_metrics.temperature,
                 measurement->variant.environment_metrics.relative_humidity);

        return true;
    } else {
        LOG_ERROR("%s: read sample failed", sensorName);
        return false;
    }
}

#endif