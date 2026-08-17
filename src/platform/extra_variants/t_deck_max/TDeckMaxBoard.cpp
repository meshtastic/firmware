#include "configuration.h"

#if defined(_VARIANT_T_DECK_MAX)

#include "TDeckMaxBoard.h"

#include <Arduino.h>
#include <Wire.h>

// SensorLib ships a same-named header; this board layer needs Meshtastic's adapter.
#include "TDeckMaxXL9555.hpp"
#include "../../../SafeFile.h"
#include "../../../SPILock.h"
#include "../../../concurrency/LockGuard.h"

#include <ErriezCRC32.h>
#include <cstddef>
#include <cstring>

namespace
{
using t_deck_max::Xl9555Pin;
using t_deck_max::Antenna;

constexpr char ANTENNA_PREFERENCE_FILE[] = "/prefs/t_deck_max.dat";

Antenna currentAntenna = t_deck_max::DEFAULT_ANTENNA;

bool setExpanderOutput(Xl9555Pin pin, bool value)
{
    if (!io.isReady())
        return false;
    io.pinMode(static_cast<uint8_t>(pin), OUTPUT);
    io.digitalWrite(static_cast<uint8_t>(pin), value ? HIGH : LOW);
    return true;
}

void configureNativePins()
{
    pinMode(t_deck_max::LORA_CS_PIN, OUTPUT);
    digitalWrite(t_deck_max::LORA_CS_PIN, HIGH);
    pinMode(t_deck_max::SDCARD_CS_PIN, OUTPUT);
    digitalWrite(t_deck_max::SDCARD_CS_PIN, HIGH);
    pinMode(t_deck_max::EINK_CS_PIN, OUTPUT);
    digitalWrite(t_deck_max::EINK_CS_PIN, HIGH);
    pinMode(t_deck_max::EINK_RESET_PIN, OUTPUT);
    digitalWrite(t_deck_max::EINK_RESET_PIN, HIGH);
    pinMode(t_deck_max::EINK_DC_PIN, OUTPUT);
    digitalWrite(t_deck_max::EINK_DC_PIN, LOW);
    pinMode(t_deck_max::EINK_BUSY_PIN, INPUT);
    pinMode(t_deck_max::EINK_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(t_deck_max::EINK_BACKLIGHT_PIN, LOW);
    pinMode(t_deck_max::KEYBOARD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(t_deck_max::KEYBOARD_BACKLIGHT_PIN, LOW);
    pinMode(t_deck_max::TOUCH_INT_PIN, INPUT_PULLUP);
    pinMode(t_deck_max::KEYBOARD_INT_PIN, INPUT_PULLUP);
    pinMode(t_deck_max::GPS_PPS_PIN, INPUT);
    pinMode(t_deck_max::MODEM_RI_PIN, INPUT);
    pinMode(t_deck_max::MODEM_DTR_PIN, OUTPUT);
    digitalWrite(t_deck_max::MODEM_DTR_PIN, LOW);
}

Antenna loadAntennaPreference(bool &loaded)
{
    loaded = false;

#ifdef FSCom
    if (!spiLock)
        return t_deck_max::DEFAULT_ANTENNA;

    concurrency::LockGuard guard(spiLock);
    auto file = FSCom.open(ANTENNA_PREFERENCE_FILE, FILE_O_READ);
    if (!file)
        return t_deck_max::DEFAULT_ANTENNA;

    t_deck_max::AntennaPreferenceFile preference{};
    const bool readOk = file.read(reinterpret_cast<uint8_t *>(&preference), sizeof(preference)) == sizeof(preference);
    file.close();

    if (!readOk || !t_deck_max::isValidAntennaPreference(preference)) {
        LOG_WARN("T-Deck-MAX: invalid antenna preference, using internal antenna");
        return t_deck_max::DEFAULT_ANTENNA;
    }

    if (crc32Buffer(&preference, offsetof(t_deck_max::AntennaPreferenceFile, crc)) != preference.crc) {
        LOG_WARN("T-Deck-MAX: antenna preference CRC mismatch, using internal antenna");
        return t_deck_max::DEFAULT_ANTENNA;
    }

    loaded = true;
    return static_cast<Antenna>(preference.antenna);
#else
    return t_deck_max::DEFAULT_ANTENNA;
#endif
}
} // namespace

void tDeckMaxSetModemPower(bool on)
{
    setExpanderOutput(Xl9555Pin::ModemPower, on);
}

void tDeckMaxSetModemPwrKey(bool high)
{
    setExpanderOutput(Xl9555Pin::ModemPwrKey, high);
}

void tDeckMaxSetAudioRoute(bool a7682e)
{
    setExpanderOutput(Xl9555Pin::AudioRoute, a7682e);
}

void tDeckMaxSetAmplifier(bool on)
{
    setExpanderOutput(Xl9555Pin::Amplifier, on);
}

void tDeckMaxSetLoRaPower(bool on)
{
    setExpanderOutput(Xl9555Pin::LoraPower, on);
}

void tDeckMaxSetGpsPower(bool on)
{
    setExpanderOutput(Xl9555Pin::GpsPower, on);
}

void tDeckMaxSetImuPower(bool on)
{
    setExpanderOutput(Xl9555Pin::ImuPower, on);
}

void tDeckMaxSetMotorPower(bool on)
{
    setExpanderOutput(Xl9555Pin::MotorPower, on);
}

void tDeckMaxSetAntennaInternal(bool internal)
{
    setExpanderOutput(Xl9555Pin::AntennaSelect, internal);
}

t_deck_max::Antenna tDeckMaxGetAntenna()
{
    return currentAntenna;
}

bool tDeckMaxSetAntenna(t_deck_max::Antenna antenna)
{
    if (!t_deck_max::isValidAntenna(antenna) ||
        !setExpanderOutput(Xl9555Pin::AntennaSelect, t_deck_max::antennaSelectPinLevel(antenna))) {
        return false;
    }

    currentAntenna = antenna;
    return true;
}

bool tDeckMaxLoadAntenna()
{
    bool loaded = false;
    const Antenna antenna = loadAntennaPreference(loaded);
    if (!tDeckMaxSetAntenna(antenna)) {
        LOG_ERROR("T-Deck-MAX: unable to apply antenna preference");
        return false;
    }

    LOG_INFO("T-Deck-MAX: antenna=%s%s", antenna == Antenna::Internal ? "internal" : "external",
             loaded ? " (saved)" : " (default)");
    return loaded;
}

bool tDeckMaxSaveAntenna()
{
#ifdef FSCom
    if (!spiLock)
        return false;

    {
        concurrency::LockGuard guard(spiLock);
        FSCom.mkdir("/prefs");
    }

    t_deck_max::AntennaPreferenceFile preference{};
    preference.magic = t_deck_max::ANTENNA_PREFERENCE_MAGIC;
    preference.version = t_deck_max::ANTENNA_PREFERENCE_VERSION;
    preference.antenna = static_cast<uint8_t>(currentAntenna);
    preference.crc = crc32Buffer(&preference, offsetof(t_deck_max::AntennaPreferenceFile, crc));

    SafeFile file(ANTENNA_PREFERENCE_FILE, true);
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&preference), sizeof(preference));
    const bool closed = file.close();
    if (!closed || written != sizeof(preference)) {
        LOG_WARN("T-Deck-MAX: failed to save antenna preference");
        return false;
    }

