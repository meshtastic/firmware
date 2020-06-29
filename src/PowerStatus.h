#pragma once
#include <Arduino.h>
#include "Status.h"
#include "configuration.h"

namespace meshtastic {

    /// Describes the state of the GPS system.
    class PowerStatus : public Status
    {

       private:
        CallbackObserver<PowerStatus, const PowerStatus *> statusObserver = CallbackObserver<PowerStatus, const PowerStatus *>(this, &PowerStatus::updateStatus);

        /// Whether we have a battery connected
        bool hasBattery;
        /// Battery voltage in mV, valid if haveBattery is true
        int batteryVoltageMv;
        /// Battery charge percentage, either read directly or estimated
        uint8_t batteryChargePercent;
        /// Whether USB is connected
        bool hasUSB;
        /// Whether we are charging the battery
        bool isCharging;

       public:

        PowerStatus() {
            statusType = STATUS_TYPE_POWER;
        }
        PowerStatus( bool hasBattery, bool hasUSB, bool isCharging, int batteryVoltageMv, uint8_t batteryChargePercent ) : Status()
        {
            this->hasBattery = hasBattery;
            this->hasUSB = hasUSB;
            this->isCharging = isCharging;
            this->batteryVoltageMv = batteryVoltageMv;
            this->batteryChargePercent = batteryChargePercent;
        }
        PowerStatus(const PowerStatus &);
        PowerStatus &operator=(const PowerStatus &);

        void observe(Observable<const PowerStatus *> *source)
        {
            statusObserver.observe(source);
        }

        bool getHasBattery() const
        { 
            return hasBattery; 
        }

        bool getHasUSB() const
        {
            return hasUSB;
        }

        bool getIsCharging() const
        { 
            return isCharging; 
        }

        int getBatteryVoltageMv() const
        { 
            return batteryVoltageMv; 
        }

        uint8_t getBatteryChargePercent() const
        { 
            return batteryChargePercent;
        }

        bool matches(const PowerStatus *newStatus) const
        {
            return (
                newStatus->getHasBattery() != hasBattery ||
                newStatus->getHasUSB() != hasUSB ||
                newStatus->getBatteryVoltageMv() != batteryVoltageMv
            );
        }
        int updateStatus(const PowerStatus *newStatus) {
            // Only update the status if values have actually changed
            bool isDirty;
            {
                isDirty = matches(newStatus);
                initialized = true; 
                hasBattery = newStatus->getHasBattery();
                batteryVoltageMv = newStatus->getBatteryVoltageMv();
                batteryChargePercent = newStatus->getBatteryChargePercent();
                hasUSB = newStatus->getHasUSB();
                isCharging = newStatus->getIsCharging();
            }
            if(isDirty) {
                DEBUG_MSG("Battery %dmV %d%%\n", batteryVoltageMv, batteryChargePercent);
                onNewStatus.notifyObservers(this);
            }
            return 0;
        }

    };

}

extern meshtastic::PowerStatus *powerStatus;
