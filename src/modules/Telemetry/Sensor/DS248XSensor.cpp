#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_DS248x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "DS248XSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_DS248x.h>

// Dallas/Maxim CRC8 (reflected polynomial 0x8C), used to validate a DS18B20 scratchpad
static uint8_t ds18b20CRC8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;

    while (len--) {
        uint8_t inbyte = *data++;
        for (uint8_t i = 8; i; i--) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }

    return crc;
}

DS248XSensor::DS248XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_DS248X, "DS248X") {}

ds248x_variant_t DS248XSensor::detectVariant()
{

    // Wait until idle
    if (!ds248x.busyWait(1000)) {
        _variant = DS248X_UNKNOWN;
        return _variant;
    }

    // Try Channel Select command (only valid on DS2482-800)
    if (!ds248x.selectChannel(0)) {
        _variant = DS248X_DS2484;
    } else {
        _variant = DS248X_DS2482_800;
    }

    return _variant;
}

void DS248XSensor::printROM(const uint8_t *rom)
{
    LOG_INFO("%s: ROM found - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", sensorName, rom[0], rom[1], rom[2], rom[3], rom[4],
             rom[5], rom[6], rom[7]);
}

bool DS248XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    _address = dev->address.address;
    _bus = bus;
    _port = dev->address.port;
    LOG_INFO("Init sensor: %s", sensorName);

#ifdef DS248X_I2C_CLOCK_SPEED
    reClockI2C.setup(_bus, _port);

    LOG_INFO("%s: attempting to reclock speed to %uHz", sensorName, DS248X_I2C_CLOCK_SPEED);
    reClockI2C.setClock(DS248X_I2C_CLOCK_SPEED);
#endif /* DS248X_I2C_CLOCK_SPEED */

    if (!ds248x.begin(bus, _address)) {
#ifdef DS248X_I2C_CLOCK_SPEED
        reClockI2C.restoreClock();
#endif /* DS248X_I2C_CLOCK_SPEED */
        return false;
    }

    // Try to init One-Wire with 3 retries. This detects ROMs consistently
    // on the second one.
    uint8_t numRetries = 3;
    uint8_t rom[8]{};

    for (uint8_t retry = 1; retry <= numRetries; retry++) {
        bool initError = false;
        uint8_t nROMDetected = 0;

        detectVariant();

        if (_variant == DS248X_DS2482_800) {

            LOG_INFO("%s: Multi-channel DS2482-800 detected", sensorName);

            for (uint8_t channel = 0; channel < 8; channel++) {

                if (ds248x.selectChannel(channel)) {

                    ds248x.OneWireReset();

                    // Scratch buffer: a failed pass must not erase a ROM found on an earlier one
                    uint8_t foundROM[8]{};
                    if (ds248x.OneWireSearch(foundROM)) {
                        memcpy(ds2482800Data.ds248xData[channel].rom, foundROM, sizeof(foundROM));
                        LOG_INFO("%s: One-wire rom detected on channel %u (%u/%u)", sensorName, channel, retry, numRetries);
                        printROM(ds2482800Data.ds248xData[channel].rom);
                    } else {
                        LOG_DEBUG("%s: no one-wire rom detected on channel %u (%u/%u)", sensorName, channel, retry, numRetries);
                    }

                } else {
                    LOG_WARN("%s: Failed to select channel %u", sensorName, channel);
                }

                // Count every channel holding a ROM, including ones carried over from an earlier pass
                if (isValidROM(ds2482800Data.ds248xData[channel].rom)) {
                    nROMDetected += 1;
                }
            }

            if (!nROMDetected) {
                initError = true;
            }

        } else if (_variant == DS248X_DS2484) {
            LOG_INFO("%s: Single-channel DS2484 detected", sensorName);

            if (!ds248x.OneWireReset()) {
                LOG_WARN("%s: One-wire reset unsuccessful (%u/%u)", sensorName, retry, numRetries);
                initError = true;
            }

            if (ds248x.shortDetected()) {
                LOG_WARN("%s: One-wire short detected (%u/%u)", sensorName, retry, numRetries);
                initError = true;
            }

            if (!ds248x.presencePulseDetected()) {
                LOG_WARN("%s: One-wire no presence pulse detected (%u/%u)", sensorName, retry, numRetries);
                initError = true;
            }

            // TODO - This will detect a ROM and will always read the same throughout runtime for the DS2484
            // If someone connects more than one one-wire temperature sensor, currently it will
            // only read the first one (we only have one temperature to report)
            if (!ds248x.OneWireSearch(ds248xData.rom)) {
                LOG_WARN("%s: no one-wire rom detected (%u/%u)", sensorName, retry, numRetries);
                initError = true;
            } else {
                LOG_INFO("%s: One-wire rom detected (%u/%u)", sensorName, retry, numRetries);
                printROM(ds248xData.rom);
            }
        } else {
            LOG_WARN("%s: Could not determine variant (%u/%u)", sensorName, retry, numRetries);
            initError = true;
        }

        if (initError && retry == numRetries) {
#ifdef DS248X_I2C_CLOCK_SPEED
            reClockI2C.restoreClock();
#endif /* DS248X_I2C_CLOCK_SPEED */
            LOG_ERROR("%s: Max retries for one-wire init (%u/%u). Aborting", sensorName, retry, numRetries);
            return false;
        }

        if (!initError) {
            LOG_INFO("%s: Started one-wire (%u/%u)", sensorName, retry, numRetries);
            status = true;
            // We want to keep searching for ROMs on the DS248X_DS2482_800
            // and always do the three passes
            if (_variant == ds248x_variant_t::DS248X_DS2484) {
                break;
            }
        }
        // TODO Potentially not needed, but taken from Adafruit's library example
        delay(500);
    }