    return true;
#else
    return false;
#endif
}

void tDeckMaxResetTouch()
{
    setExpanderOutput(Xl9555Pin::TouchReset, false);
    delay(20);
    setExpanderOutput(Xl9555Pin::TouchReset, true);
    delay(80);
}

void tDeckMaxResetKeyboard()
{
    setExpanderOutput(Xl9555Pin::KeyboardReset, false);
    delay(10);
    setExpanderOutput(Xl9555Pin::KeyboardReset, true);
    delay(10);
}

void tDeckMaxSetSafeState()
{
    if (io.isReady())
        io.setSafeState();

    setExpanderOutput(Xl9555Pin::ModemPower, false);
    setExpanderOutput(Xl9555Pin::LoraPower, false);
    setExpanderOutput(Xl9555Pin::GpsPower, false);
    setExpanderOutput(Xl9555Pin::ImuPower, false);
    setExpanderOutput(Xl9555Pin::AntennaSelect, true);
    setExpanderOutput(Xl9555Pin::MotorPower, false);
    setExpanderOutput(Xl9555Pin::Amplifier, false);
    setExpanderOutput(Xl9555Pin::TouchReset, true);
    setExpanderOutput(Xl9555Pin::ModemPwrKey, false);
    setExpanderOutput(Xl9555Pin::KeyboardReset, true);
    setExpanderOutput(Xl9555Pin::AudioRoute, false);
    digitalWrite(t_deck_max::MODEM_DTR_PIN, LOW);
}

void tDeckMaxInit()
{
    configureNativePins();

    if (!io.begin(Wire, t_deck_max::I2C_ADDRESS, -1, -1)) {
        LOG_ERROR("T-Deck-MAX: XL9555 0x%02x initialization failed", t_deck_max::I2C_ADDRESS);
        return;
    }

    tDeckMaxSetSafeState();
    tDeckMaxResetKeyboard();
    tDeckMaxSetLoRaPower(true);
    tDeckMaxSetImuPower(true);
    tDeckMaxSetMotorPower(t_deck_max::MOTOR_POWER_ON_AT_BOOT);
    tDeckMaxLoadAntenna();
    LOG_INFO("T-Deck-MAX: XL9555 0x%02x initialized", t_deck_max::I2C_ADDRESS);
}

void initVariantAfterI2C()
{
    tDeckMaxInit();
}

bool tDeckMaxRecoverI2C()
{
    const bool ended = Wire.end();
    const bool started = Wire.begin(t_deck_max::I2C_SDA_PIN, t_deck_max::I2C_SCL_PIN);

    if (!ended || !started) {
        LOG_ERROR("T-Deck-MAX: I2C bus recovery failed (end=%d begin=%d)", ended, started);
        return false;
    }

    LOG_INFO("T-Deck-MAX: I2C bus recovered on SDA %d, SCL %d", t_deck_max::I2C_SDA_PIN,
             t_deck_max::I2C_SCL_PIN);
    return true;
}

bool tDeckMaxHasModemReset()
{
    return false;
}

GpioPin *tDeckMaxMakeGpioPin(uint8_t pin)
{
    return io.makeGpioPin(pin);
}

#endif // _VARIANT_T_DECK_MAX
