#include "configuration.h"
#include "platform/DevicePowerController.h"
#include "platform/DeviceSensorProvider.h"
#include "platform/DeviceVariant.h"
#include "graphics/DeviceUiPolicy.h"

#include <Arduino.h>

// Keep these weak compatibility hooks until each target has moved to a variant object.
// The fallback object below is the only common code that knows about the old names.
__attribute__((noinline, weak)) void earlyInitVariant() {}
__attribute__((noinline, weak)) void initVariantAfterI2C() {}
__attribute__((noinline, weak)) void lateInitVariant() {}

namespace
{
void setDefaultMotorPower(bool enabled)
{
#ifdef PIN_DRV_EN
    pinMode(PIN_DRV_EN, OUTPUT);
    digitalWrite(PIN_DRV_EN, enabled ? HIGH : LOW);
#else
    (void)enabled;
#endif
}

class DefaultDeviceVariant final : public DeviceVariant
{
  public:
    void earlyInit() override { earlyInitVariant(); }
    void afterI2CInit() override { initVariantAfterI2C(); }
    void lateInit() override { lateInitVariant(); }
    DeviceSensorProvider *sensorProvider() override { return &sensors; }

    void setMotorPower(bool enabled) override { setDefaultMotorPower(enabled); }

  private:
    DeviceSensorProvider sensors;
};
} // namespace

std::unique_ptr<DeviceVariant> createDeviceVariant() __attribute__((weak, noinline));
std::unique_ptr<DeviceVariant> createDeviceVariant()
{
    return std::make_unique<DefaultDeviceVariant>();
}

std::unique_ptr<DeviceVariant> deviceVariant;

void initializeDeviceVariant()
{
    if (!deviceVariant)
        deviceVariant = createDeviceVariant();
}

void shutdownDeviceVariant()
{
    if (deviceVariant)
        deviceVariant->shutdown();
}

DevicePowerController *getDevicePowerController()
{
    return deviceVariant ? deviceVariant->powerController() : nullptr;
}

DeviceSensorProvider *getDeviceSensorProvider()
{
    return deviceVariant ? deviceVariant->sensorProvider() : nullptr;
}

DeviceInputProvider *getDeviceInputProvider()
{
    return deviceVariant ? deviceVariant->inputProvider() : nullptr;
}

HapticOutput *getHapticOutput()
{
    return deviceVariant ? deviceVariant->hapticOutput() : nullptr;
}

NotificationAudio *getNotificationAudio()
{
    return deviceVariant ? deviceVariant->notificationAudio() : nullptr;
}

DeviceUiPolicy *getDeviceUiPolicy()
{
    return deviceVariant && deviceVariant->uiPolicy() ? deviceVariant->uiPolicy()
                                                       : const_cast<DeviceUiPolicy *>(&getDefaultDeviceUiPolicy());
}