#ifdef DS248X_I2C_CLOCK_SPEED
    LOG_INFO("%s: restoring clock speed", sensorName);
    reClockI2C.restoreClock();
#endif /* DS248X_I2C_CLOCK_SPEED */

    initI2CSensor();
    return status;
}

bool DS248XSensor::isValidROM(const uint8_t *rom)
{
    return (rom[0] || rom[1] || rom[2] || rom[3] || rom[4] || rom[5] || rom[6] || rom[7]);
}

// Read a one-wire temperature sensor by matching it's ROM
float DS248XSensor::readTemperatureROM(const uint8_t *rom)
{
#ifdef DS248X_I2C_CLOCK_SPEED
    LOG_DEBUG("%s: attempting to reclock speed to %uHz", sensorName, DS248X_I2C_CLOCK_SPEED);
    reClockI2C.setClock(DS248X_I2C_CLOCK_SPEED);
#endif /* DS248X_I2C_CLOCK_SPEED */

    uint8_t data[9]{};

    // Select the DS18B20 device
    bool ok = ds248x.OneWireReset() && ds248x.OneWireWriteByte(DS18B20_CMD_MATCH_ROM); // Match ROM command
    for (int i = 0; ok && i < 8; i++) {
        ok = ds248x.OneWireWriteByte(rom[i]);
    }

    // Start temperature conversion
    ok = ok && ds248x.OneWireWriteByte(DS18B20_CMD_CONVERT_T); // Convert T command

    if (ok) {
        delay(750); // Wait for conversion (750ms for maximum precision)

        // Read scratchpad
        ok = ds248x.OneWireReset() && ds248x.OneWireWriteByte(DS18B20_CMD_MATCH_ROM); // Match ROM command
        for (int i = 0; ok && i < 8; i++) {
            ok = ds248x.OneWireWriteByte(rom[i]);
        }
        ok = ok && ds248x.OneWireWriteByte(DS18B20_CMD_READ_SCRATCHPAD); // Read Scratchpad command

        for (int i = 0; ok && i < 9; i++) {
            ok = ds248x.OneWireReadByte(&data[i]);
        }
    }

#ifdef DS248X_I2C_CLOCK_SPEED
    LOG_DEBUG("%s: restoring clock speed", sensorName);
    reClockI2C.restoreClock();
#endif /* DS248X_I2C_CLOCK_SPEED */

    if (!ok) {
        LOG_WARN("%s: One-wire transaction failed", sensorName);
        return DS248X_INVALID_TEMPERATURE;
    }

    // The scratchpad ends with a CRC8 over its first eight bytes
    if (ds18b20CRC8(data, 8) != data[8]) {
        LOG_WARN("%s: Scratchpad CRC mismatch", sensorName);
        return DS248X_INVALID_TEMPERATURE;
    }

    // Calculate temperature
    int16_t raw = (data[1] << 8) | data[0];
    float celsius = (float)raw / 16.0;

    return celsius;
}

bool DS248XSensor::readTemperatureChannel(uint8_t channel)
{
    if (!isValidROM(ds2482800Data.ds248xData[channel].rom)) {
        LOG_DEBUG("%s: No ROM in channel %u", sensorName, channel);
        return false;
    }
    // Select the channel on the DS2482-800
    if (!ds248x.selectChannel(channel)) {
        // Handle error if channel selection fails
        LOG_WARN("%s: Failed to select channel %u", sensorName, channel);
        return false;
    }

    float temperature;
    temperature = readTemperatureROM(ds2482800Data.ds248xData[channel].rom);

    if (temperature == DS248X_INVALID_TEMPERATURE) {
        LOG_WARN("%s: Failed to read temperature in channel %u", sensorName, channel);
        return false;
    }

    ds2482800Data.ds248xData[channel].temperature = temperature;
    LOG_DEBUG("%s: read temperature in channel %u: %0.2f", sensorName, channel, temperature);
    return true;
}

bool DS248XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (_variant == ds248x_variant_t::DS248X_DS2484) {
        float temperature = readTemperatureROM(ds248xData.rom);
        if (temperature != DS248X_INVALID_TEMPERATURE) {
            measurement->variant.environment_metrics.temperature = temperature;
            measurement->variant.environment_metrics.has_temperature = true;
            LOG_DEBUG("Got %s readings: temperature=%.2f", sensorName, measurement->variant.environment_metrics.temperature);
            return true;
        }
    } else if (_variant == ds248x_variant_t::DS248X_DS2482_800) {
        // Only ch0 is reported, and each populated channel blocks 750ms on its conversion
        // TODO Support more than one temperature via repeated (3.0)
        // TODO Select which channel can be reported as main temperature
        if (readTemperatureChannel(0)) {
            measurement->variant.environment_metrics.temperature = ds2482800Data.ds248xData[0].temperature;
            measurement->variant.environment_metrics.has_temperature = true;
            LOG_DEBUG("Got %s readings: temperature=%.2f", sensorName, measurement->variant.environment_metrics.temperature);
            return true;
        }
        return false;
    }
    return false;
}

#endif