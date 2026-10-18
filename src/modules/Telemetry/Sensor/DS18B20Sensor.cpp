#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ARCH_ESP32) && defined(PIN_DS18B20_DATA)

#include "DS18B20Sensor.h"
#include "TelemetrySensor.h"
#include "soc/gpio_struct.h"
#include <Arduino.h>

// DS18B20 command set (see Maxim datasheet)
#define DS18B20_CMD_SKIP_ROM 0xCC
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

// 12-bit conversion time (max)
#define DS18B20_CONVERSION_TIME_MS 750

// Dallas/Maxim CRC8 (reflected polynomial 0x8C) to validate the scratchpad
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

// ---------------------------------------------------------------------------------
// Direct GPIO register access: the Arduino digitalWrite/pinMode path has multi-
// microsecond overhead and occasional flash-cache stalls, which breaks 1-Wire
// slot timing (max 15us low for a write-1). Single-write instructions keep
// slots deterministic.
//
// ESP32 GPIOs live in two register banks (0-31 and 32+); select the right one
// at compile time so any pin works.
// ---------------------------------------------------------------------------------
#if PIN_DS18B20_DATA < 32
#define OW_MASK (1u << PIN_DS18B20_DATA)
#define OW_OUTPUT_REG_LOW() (GPIO.out_w1tc = OW_MASK)
#define OW_OUTPUT_ENABLE() (GPIO.enable_w1ts = OW_MASK)
#define OW_OUTPUT_DISABLE() (GPIO.enable_w1tc = OW_MASK)
#define OW_INPUT_READ() ((GPIO.in >> PIN_DS18B20_DATA) & 0x1)
#else
#define OW_MASK (1u << (PIN_DS18B20_DATA - 32))
#define OW_OUTPUT_REG_LOW() (GPIO.out1_w1tc = OW_MASK)
#define OW_OUTPUT_ENABLE() (GPIO.enable1_w1ts = OW_MASK)
#define OW_OUTPUT_DISABLE() (GPIO.enable1_w1tc = OW_MASK)
#define OW_INPUT_READ() ((GPIO.in1.val >> (PIN_DS18B20_DATA - 32)) & 0x1)
#endif

// Drive the line low (output enabled + low)
#define OW_DRIVE_LOW()                                                                                                           \
    do {                                                                                                                         \
        OW_OUTPUT_REG_LOW(); /* set output level low first */                                                                    \
        OW_OUTPUT_ENABLE();  /* then enable output driver */                                                                     \
    } while (0)

// Release the bus (disable output driver; external 4.7k pull-up brings it high)
#define OW_RELEASE() OW_OUTPUT_DISABLE()

// Read the line state
#define OW_READ() OW_INPUT_READ()

DS18B20Sensor::DS18B20Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "DS18B20") {}

// --- Low-level 1-Wire primitives -------------------------------------------------

bool IRAM_ATTR DS18B20Sensor::oneWireReset()
{
    // Master pull low for 480us, then release and sample for the presence pulse
    noInterrupts();
    OW_DRIVE_LOW();
    interrupts(); // 480us is orders of magnitude longer than any ISR jitter
    delayMicroseconds(480);

    noInterrupts();
    OW_RELEASE();
    delayMicroseconds(70);
    bool presence = (OW_READ() == 0); // device pulls low = present
    interrupts();
    delayMicroseconds(410); // complete the reset sequence

    return presence;
}

void IRAM_ATTR DS18B20Sensor::oneWireWriteByte(uint8_t b)
{
    for (uint8_t i = 0; i < 8; i++) {
        noInterrupts();
        OW_DRIVE_LOW();
        if ((b >> i) & 0x01) {
            // Write 1: pull low 1-15us, then release for the rest of the slot
            delayMicroseconds(6);
            OW_RELEASE();
        } else {
            // Write 0: hold low for the full 60us+ slot
            delayMicroseconds(60);
            OW_RELEASE();
        }
        interrupts();
        // Recovery: the total slot (low + released) must be >=60us, plus >=1us
        // high recovery time before the next falling edge.
        delayMicroseconds(64);
    }
}

bool IRAM_ATTR DS18B20Sensor::oneWireReadBit()
{
    bool bit;

    noInterrupts();
    OW_DRIVE_LOW();
    delayMicroseconds(3); // initiate read slot (min 1us)
    OW_RELEASE();
    delayMicroseconds(8); // data valid for 15us from slot start
    bit = OW_READ();      // sample well inside the window
    interrupts();
    delayMicroseconds(55); // complete the read slot

    return bit;
}

uint8_t DS18B20Sensor::oneWireReadByte()
{
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8; i++) {
        b >>= 1;
        if (oneWireReadBit()) {
            b |= 0x80;
        }
    }
    return b;
}

