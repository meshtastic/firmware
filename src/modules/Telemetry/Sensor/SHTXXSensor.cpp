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

    if (sht.init(*_bus)) {
        LOG_INFO("%s: init(): success", sensorName);
        getSensorVariant(sht.mSensorType);
        LOG_INFO("%s Sensor detected: %s", sensorName, sensorVariant);
        status = 1;
    } else {
        LOG_ERROR("%s: init(): failed", sensorName);
    }

    initI2CSensor();
    return status;
}

/**
 * Accuracy setting of measurement.
 * Not all sensors support changing the sampling accuracy (only SHT3X and SHT4X)
 * SHTAccuracy:
 * - SHT_ACCURACY_HIGH: Highest repeatability at the cost of slower measurement
 * - SHT_ACCURACY_MEDIUM: Balanced repeatability and speed of measurement
 * - SHT_ACCURACY_LOW: Fastest measurement but lowest repeatability
 */
bool SHTXXSensor::setAccuracy(SHTSensor::SHTAccuracy newAccuracy)
{
    if (!(sht.mSensorType == SHTSensor::SHTSensorType::SHT3X || sht.mSensorType != SHTSensor::SHTSensorType::SHT4X)) {
        LOG_WARN("%s doesn't support accuracy setting", sensorVariant);
        return false;
    }
    LOG_INFO("%s: setting new accuracy setting", sensorVariant);
    accuracy = newAccuracy;
    return sht.setAccuracy(newAccuracy);
}

bool SHTXXSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (sht.readSample()) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.temperature = sht.getTemperature();
        measurement->variant.environment_metrics.relative_humidity = sht.getHumidity();

        LOG_INFO("%s (%s): Got: temp:%fdegC, hum:%f%rh", sensorName, sensorVariant,
                 measurement->variant.environment_metrics.temperature,
                 measurement->variant.environment_metrics.relative_humidity);

        return true;
    } else {
        LOG_ERROR("%s (%s): read sample failed", sensorName, sensorVariant);
        return false;
    }
}

AdminMessageHandleResult SHTXXSensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                         meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;
    result = AdminMessageHandleResult::NOT_HANDLED;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_sensor_config_tag:
        if (!request->sensor_config.has_shtxx_config) {
            result = AdminMessageHandleResult::NOT_HANDLED;
            break;
        }

        // Check for sensor accuracy setting
        if (request->sensor_config.shtxx_config.has_set_accuracy) {
            SHTSensor::SHTAccuracy newAccuracy;
            if (request->sensor_config.shtxx_config.set_accuracy == 0) {
                newAccuracy = SHTSensor::SHT_ACCURACY_LOW;
            } else if (request->sensor_config.shtxx_config.set_accuracy == 1) {
                newAccuracy = SHTSensor::SHT_ACCURACY_MEDIUM;
            } else if (request->sensor_config.shtxx_config.set_accuracy == 2) {
                newAccuracy = SHTSensor::SHT_ACCURACY_HIGH;
            } else {
                LOG_ERROR("%s: incorrect accuracy setting", sensorName);
                result = AdminMessageHandleResult::HANDLED;
                break;
            }
            this->setAccuracy(newAccuracy);
        }

        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

#endif