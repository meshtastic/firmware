#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "SHT21Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Wire.h>

// SHT21 I2C address and commands (use no-hold commands to avoid clock-stretch issues)
static const uint8_t CMD_TEMP_NOHOLD = 0xF3;
static const uint8_t CMD_HUM_NOHOLD = 0xF5;

static bool read_sht21_raw(TwoWire *bus, uint8_t addr, uint8_t cmd, uint16_t &raw, uint16_t waitMs)
{
    bus->beginTransmission(addr);
    bus->write(cmd);
    if (bus->endTransmission() != 0)
        return false;
    // Wait for measurement to complete (no-hold mode)
    delay(waitMs);
    int toRead = 3; // two data bytes + CRC
    bus->requestFrom((int)addr, toRead);
    if (bus->available() < 2)
        return false;
    uint8_t msb = bus->read();
    uint8_t lsb = bus->read();
    if (bus->available())
        (void)bus->read();

    raw = ((uint16_t)msb << 8) | lsb;
    // clear status bits per datasheet
    raw &= ~0x0003;
    return true;
}

SHT21Sensor::SHT21Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHT21, "SHT21") {}

bool SHT21Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    i2c = bus;
    i2c_addr = dev->address.address ? dev->address.address : SHT21_ADDR;

    uint16_t raw;
    // use no-hold command and wait for conversion (max ~85ms for temp)
    if (!read_sht21_raw(i2c, i2c_addr, CMD_TEMP_NOHOLD, raw, 85)) {
        return false;
    }
    float temp = -46.85f + 175.72f * ((float)raw / 65536.0f);
    // basic sanity check
    if (temp < -50.0f || temp > 150.0f)
        return false;

    status = true;
    initI2CSensor();
    return true;
}

bool SHT21Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;

    LOG_DEBUG("SHT21 getMetrics");

    uint16_t raw_t = 0, raw_h = 0;
    if (!read_sht21_raw(i2c, i2c_addr, CMD_TEMP_NOHOLD, raw_t, 85))
        return false;
    // humidity max conversion time ~29ms
    if (!read_sht21_raw(i2c, i2c_addr, CMD_HUM_NOHOLD, raw_h, 30))
        return false;

    float temp = -46.85f + 175.72f * ((float)raw_t / 65536.0f);
    float rh = -6.0f + 125.0f * ((float)raw_h / 65536.0f);

    measurement->variant.environment_metrics.temperature = temp;
    measurement->variant.environment_metrics.relative_humidity = rh;

    return true;
}

#endif
