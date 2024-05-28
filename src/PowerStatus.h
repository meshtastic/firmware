#pragma once
#include "Status.h"
#include "configuration.h"
#include <Arduino.h>

namespace meshtastic
{

/**
 * A boolean where we have a third state of Unknown
 */
enum OptionalBool { OptFalse = 0, OptTrue = 1, OptUnknown = 2 };

/// Describes the state of the Power system.
class PowerStatus : public Status
{

  private:
    CallbackObserver<PowerStatus, const PowerStatus *> statusObserver =
        CallbackObserver<PowerStatus, const PowerStatus *>(this, &PowerStatus::updateStatus);

    /// Whether we have a battery connected
    OptionalBool hasBattery = OptUnknown;
    /// Battery voltage in mV, valid if haveBattery is true
    int batteryVoltageMv = 0;
    /// Battery charge percentage, either read directly or estimated
    int8_t batteryChargePercent = 0;
    /// Whether USB is connected
    OptionalBool hasUSB = OptUnknown;
    /// Whether we are charging the battery
    OptionalBool isCharging = OptUnknown;

  public:
    PowerStatus() { statusType = STATUS_TYPE_POWER; }
    PowerStatus(OptionalBool hasBattery, OptionalBool hasUSB, OptionalBool isCharging, int batteryVoltageMv = -1,
                int8_t batteryChargePercent = 0)
        : Status()
    {
        this->hasBattery = hasBattery;
        this->hasUSB = hasUSB;
        this->isCharging = isCharging;
        this->batteryVoltageMv = batteryVoltageMv;
        this->batteryChargePercent = batteryChargePercent;
    }
    PowerStatus(const PowerStatus &);
    PowerStatus &operator=(const PowerStatus &);

    void observe(Observable<const PowerStatus *> *source) { statusObserver.observe(source); }

    bool getHasBattery() const { return hasBattery == OptTrue; }

    bool getHasUSB() const { return hasUSB == OptTrue; }

    /// Can we even know if this board has USB power or not
    bool knowsUSB() const { return hasUSB != OptUnknown; }

    bool getIsCharging() const { return isCharging == OptTrue; }

    int getBatteryVoltageMv() const { return batteryVoltageMv; }

    /**
     * Note: for boards with battery pin or PMU, 0% battery means 'unknown/this board doesn't have a battery installed'
     */
#if defined(HAS_PMU) || defined(BATTERY_PIN)
    uint8_t getBatteryChargePercent() const { return getHasBattery() ? batteryChargePercent : 0; }
#endif

    /**
     * Note: for boards without battery pin and PMU, 101% battery means 'the board is using external power'
     */
#if !defined(HAS_PMU) && !defined(BATTERY_PIN)
    uint8_t getBatteryChargePercent() const { return getHasBattery() ? batteryChargePercent : 101; }
#endif

    bool matches(const PowerStatus *newStatus) const
    {
        return (newStatus->getHasBattery() != hasBattery || newStatus->getHasUSB() != hasUSB ||
                newStatus->getBatteryVoltageMv() != batteryVoltageMv);
    }
    int updateStatus(const PowerStatus *newStatus)
    {
        // Only update the status if values have actually changed
        bool isDirty;
        {
            isDirty = matches(newStatus);
            initialized = true;
            hasBattery = newStatus->hasBattery;
            batteryVoltageMv = newStatus->getBatteryVoltageMv();
            batteryChargePercent = newStatus->getBatteryChargePercent();
            hasUSB = newStatus->hasUSB;
            isCharging = newStatus->isCharging;
        }
        if (isDirty) {
            // LOG_DEBUG("Battery %dmV %d%%\n", batteryVoltageMv, batteryChargePercent);
            onNewStatus.notifyObservers(this);
        }
        return 0;
    }
};

} // namespace meshtastic

extern meshtastic::PowerStatus *powerStatus;
