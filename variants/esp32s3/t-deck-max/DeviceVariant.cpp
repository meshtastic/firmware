#include "configuration.h"
#include "A7682Audio.h"
#include "TDeckMaxBoard.h"
#include "TDeckMaxBHI260APSensor.h"
#include "TDeckMaxHapticOutput.h"
#include "TDeckMaxInput.h"
#include "TDeckUiPolicy.h"
#include "platform/DevicePowerController.h"
#include "platform/DeviceSensorProvider.h"
#include "platform/DeviceVariant.h"

#if defined(HAS_I2S)
#include "AudioBoard.h"
#endif

namespace
{
#if defined(HAS_I2S)
DriverPins PinsAudioBoardES8311;
AudioBoard audioCodecBoard(AudioDriverES8311, PinsAudioBoardES8311);
#endif

class TDeckMaxSensorProvider final : public DeviceSensorProvider
{
  public:
    std::unique_ptr<MotionSensor> createAccelerometer(const ScanI2C::FoundDevice &device) override
    {
        if (device.type == ScanI2C::DeviceType::BHI260AP)
            return std::make_unique<TDeckMaxBHI260APSensor>(device);
        return nullptr;
    }

};

class TDeckMaxPowerController final : public DevicePowerController
{
  public:
    void setModemPower(bool on) override { tDeckMaxSetModemPower(on); }
    void setModemPwrKey(bool high) override { tDeckMaxSetModemPwrKey(high); }
    void setModemReset(bool high) override { tDeckMaxSetModemReset(high); }
    void setModemDtr(bool low) override { tDeckMaxSetModemDtr(low); }
    HardwareSerial *notificationAudioSerial() override { return &Serial1; }
    void configureNotificationAudioSerial(HardwareSerial &serial) override
    {
        serial.begin(115200, SERIAL_8N1, t_deck_max::MODEM_TX_PIN, t_deck_max::MODEM_RX_PIN);
    }
    bool usesAudioPowerKeyStartup() const override { return true; }
    void setAudioRoute(bool a7682e) override { tDeckMaxSetAudioRoute(a7682e); }
    void setAmplifier(bool on) override { tDeckMaxSetAmplifier(on); }
    void setLoRaPower(bool on) override { tDeckMaxSetLoRaPower(on); }
    void setGpsPower(bool on) override { tDeckMaxSetGpsPower(on); }
    void setImuPower(bool on) override { tDeckMaxSetImuPower(on); }
    void setMotorPower(bool on) override { tDeckMaxSetMotorPower(on); }
    void resetTouch() override { tDeckMaxResetTouch(); }
    void resetKeyboard() override { tDeckMaxResetKeyboard(); }
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
    ::GpioPin *makeGpioPin(uint32_t pin) override
    {
        return t_deck_max::isEncodedXl9555Pin(pin) ? tDeckMaxMakeGpioPin(t_deck_max::decodeXl9555Pin(pin)) : nullptr;
    }
    bool isEncodedGpioPin(uint32_t pin) const override { return t_deck_max::isEncodedXl9555Pin(pin); }
    uint8_t chargerAddress(uint8_t fallback) const override
    {
        (void)fallback;
        return t_deck_max::CHARGER_ADDRESS;
    }
    const char *chargerName() const override { return "SY6970"; }
    bool keyboardAddressIsBatteryGauge() const override { return true; }
    bool isChargerAddress(uint8_t address) const override { return address == t_deck_max::CHARGER_ADDRESS; }
    bool usesCustomBq27220Initialization() const override { return true; }
};

class TDeckMaxVariant final : public DeviceVariant
{
  public:
    void afterI2CInit() override { tDeckMaxInit(); }
    void lateInit() override
    {
        input.begin();

#if defined(HAS_I2S)
        PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire);
        PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT,
                                    DAC_I2S_DIN);

        CodecConfig cfg;
        cfg.input_device = ADC_INPUT_LINE1;
        cfg.output_device = DAC_OUTPUT_ALL;
        cfg.i2s.bits = BIT_LENGTH_16BITS;
        cfg.i2s.rate = RATE_44K;
        audioCodecBoard.begin(cfg);
        audioCodecBoard.setVolume(75);
        tDeckMaxSetAudioRoute(false);
        tDeckMaxSetAmplifier(false);
#endif
    }
    bool recoverI2C() override { return tDeckMaxRecoverI2C(); }
    bool requiresI2CRecovery() const override { return true; }
    void onAccelerometerScan(bool found) override
    {
        if (!found)
            power.setImuPower(false);
    }
    void setMotorPower(bool enabled) override { power.setMotorPower(enabled); }
    DevicePowerController *powerController() override { return &power; }
    DeviceSensorProvider *sensorProvider() override { return &sensors; }
    DeviceInputProvider *inputProvider() override { return &input; }
    HapticOutput *hapticOutput() override { return &haptic; }
    DeviceUiPolicy *uiPolicy() override { return &ui; }
    bool supportsAntennaSelection() const override { return true; }
    bool isExternalAntennaSelected() const override
    {
        return tDeckMaxGetAntenna() == t_deck_max::Antenna::External;
    }
    bool setExternalAntenna(bool external) override
    {
        return tDeckMaxSetAntenna(external ? t_deck_max::Antenna::External : t_deck_max::Antenna::Internal);
    }
    bool saveAntennaSelection() override { return tDeckMaxSaveAntenna(); }
    NotificationAudio *notificationAudio() override
    {
        if (!audio)
            audio = std::make_unique<A7682Audio>();
        return audio.get();
    }
    void shutdown() override
    {
        if (audio)
            audio->shutdown();

        tDeckMaxSetAmplifier(false);
        tDeckMaxSetAudioRoute(false);
        digitalWrite(PIN_EINK_EN, LOW);
        digitalWrite(KB_BL_PIN, LOW);
        digitalWrite(LORA_CS, HIGH);
        digitalWrite(SDCARD_CS, HIGH);
        digitalWrite(PIN_EINK_CS, HIGH);
        tDeckMaxSetSafeState();
    }

  private:
    TDeckMaxSensorProvider sensors;
    TDeckMaxPowerController power;
    TDeckMaxInput input;
    TDeckMaxHapticOutput haptic;
    TDeckUiPolicy ui;
    std::unique_ptr<A7682Audio> audio;
};
} // namespace

std::unique_ptr<DeviceVariant> createDeviceVariant()
{
    return std::make_unique<TDeckMaxVariant>();
}
