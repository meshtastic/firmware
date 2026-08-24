#pragma once

#include <memory>

// Board-owned lifecycle and hardware extension points. The default implementation
// delegates to the legacy hooks so unconverted variants retain their existing boot path.
class DeviceVariant
{
  public:
    virtual ~DeviceVariant() = default;

    virtual void earlyInit() {}
    virtual void afterI2CInit() {}
    virtual void lateInit() {}
    virtual bool recoverI2C() { return false; }
    virtual bool requiresI2CRecovery() const { return false; }
    virtual void onAccelerometerScan(bool found) { (void)found; }
    virtual void setMotorPower(bool enabled) { (void)enabled; }
    virtual void shutdown() {}
};

extern std::unique_ptr<DeviceVariant> deviceVariant;

std::unique_ptr<DeviceVariant> createDeviceVariant();
void initializeDeviceVariant();
void shutdownDeviceVariant();
