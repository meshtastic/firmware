#include "configuration.h"
#include "TDeckMaxBoard.h"
#include "platform/DeviceVariant.h"

extern void tDeckMaxLateInit();
extern void tDeckMaxShutdown();

namespace
{
class TDeckMaxVariant final : public DeviceVariant
{
  public:
    void afterI2CInit() override { tDeckMaxInit(); }
    void lateInit() override { tDeckMaxLateInit(); }
    bool recoverI2C() override { return tDeckMaxRecoverI2C(); }
    bool requiresI2CRecovery() const override { return true; }
    void onAccelerometerScan(bool found) override
    {
        if (!found)
            tDeckMaxSetImuPower(false);
    }
    void setMotorPower(bool enabled) override { tDeckMaxSetMotorPower(enabled); }
    void shutdown() override { tDeckMaxShutdown(); }
};
} // namespace

std::unique_ptr<DeviceVariant> createDeviceVariant()
{
    return std::make_unique<TDeckMaxVariant>();
}
