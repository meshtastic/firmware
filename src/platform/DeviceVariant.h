#pragma once

#include <memory>

class DevicePowerController;
class DeviceSensorProvider;
class DeviceInputProvider;
class HapticOutput;
class NotificationAudio;
class DeviceUiPolicy;

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
    virtual DevicePowerController *powerController() { return nullptr; }
    virtual DeviceSensorProvider *sensorProvider() { return nullptr; }
    virtual DeviceInputProvider *inputProvider() { return nullptr; }
    virtual HapticOutput *hapticOutput() { return nullptr; }
    virtual NotificationAudio *notificationAudio() { return nullptr; }
    virtual DeviceUiPolicy *uiPolicy() { return nullptr; }
    virtual bool supportsAntennaSelection() const { return false; }
    virtual bool isExternalAntennaSelected() const { return false; }
    virtual bool setExternalAntenna(bool external) { (void)external; return false; }
    virtual bool saveAntennaSelection() { return false; }
    virtual void shutdown() {}
};

extern std::unique_ptr<DeviceVariant> deviceVariant;

std::unique_ptr<DeviceVariant> createDeviceVariant();
void initializeDeviceVariant();
void shutdownDeviceVariant();
DevicePowerController *getDevicePowerController();
DeviceSensorProvider *getDeviceSensorProvider();
DeviceInputProvider *getDeviceInputProvider();
HapticOutput *getHapticOutput();
NotificationAudio *getNotificationAudio();
DeviceUiPolicy *getDeviceUiPolicy();
