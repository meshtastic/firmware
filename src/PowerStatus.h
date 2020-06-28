#pragma once
#include "lock.h"
#include "configuration.h"

namespace meshtastic
{

/// Describes the state of the power system.
struct PowerStatus 
{

    /// Whether or not values have changed since last read
    bool isDirty = false;
    /// Whether we have a battery connected
    bool hasBattery;
    /// Battery voltage in mV, valid if haveBattery is true
    int batteryVoltageMv;
    /// Battery charge percentage, either read directly or estimated
    int batteryChargePercent;
    /// Whether USB is connected
    bool hasUSB;
    /// Whether we are charging the battery
    bool isCharging;

};

class PowerStatusHandler 
{

   private:
    PowerStatus status;
    CallbackObserver<PowerStatusHandler, const PowerStatus> powerObserver = CallbackObserver<PowerStatusHandler, const PowerStatus>(this, &PowerStatusHandler::updateStatus);
    bool initialized = false;
    /// Protects all of internal state.
    Lock lock;

   public:

    Observable<void *> onNewStatus;

    void observe(Observable<const PowerStatus> *source)
    {
        powerObserver.observe(source);
    }

    bool isInitialized() { LockGuard guard(&lock); return initialized; }
    bool isCharging() { LockGuard guard(&lock); return status.isCharging; }
    bool hasUSB() { LockGuard guard(&lock); return status.hasUSB; }
    bool hasBattery() { LockGuard guard(&lock); return status.hasBattery; }
    int getBatteryVoltageMv() { LockGuard guard(&lock); return status.batteryVoltageMv; }
    int getBatteryChargePercent() { LockGuard guard(&lock); return status.batteryChargePercent; }

    int updateStatus(const PowerStatus newStatus) {
        // Only update the status if values have actually changed
        status.isDirty = (
            newStatus.hasBattery != status.hasBattery ||
            newStatus.hasUSB != status.hasUSB ||
            newStatus.batteryVoltageMv != status.batteryVoltageMv
        );
        LockGuard guard(&lock);
        initialized = true; 
        status.hasBattery = newStatus.hasBattery;
        status.batteryVoltageMv = newStatus.batteryVoltageMv;
        status.batteryChargePercent = newStatus.batteryChargePercent;
        status.hasUSB = newStatus.hasUSB;
        status.isCharging = newStatus.isCharging;
        if(status.isDirty) {
            DEBUG_MSG("Battery %dmV %d%%\n", status.batteryVoltageMv, status.batteryChargePercent);
            onNewStatus.notifyObservers(this);
        }
        return 0;
    }
};

} // namespace meshtastic

extern meshtastic::PowerStatus powerStatus;