// ---------------------------------------------------------------------------------

float DS18B20Sensor::readTemperature()
{
    // 1. Reset + Skip ROM (single sensor on the bus) + start conversion
    if (!oneWireReset()) {
        LOG_WARN("%s: 1-Wire reset failed, no presence pulse", sensorName);
        return INVALID_TEMPERATURE;
    }
    oneWireWriteByte(DS18B20_CMD_SKIP_ROM);
    oneWireWriteByte(DS18B20_CMD_CONVERT_T);

    // 2. Wait for the 12-bit conversion. Bus-polling only works in parasite power mode;
    //    with a properly powered sensor (3-wire) we must simply wait the max time.
    //    (Same approach as the DS248X driver in this codebase.)
    delay(DS18B20_CONVERSION_TIME_MS);

    // 3. Reset + Skip ROM + read scratchpad (9 bytes: 2 temp, 2 TH/TL, 3 config, 1 resv, 1 CRC)
    if (!oneWireReset()) {
        LOG_WARN("%s: 1-Wire reset failed before scratchpad read", sensorName);
        return INVALID_TEMPERATURE;
    }
    oneWireWriteByte(DS18B20_CMD_SKIP_ROM);
    oneWireWriteByte(DS18B20_CMD_READ_SCRATCHPAD);

    uint8_t data[9];
    bool allZero = true;
    for (uint8_t i = 0; i < 9; i++) {
        data[i] = oneWireReadByte();
        if (data[i] != 0) {
            allZero = false;
        }
    }

    // An all-zero scratchpad (which passes CRC by coincidence) means the bus is
    // shorted or the sensor is absent; report failure instead of a bogus 0 C.
    if (allZero) {
        LOG_WARN("%s: scratchpad all zeros (bus shorted or sensor absent)", sensorName);
        return INVALID_TEMPERATURE;
    }

    // 4. Validate CRC over the first 8 bytes
    if (ds18b20CRC8(data, 8) != data[8]) {
        LOG_WARN("%s: scratchpad CRC mismatch: %02X %02X %02X %02X %02X %02X %02X %02X CRC=%02X (want %02X)", sensorName, data[0],
                 data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], ds18b20CRC8(data, 8));
        return INVALID_TEMPERATURE;
    }

    // 5. Reject the power-on reset value: a DS18B20 that lost power or was
    //    re-plugged reports exactly +85.0 C (raw 0x0550) until the first
    //    conversion completes. Since we always issue CONVERT_T and wait the
    //    full conversion time before reading, seeing 0x0550 here means the
    //    conversion did not actually run (brownout, wiring fault).
    int16_t raw = (int16_t)((data[1] << 8) | data[0]);
    if (raw == 0x0550) {
        LOG_WARN("%s: power-on reset value +85.0C (conversion did not complete)", sensorName);
        return INVALID_TEMPERATURE;
    }

    // 6. Convert raw reading to Celsius (12-bit resolution: 1/16 C per LSB)
    return (float)raw / 16.0f;
}

bool DS18B20Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s (GPIO%d, bit-banged 1-Wire)", sensorName, PIN_DS18B20_DATA);

    // The bus argument is unused (not an I2C sensor); we drive GPIO directly
    (void)bus;
    (void)dev;

    // Enable the pin's input buffer (IO_MUX FUN_IE) via Arduino pinMode, then use
    // direct register access for the timing-critical 1-Wire primitives.
    pinMode(PIN_DS18B20_DATA, INPUT);
    OW_RELEASE(); // idle high via external 4.7k pull-up

    // Try up to 3 times: 1-Wire detection can be flaky on the first attempt
    for (uint8_t retry = 1; retry <= 3; retry++) {
        if (oneWireReset()) {
            LOG_INFO("%s: sensor detected (presence pulse) (%u/3)", sensorName, retry);
            status = true;
            return status;
        }
        LOG_WARN("%s: no presence pulse (%u/3)", sensorName, retry);
        delay(100);
    }

    LOG_ERROR("%s: no sensor found on GPIO%d. Check wiring and 4.7k pull-up", sensorName, PIN_DS18B20_DATA);
    return false;
}

bool DS18B20Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float temperature = INVALID_TEMPERATURE;

    // Retry once: a single transient CRC error shouldn't drop the whole reading
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        temperature = readTemperature();
        if (temperature != INVALID_TEMPERATURE) {
            break;
        }
    }

    if (temperature == INVALID_TEMPERATURE) {
        LOG_WARN("%s: read failed", sensorName);
        return false;
    }

    measurement->variant.environment_metrics.temperature = temperature;
    measurement->variant.environment_metrics.has_temperature = true;
    LOG_DEBUG("Got %s readings: temperature=%.2f", sensorName, measurement->variant.environment_metrics.temperature);
    return true;
}

#endif
