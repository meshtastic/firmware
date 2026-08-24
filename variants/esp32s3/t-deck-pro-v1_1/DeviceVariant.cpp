#include "configuration.h"
#include "platform/DeviceVariant.h"

extern void tDeckProEarlyInit();
extern void tDeckProLateInit();
extern bool tDeckProV1_1RecoverI2C();
extern void tDeckProShutdown();

namespace
{
class TDeckProVariant final : public DeviceVariant
{
  public:
    void earlyInit() override { tDeckProEarlyInit(); }
    void lateInit() override { tDeckProLateInit(); }
    bool recoverI2C() override { return tDeckProV1_1RecoverI2C(); }
    bool requiresI2CRecovery() const override { return true; }
    void setMotorPower(bool enabled) override
    {
#ifdef PIN_DRV_EN
        pinMode(PIN_DRV_EN, OUTPUT);
        digitalWrite(PIN_DRV_EN, enabled ? HIGH : LOW);
#else
        (void)enabled;
#endif
    }
    void shutdown() override { tDeckProShutdown(); }
};
} // namespace

std::unique_ptr<DeviceVariant> createDeviceVariant()
{
    return std::make_unique<TDeckProVariant>();
}
