#include "configuration.h"
#include "A7682Audio.h"
#include "TDeckProHapticOutput.h"
#include "TDeckProInput.h"
#include "TDeckUiPolicy.h"
#include "platform/DevicePowerController.h"
#include "platform/DeviceSensorProvider.h"
#include "platform/DeviceVariant.h"

#include <Wire.h>

namespace
{
class TDeckProPowerController final : public DevicePowerController
{
  public:
    void setModemPower(bool on) override
    {
        pinMode(MODEM_POWER_EN, OUTPUT);
        digitalWrite(MODEM_POWER_EN, on ? HIGH : LOW);
    }
    void setModemPwrKey(bool high) override
    {
        pinMode(MODEM_PWRKEY, OUTPUT);
        digitalWrite(MODEM_PWRKEY, high ? HIGH : LOW);
    }
    void setLoRaPower(bool on) override
    {
        pinMode(LORA_EN, OUTPUT);
        digitalWrite(LORA_EN, on ? HIGH : LOW);
    }

    void setModemReset(bool high) override
    {
        pinMode(MODEM_RST, OUTPUT);
        digitalWrite(MODEM_RST, high ? HIGH : LOW);
    }
    void setModemDtr(bool low) override
    {
        pinMode(MODEM_DTR, OUTPUT);
        digitalWrite(MODEM_DTR, low ? LOW : HIGH);
    }
    HardwareSerial *notificationAudioSerial() override { return &Serial2; }
    void configureNotificationAudioSerial(HardwareSerial &serial) override
    {
        serial.begin(115200, SERIAL_8N1, MODEM_TX, MODEM_RX);
    }

    void setMotorPower(bool on) override
    {
#ifdef PIN_DRV_EN
        pinMode(PIN_DRV_EN, OUTPUT);
        digitalWrite(PIN_DRV_EN, on ? HIGH : LOW);
#else
        (void)on;
#endif
    }
    void resetKeyboard() override {}
    void setKeyboardBacklight(bool on) override
    {
        pinMode(KB_BL_PIN, OUTPUT);
        digitalWrite(KB_BL_PIN, on ? HIGH : LOW);
    }
    void toggleKeyboardBacklight() override
    {
        pinMode(KB_BL_PIN, OUTPUT);
        digitalWrite(KB_BL_PIN, !digitalRead(KB_BL_PIN));
    }
    const char *chargerName() const override { return "BQ25896"; }
    bool usesCustomBq27220Initialization() const override { return true; }
    bool showUsbOnlyShutdownBanner() const override { return true; }
};

class TDeckProVariant final : public DeviceVariant
{
  public:
    void earlyInit() override
    {
        pinMode(LORA_EN, OUTPUT);
        digitalWrite(LORA_EN, HIGH);
        pinMode(LORA_CS, OUTPUT);
        digitalWrite(LORA_CS, HIGH);
        pinMode(SDCARD_CS, OUTPUT);
        digitalWrite(SDCARD_CS, HIGH);
        pinMode(PIN_EINK_CS, OUTPUT);
        digitalWrite(PIN_EINK_CS, HIGH);
    }
    void lateInit() override { input.begin(); }
    bool recoverI2C() override
    {
        const bool ended = Wire.end();
        const bool started = Wire.begin(I2C_SDA, I2C_SCL);
        if (!ended || !started)
            LOG_ERROR("T-Deck-Pro V1.1: I2C bus recovery failed (end=%d begin=%d)", ended, started);
        return ended && started;
    }
    bool requiresI2CRecovery() const override { return true; }
    void setMotorPower(bool enabled) override { power.setMotorPower(enabled); }
    DevicePowerController *powerController() override { return &power; }
    DeviceSensorProvider *sensorProvider() override { return &sensors; }
    DeviceInputProvider *inputProvider() override { return &input; }
    HapticOutput *hapticOutput() override { return &haptic; }
    DeviceUiPolicy *uiPolicy() override { return &ui; }
    NotificationAudio *notificationAudio() override
    {
        if (!audio)
            audio = std::make_unique<A7682Audio>();
        return audio.get();
    }
    void shutdown() override
    {
        input.shutdown();
        if (audio)
            audio->shutdown();
    }

  private:
    DeviceSensorProvider sensors;
    TDeckProPowerController power;
    TDeckProInput input;
    TDeckProHapticOutput haptic;
    TDeckUiPolicy ui;
    std::unique_ptr<A7682Audio> audio;
};
} // namespace

std::unique_ptr<DeviceVariant> createDeviceVariant()
{
    return std::make_unique<TDeckProVariant>();
}
