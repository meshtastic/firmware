#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "HDC1080Sensor.h"
#include "TelemetrySensor.h"
#include "meshUtils.h"
#include <Wire.h>

#define HDC1080_REG_TEMP 0x00
#define HDC1080_REG_HUMIDITY 0x01
#define HDC1080_REG_CONFIG 0x02
#define HDC1080_CONFIG_ACQ_SEQ 0x1000

HDC1080Sensor::HDC1080Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_HDC1080, "HDC1080") {}

bool HDC1080Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    _bus = bus;
    _address = dev->address.address;
    _port = dev->address.port;

    _bus->beginTransmission(_address);
    _bus->write(HDC1080_REG_CONFIG);
    _bus->write((uint8_t)(HDC1080_CONFIG_ACQ_SEQ >> 8));
    _bus->write((uint8_t)(HDC1080_CONFIG_ACQ_SEQ & 0xFF));
    status = (_bus->endTransmission() == 0);

    initI2CSensor();
    return status;
}

bool HDC1080Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!_bus || !status) {
        return false;
    }

    _bus->beginTransmission(_address);
    _bus->write(HDC1080_REG_TEMP);
    if (_bus->endTransmission() != 0) {
        LOG_WARN("HDC1080 trigger failed");
        return false;
    }

    delay(15);

    if (_bus->requestFrom(_address, (uint8_t)4) != 4) {
        LOG_WARN("HDC1080 read failed");
        return false;
    }

    uint16_t rawTemp = ((uint16_t)_bus->read() << 8) | _bus->read();
    uint16_t rawHum = ((uint16_t)_bus->read() << 8) | _bus->read();

    float tempC = (rawTemp / 65536.0f) * 165.0f - 40.0f;
    float hum = (rawHum / 65536.0f) * 100.0f;

    LOG_DEBUG("HDC1080 temp=%.2f C, hum=%.2f %%", tempC, hum);

    if (!measurement->variant.environment_metrics.has_temperature) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.temperature = tempC;
    }

    if (!measurement->variant.environment_metrics.has_relative_humidity) {
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.relative_humidity = clamp(hum, 0.0f, 100.0f);
    }

    return true;
}

#endif
