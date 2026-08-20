#pragma once

#include <stdint.h>

class GpioPin;

namespace t_deck_max
{
constexpr uint32_t XL9555_GPIO_BASE = 0x100;
constexpr uint8_t XL9555_PIN_COUNT = 16;

constexpr uint32_t encodeXl9555Pin(uint8_t pin)
{
    return XL9555_GPIO_BASE + pin;
}

constexpr bool isEncodedXl9555Pin(uint32_t pin)
{
    return pin >= XL9555_GPIO_BASE && pin < XL9555_GPIO_BASE + XL9555_PIN_COUNT;
}

constexpr uint8_t decodeXl9555Pin(uint32_t pin)
{
    return static_cast<uint8_t>(pin - XL9555_GPIO_BASE);
}

enum class Xl9555Pin : uint8_t {
    ModemPower = 0,
    LoraPower = 1,
    GpsPower = 2,
    ImuPower = 3,
    AntennaSelect = 4,
    MotorPower = 5,
    Amplifier = 6,
    TouchReset = 7,
    // XL9555 library pin numbers are linear bit indexes: P10/P11/P12 = 8/9/10.
    ModemPwrKey = 8,
    KeyboardReset = 9,
    AudioRoute = 10,
};

enum class Antenna : uint8_t { Internal = 0, External = 1 };

constexpr Antenna DEFAULT_ANTENNA = Antenna::Internal;
constexpr uint32_t ANTENNA_PREFERENCE_MAGIC = 0x54444D41u; // "TDMA"
constexpr uint8_t ANTENNA_PREFERENCE_VERSION = 1;

constexpr bool isValidAntenna(Antenna antenna)
{
    return antenna == Antenna::Internal || antenna == Antenna::External;
}

// XL9555 P04 is active high for the built-in antenna path.
constexpr bool antennaSelectPinLevel(Antenna antenna)
{
    return antenna == Antenna::Internal;
}

struct AntennaPreferenceFile {
    uint32_t magic;
    uint8_t version;
    uint8_t antenna;
    uint8_t reserved[2];
    uint32_t crc;
} __attribute__((packed));

static_assert(sizeof(AntennaPreferenceFile) == 12, "Unexpected MAX antenna preference layout");

constexpr bool isValidAntennaPreference(const AntennaPreferenceFile &preference)
{
    return preference.magic == ANTENNA_PREFERENCE_MAGIC && preference.version == ANTENNA_PREFERENCE_VERSION &&
           isValidAntenna(static_cast<Antenna>(preference.antenna));
}

constexpr uint8_t I2C_ADDRESS = 0x20;
constexpr uint8_t CST328_ADDRESS = 0x1A;
constexpr uint8_t TCA8418_ADDRESS = 0x34;
constexpr uint8_t ES8311_ADDRESS = 0x18;
constexpr uint8_t BQ27220_ADDRESS = 0x55;
constexpr uint8_t SY6970_ADDRESS = 0x6A;
constexpr uint8_t CHARGER_ADDRESS = SY6970_ADDRESS;
constexpr uint8_t BHI260AP_ADDRESS = 0x28;
constexpr uint8_t LTR553ALS_ADDRESS = 0x23;
constexpr uint8_t DRV2605_ADDRESS = 0x5A;
constexpr uint8_t BHI260AP_IRQ_PIN = 21;
constexpr int8_t BHI260AP_RESET_PIN = -1;
constexpr bool BHI260AP_USES_RAM_FIRMWARE = true;
constexpr bool BHI260AP_USES_IRQ = true;
constexpr uint8_t BHI260AP_SAMPLE_RATE_HZ = 100;
constexpr uint32_t BHI260AP_REPORT_LATENCY_MS = 0;
constexpr float BHI260AP_MOTION_THRESHOLD_G = 0.20f;
constexpr bool MOTOR_POWER_ON_AT_BOOT = false;
constexpr uint16_t HAPTIC_POWER_HOLD_MS = 250;

constexpr uint8_t I2C_SDA_PIN = 13;
constexpr uint8_t I2C_SCL_PIN = 14;
constexpr uint8_t SPI_SCK_PIN = 36;
constexpr uint8_t SPI_MOSI_PIN = 33;
constexpr uint8_t SPI_MISO_PIN = 47;
constexpr uint8_t EINK_CS_PIN = 34;
constexpr uint8_t EINK_DC_PIN = 35;
constexpr uint8_t EINK_BUSY_PIN = 37;
constexpr uint8_t EINK_RESET_PIN = 9;
constexpr uint8_t EINK_BACKLIGHT_PIN = 41;
constexpr uint8_t KEYBOARD_BACKLIGHT_PIN = 42;
constexpr uint8_t TOUCH_INT_PIN = 12;
constexpr uint8_t KEYBOARD_INT_PIN = 15;
constexpr uint8_t GPS_RX_GPIO = 2;
constexpr uint8_t GPS_TX_GPIO = 16;
constexpr uint8_t GPS_PPS_PIN = 1;
constexpr uint8_t MODEM_RI_PIN = 7;
constexpr uint8_t MODEM_DTR_PIN = 8;
constexpr uint8_t MODEM_RX_PIN = 10;
constexpr uint8_t MODEM_TX_PIN = 11;
constexpr uint8_t CODEC_MCLK_PIN = 38;
constexpr uint8_t CODEC_BCLK_PIN = 39;
constexpr uint8_t CODEC_LRCK_PIN = 18;
constexpr uint8_t CODEC_DOUT_PIN = 17;
constexpr uint8_t CODEC_DIN_PIN = 40;
constexpr uint8_t SDCARD_CS_PIN = 48;
constexpr uint8_t LORA_CS_PIN = 3;
constexpr uint8_t LORA_RESET_PIN = 4;
constexpr uint8_t LORA_DIO1_PIN = 5;
constexpr uint8_t LORA_BUSY_PIN = 6;

enum class AudioRoute : uint8_t { Es8311, A7682e };

class AudioPolicy
{
  public:
    void reset()
    {
        route = AudioRoute::Es8311;
        amplifierEnabled = false;
    }

