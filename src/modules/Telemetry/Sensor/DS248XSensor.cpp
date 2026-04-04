#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_DS248x.h>)

#include "../detect/reClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "DS248XSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_DS248x.h>

DS248XSensor::DS248XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_DS248X, "DS248X") {}

ds248x_variant_t DS248XSensor::detectVariant()
{

    // Wait until idle
    if (!ds248x.busyWait(1000)) {
        return DS248X_UNKNOWN;
    }

    // Try Channel Select command (only valid on DS2482-800)
    if (!ds248x.selectChannel(0)) {
        _variant = DS248X_DS2484;
    } else {
        _variant = DS248X_DS2482_800;
    }

    return _variant;
}

void DS248XSensor::printROM(uint8_t *rom)
{
    LOG_INFO("%s: ROM found - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", sensorName, rom[0], rom[1], rom[2], rom[3], rom[4],
             rom[5], rom[6], rom[7]);
}

bool DS248XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    _address = dev->address.address;
    _bus = bus;
    LOG_INFO("Init sensor: %s", sensorName);

#ifdef DS248X_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(DS248X_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(DS248X_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* DS248X_I2C_CLOCK_SPEED */

    // #ifdef DS248X_I2C_CLOCK_SPEED
    //     uint32_t currentClock = reClockI2C(DS248X_I2C_CLOCK_SPEED, _bus, false);
    // #endif /* DS248X_I2C_CLOCK_SPEED */

    if (!ds248x.begin(bus, _address)) {
#if defined(DS248X_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
        reClockI2C(currentClock, _bus, false);
#endif
        return false;
    }

    // Try to init One-Wire with max retries
    uint8_t numRetries = 3;
    uint8_t rom[8]{};
    for (uint8_t retry = 0; retry < numRetries; retry++) {
        bool initError = false;
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

        // TODO - This will detect a ROM and will always read the same throughout runtime
        // If someone connects more than one one-wire temperature sensor, currently it will
        // only read the first one (we only have one temperature to report)
        if (!ds248x.OneWireSearch(ds248xData.rom)) {
            LOG_WARN("%s: no one-wire rom detected (%u/%u)", sensorName, retry, numRetries);
            initError = true;
        } else {
            LOG_INFO("%s: One-wire rom detected (%u/%u)", sensorName, retry, numRetries);
            printROM(ds248xData.rom);
        }

        if (detectVariant() == DS248X_DS2482_800) {
            LOG_INFO("%s: Multi-channel DS2482-800 detected", sensorName);
        } else {
            LOG_INFO("%s: Single-channel DS2484 detected", sensorName);
        }

        if (initError && retry == numRetries - 1) {
#if defined(DS248X_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
            reClockI2C(currentClock, _bus, false);
#endif
            LOG_WARN("%s: Max retries for one-wire init (%u/%u). Aborting", sensorName, retry, numRetries);
            return false;
        }

        if (!initError) {
            LOG_INFO("%s: Started one-wire (%u/%u)", sensorName, retry, numRetries);
            status = true;
            break;
        }
        delay(500);
    }

    initI2CSensor();
    return status;
}

// Read a one-wire temperature sensor by matching it's ROM
bool DS248XSensor::readTemperatureROM(uint8_t *rom)
{
#ifdef DS248X_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(DS248X_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(DS248X_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* DS248X_I2C_CLOCK_SPEED */

    // #ifdef DS248X_I2C_CLOCK_SPEED
    //     uint32_t currentClock = reClockI2C(DS248X_I2C_CLOCK_SPEED, _bus, false);
    // #endif /* DS248X_I2C_CLOCK_SPEED */

    // Select the DS18B20 device
    ds248x.OneWireReset();
    ds248x.OneWireWriteByte(DS18B20_CMD_MATCH_ROM); // Match ROM command
    for (int i = 0; i < 8; i++) {
        ds248x.OneWireWriteByte(rom[i]);
    }

    // Start temperature conversion
    ds248x.OneWireWriteByte(DS18B20_CMD_CONVERT_T); // Convert T command
    delay(750);                                     // Wait for conversion (750ms for maximum precision)

    // Read scratchpad
    ds248x.OneWireReset();
    ds248x.OneWireWriteByte(DS18B20_CMD_MATCH_ROM); // Match ROM command
    for (int i = 0; i < 8; i++) {
        ds248x.OneWireWriteByte(rom[i]);
    }
    ds248x.OneWireWriteByte(DS18B20_CMD_READ_SCRATCHPAD); // Read Scratchpad command

    uint8_t data[9];
    for (int i = 0; i < 9; i++) {
        ds248x.OneWireReadByte(&data[i]);
    }

#if defined(DS248X_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    // Calculate temperature
    int16_t raw = (data[1] << 8) | data[0];
    float celsius = (float)raw / 16.0;

    ds248xData.temperature = celsius;
    return true;
}

bool DS248XSensor::readTemperatureChannel(uint8_t channel)
{
    // Select the channel on the DS2482-800
    if (!ds248x.selectChannel(channel)) {
        // Handle error if channel selection fails
        LOG_WARN("%s: Failed to select channel %u", sensorName, channel);
        return false;
    }

    // Start temperature conversion
    ds248x.OneWireReset();
    ds248x.OneWireWriteByte(DS18B20_CMD_SKIP_ROM);  // Skip ROM command
    ds248x.OneWireWriteByte(DS18B20_CMD_CONVERT_T); // Convert T command
    delay(750);                                     // Wait for conversion (750ms for maximum precision)

    // Read scratchpad
    ds248x.OneWireReset();
    ds248x.OneWireWriteByte(DS18B20_CMD_SKIP_ROM);        // Skip ROM command
    ds248x.OneWireWriteByte(DS18B20_CMD_READ_SCRATCHPAD); // Read Scratchpad command

    uint8_t data[9];
    for (int i = 0; i < 9; i++) {
        ds248x.OneWireReadByte(&data[i]);
    }

    // Calculate temperature
    int16_t raw = (data[1] << 8) | data[0];
    float celsius = (float)raw / 16.0;

    ds2482800Data.ds248xData[channel].temperature = celsius;
    LOG_DEBUG("%s: read temperature in channel %u: %0.2f", sensorName, channel, celsius);
    return true;
}

bool DS248XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (_variant == ds248x_variant_t::DS248X_DS2484) {
        if (readTemperatureROM(ds248xData.rom)) {
            measurement->variant.environment_metrics.temperature = ds248xData.temperature;
            measurement->variant.environment_metrics.has_temperature = true;
            LOG_DEBUG("Got %s readings: temperature=%.2f", sensorName, measurement->variant.environment_metrics.temperature);
            return true;
        }
    } else if (_variant == ds248x_variant_t::DS248X_DS2482_800) {
        // If using DS248X_DS2482_800, we read all channels, but we will only report ch0
        bool readChannel0 = false;
        for (uint8_t channel = 0; channel < 8; channel++) {
            if (readTemperatureChannel(channel)) {
                // TODO Support more than one temperature via repeated (3.0)
                if (channel == 0) {
                    readChannel0 = true;
                    measurement->variant.environment_metrics.temperature = ds2482800Data.ds248xData[0].temperature;
                    measurement->variant.environment_metrics.has_temperature = true;
                    LOG_DEBUG("Got %s readings: temperature=%.2f", sensorName,
                              measurement->variant.environment_metrics.temperature);
                }
            }
        }
        return readChannel0;
    }
    return false;
}

#endif