#pragma once

#include <cstdint>

class GpioPin;
class HardwareSerial;

// Board-neutral power and routing operations used by common firmware services.
// Pin numbers, expander registers, and active levels stay in the variant implementation.
class DevicePowerController
{
  public:
    virtual ~DevicePowerController() = default;

    virtual void setModemPower(bool on) { (void)on; }
    virtual void setModemPwrKey(bool high) { (void)high; }
    virtual void setModemReset(bool high) { (void)high; }
    virtual void setModemDtr(bool low) { (void)low; }
    virtual HardwareSerial *notificationAudioSerial() { return nullptr; }
    virtual void configureNotificationAudioSerial(HardwareSerial &serial) { (void)serial; }
    virtual bool usesAudioPowerKeyStartup() const { return false; }
    virtual void setAudioRoute(bool a7682e) { (void)a7682e; }
    virtual void setAmplifier(bool on) { (void)on; }
    virtual void setLoRaPower(bool on) { (void)on; }
    virtual void setGpsPower(bool on) { (void)on; }
    virtual void setImuPower(bool on) { (void)on; }
    virtual void setMotorPower(bool on) { (void)on; }
    virtual void resetTouch() {}
    virtual void resetKeyboard() {}
    virtual void setKeyboardBacklight(bool on) { (void)on; }
    virtual void toggleKeyboardBacklight() {}
    virtual void onLightSleepEnter() {}
    virtual void onLightSleepExit() {}
    virtual void onScreenTimeout() {}
    virtual void onUserInput() {}

    virtual GpioPin *makeGpioPin(uint32_t pin) { (void)pin; return nullptr; }
    virtual bool isEncodedGpioPin(uint32_t pin) const { (void)pin; return false; }

    virtual uint8_t chargerAddress(uint8_t fallback) const { return fallback; }
    virtual const char *chargerName() const { return nullptr; }
    virtual bool keyboardAddressIsBatteryGauge() const { return false; }
    virtual bool isChargerAddress(uint8_t address) const { (void)address; return false; }
    virtual bool usesCustomBq27220Initialization() const { return false; }
    virtual bool showUsbOnlyShutdownBanner() const { return false; }
};
