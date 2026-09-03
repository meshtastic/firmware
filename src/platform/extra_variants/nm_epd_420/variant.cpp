#include "configuration.h"

#if defined(NM_EPD_420_BW)
#include <Wire.h>
#endif

#ifdef _VARIANT_nm_epd_420

#if defined(NM_EPD_420_BW)
namespace {

constexpr uint8_t ES8311_ADDRESS = 0x18;

bool es8311Write(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(ES8311_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    const bool ok = Wire.endTransmission() == 0;
    if (!ok)
        LOG_WARN("NM-EPD-420: ES8311 write failed reg=0x%02x", reg);
    return ok;
}

bool es8311Probe()
{
    Wire.beginTransmission(ES8311_ADDRESS);
    return Wire.endTransmission() == 0;
}

void audioCodecPowerOn()
{
    digitalWrite(PIN_ES8311_POWER, HIGH);
    delay(10);
}

void audioCodecConfigurePlayback()
{
    static constexpr uint8_t playbackRegisters[][2] = {
        {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x20},
        {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0x80},
        {0x01, 0x3F}, {0x07, 0x00}, {0x08, 0xFF}, {0x06, 0x03}, {0x09, 0x0C}, {0x0A, 0x0C},
        {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58}, {0x17, 0xBF}, {0x0E, 0x02},
        {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00},
        {0x32, 0xD3}, {0x31, 0x00},
    };

    for (const auto &entry : playbackRegisters)
        es8311Write(entry[0], entry[1]);
}

void audioCodecPowerDown()
{
    es8311Write(0x32, 0x00);
    es8311Write(0x17, 0x00);
    es8311Write(0x0E, 0xFF);
    es8311Write(0x12, 0x02);
    es8311Write(0x14, 0x00);
    es8311Write(0x0D, 0xFA);
    es8311Write(0x15, 0x00);
    es8311Write(0x45, 0x01);
    digitalWrite(PIN_ES8311_POWER, LOW);
}

} // namespace
#endif

void earlyInitVariant()
{
    pinMode(PIN_AMP_ENABLE, OUTPUT);
    digitalWrite(PIN_AMP_ENABLE, LOW);

    pinMode(PIN_ES8311_POWER, OUTPUT);
    digitalWrite(PIN_ES8311_POWER, LOW);

    pinMode(AHTX0_POWER_PIN, OUTPUT);
    digitalWrite(AHTX0_POWER_PIN, HIGH);
}

void lateInitVariant()
{
#if defined(NM_EPD_420_BW)
    audioCodecPowerOn();
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!es8311Probe()) {
        LOG_WARN("NM-EPD-420: ES8311 not found at 0x18");
        audioCodecPowerDown();
        digitalWrite(PIN_AMP_ENABLE, LOW);
        digitalWrite(AHTX0_POWER_PIN, LOW);
        return;
    }
    LOG_INFO("NM-EPD-420: ES8311 detected at 0x18");
    audioCodecConfigurePlayback();
    digitalWrite(PIN_AMP_ENABLE, LOW);
#endif
    digitalWrite(AHTX0_POWER_PIN, LOW);
}

#endif
