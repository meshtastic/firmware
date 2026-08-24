#include "configuration.h"
#include "platform/DeviceVariant.h"

#include <Arduino.h>

// Keep these weak compatibility hooks until each target has moved to a variant object.
// The fallback object below is the only common code that knows about the old names.
__attribute__((noinline, weak)) void earlyInitVariant() {}
__attribute__((noinline, weak)) void initVariantAfterI2C() {}
__attribute__((noinline, weak)) void lateInitVariant() {}

namespace
{
class DefaultDeviceVariant final : public DeviceVariant
{
  public:
    void earlyInit() override { earlyInitVariant(); }
    void afterI2CInit() override { initVariantAfterI2C(); }
    void lateInit() override { lateInitVariant(); }

    void setMotorPower(bool enabled) override
    {
#ifdef PIN_DRV_EN
        pinMode(PIN_DRV_EN, OUTPUT);
        digitalWrite(PIN_DRV_EN, enabled ? HIGH : LOW);
#else
        (void)enabled;
#endif
    }
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
