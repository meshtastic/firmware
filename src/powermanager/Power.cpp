#include "Power.h"
#include "PowerFSM.h"
#include "main.h"
#include "utils.h"
#include "sleep.h"
#include "configs.h"
#include "events.h"

#ifdef APX192
#include "pmu/Pmu_axp192.h"
powermanager::Pmu_axp192 pmu;
#else
#include "pmu/Pmu.h"
powermanager::Pmu pmu;
#endif

// FIXME. nasty hack cleanup how we load axp192
//#undef AXP192_SLAVE_ADDRESS

namespace powermanager {

bool pmu_irq = false;
//Power *power;

bool Power::setup() 
{
    pmu.init(pmu_irq);
    readPowerStatus();
    concurrency::PeriodicTask::setup(); // We don't start our periodic task unless we actually found the device
    setPeriod(1);

    return pmu.status();
}

/// Reads power status to powerStatus singleton.
void Power::readPowerStatus()
{
    bool hasBattery = pmu.isBatteryConnect();
    int batteryVoltageMv = 0;
    uint8_t batteryChargePercent = 0;
    if (hasBattery) {
        batteryVoltageMv = pmu.getBattVoltage();
        batteryChargePercent = pmu.getBattPercentage();
    }

    // Notify any status instances that are observing us
    const PowerStatus powerStatus = PowerStatus(hasBattery, pmu.isVBUSPlug(), pmu.isChargeing(), batteryVoltageMv, batteryChargePercent);
    newStatus.notifyObservers(&powerStatus);

    // If we have a battery at all and it is less than 10% full, force deep sleep
    if (powerStatus.getHasBattery() && !powerStatus.getHasUSB() &&
        pmu.getBattVoltage() < MIN_BAT_MILLIVOLTS)
        powerFSM.trigger(EVENT_LOW_BATTERY);
}

void Power::doTask() 
{
    readPowerStatus();
    
    // Only read once every 20 seconds once the power status for the app has been initialized
    if(statusHandler && statusHandler->isInitialized())
        setPeriod(1000 * 20);
}

void Power::loop() 
{
    if (pmu_irq) {
        pmu_irq = false;
        pmu.setIRQ(pmu_irq);
        pmu.IRQloop();

        readPowerStatus();
    }

}

} // namespace powermanager