    void startEs8311Playback()
    {
        route = AudioRoute::Es8311;
        amplifierEnabled = true;
    }

    void startA7682ePlayback()
    {
        route = AudioRoute::A7682e;
        amplifierEnabled = true;
    }

    void stopPlayback() { reset(); }

    AudioRoute getRoute() const { return route; }
    bool isAmplifierEnabled() const { return amplifierEnabled; }

  private:
    AudioRoute route = AudioRoute::Es8311;
    bool amplifierEnabled = false;
};
} // namespace t_deck_max

void initVariantAfterI2C();
void tDeckMaxInit();
bool tDeckMaxRecoverI2C();
void tDeckMaxSetModemPower(bool on);
void tDeckMaxSetModemPwrKey(bool high);
void tDeckMaxSetAudioRoute(bool a7682e);
void tDeckMaxSetAmplifier(bool on);
void tDeckMaxSetLoRaPower(bool on);
void tDeckMaxSetGpsPower(bool on);
void tDeckMaxSetImuPower(bool on);
void tDeckMaxSetMotorPower(bool on);
void tDeckMaxSetAntennaInternal(bool internal);
t_deck_max::Antenna tDeckMaxGetAntenna();
bool tDeckMaxSetAntenna(t_deck_max::Antenna antenna);
bool tDeckMaxLoadAntenna();
bool tDeckMaxSaveAntenna();
void tDeckMaxResetTouch();
void tDeckMaxResetKeyboard();
void tDeckMaxSetSafeState();
bool tDeckMaxHasModemReset();
GpioPin *tDeckMaxMakeGpioPin(uint8_t pin);
