#include "power.h"
#include "PowerFSM.h"
#include "main.h"
#include "utils.h"
#include "sleep.h"


#ifdef TBEAM_V10

// FIXME. nasty hack cleanup how we load axp192
#undef AXP192_SLAVE_ADDRESS
#include "axp20x.h"
AXP20X_Class axp;
bool pmu_irq = false;

Power *power;

bool Power::setup() 
{

    axp192Init();
    PeriodicTask::setup(); // We don't start our periodic task unless we actually found the device
    setPeriod(1);

    return axp192_found;
}

/// Reads power status to powerStatus singleton.
//
// TODO(girts): move this and other axp stuff to power.h/power.cpp.
void Power::readPowerStatus()
{
    bool hasBattery = axp.isBatteryConnect();
    int batteryVoltageMv = 0;
    uint8_t batteryChargePercent = 0;
    if (hasBattery) {
        batteryVoltageMv = axp.getBattVoltage();
        // If the AXP192 returns a valid battery percentage, use it
        if (axp.getBattPercentage() >= 0) {
            batteryChargePercent = axp.getBattPercentage();
        } else {
            // If the AXP192 returns a percentage less than 0, the feature is either not supported or there is an error
            // In that case, we compute an estimate of the charge percent based on maximum and minimum voltages defined in power.h
            batteryChargePercent = clamp((int)(((batteryVoltageMv - BAT_MILLIVOLTS_EMPTY) * 1e2) / (BAT_MILLIVOLTS_FULL - BAT_MILLIVOLTS_EMPTY)), 0, 100);
        }
    }

    // Notify any status instances that are observing us
    const meshtastic::PowerStatus powerStatus = meshtastic::PowerStatus(hasBattery, axp.isVBUSPlug(), axp.isChargeing(), batteryVoltageMv, batteryChargePercent);
    newStatus.notifyObservers(&powerStatus);

    // If we have a battery at all and it is less than 10% full, force deep sleep
    if (powerStatus.getHasBattery() && !powerStatus.getHasUSB() &&
        axp.getBattVoltage() < MIN_BAT_MILLIVOLTS)
        powerFSM.trigger(EVENT_LOW_BATTERY);
}

void Power::doTask() 
{
    readPowerStatus();
    
    // Only read once every 20 seconds once the power status for the app has been initialized
    if(statusHandler && statusHandler->isInitialized())
        setPeriod(1000 * 20);
}
#endif // TBEAM_V10

#ifdef AXP192_SLAVE_ADDRESS
/**
 * Init the power manager chip
 *
 * axp192 power
    DCDC1 0.7-3.5V @ 1200mA max -> OLED // If you turn this off you'll lose comms to the axp192 because the OLED and the axp192
 share the same i2c bus, instead use ssd1306 sleep mode DCDC2 -> unused DCDC3 0.7-3.5V @ 700mA max -> ESP32 (keep this on!) LDO1
 30mA -> charges GPS backup battery // charges the tiny J13 battery by the GPS to power the GPS ram (for a couple of days), can
 not be turned off LDO2 200mA -> LORA LDO3 200mA -> GPS
 */
void Power::axp192Init()
{
    if (axp192_found) {
        if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
            DEBUG_MSG("AXP192 Begin PASS\n");

            // axp.setChgLEDMode(LED_BLINK_4HZ);
            DEBUG_MSG("DCDC1: %s\n", axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC2: %s\n", axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO2: %s\n", axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO3: %s\n", axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC3: %s\n", axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("Exten: %s\n", axp.isExtenEnable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("----------------------------------------\n");

            axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // LORA radio
            axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // GPS main power
            axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
            axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
            axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
            axp.setDCDC1Voltage(3300); // for the OLED power

            DEBUG_MSG("DCDC1: %s\n", axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC2: %s\n", axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO2: %s\n", axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO3: %s\n", axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC3: %s\n", axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("Exten: %s\n", axp.isExtenEnable() ? "ENABLE" : "DISABLE");

            axp.setChargeControlCur(AXP1XX_CHARGE_CUR_1320MA); // actual limit (in HW) on the tbeam is 450mA
#if 0

      // Not connected
      //val = 0xfc;
      //axp._writeByte(AXP202_VHTF_CHGSET, 1, &val); // Set temperature protection

      //not used
      //val = 0x46;
      //axp._writeByte(AXP202_OFF_CTL, 1, &val); // enable bat detection
#endif
            axp.debugCharging();

#ifdef PMU_IRQ
            pinMode(PMU_IRQ, INPUT);
            attachInterrupt(
                PMU_IRQ, [] { pmu_irq = true; }, FALLING);

            axp.adc1Enable(AXP202_BATT_CUR_ADC1, 1);
            axp.enableIRQ(AXP202_BATT_REMOVED_IRQ | AXP202_BATT_CONNECT_IRQ | AXP202_CHARGING_FINISHED_IRQ | AXP202_CHARGING_IRQ |
                              AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_PEK_SHORTPRESS_IRQ,
                          1);

            axp.clearIRQ();
#endif
            readPowerStatus();
        } else {
            DEBUG_MSG("AXP192 Begin FAIL\n");
        }
    } else {
        DEBUG_MSG("AXP192 not found\n");
    }
}
#endif

void Power::loop() 
{

#ifdef PMU_IRQ
    if (pmu_irq) {
        pmu_irq = false;
        axp.readIRQ();

        DEBUG_MSG("pmu irq!\n");

        if (axp.isChargingIRQ()) {
            DEBUG_MSG("Battery start charging\n");
        }
        if (axp.isChargingDoneIRQ()) {
            DEBUG_MSG("Battery fully charged\n");
        }
        if (axp.isVbusRemoveIRQ()) {
            DEBUG_MSG("USB unplugged\n");
        }
        if (axp.isVbusPlugInIRQ()) {
            DEBUG_MSG("USB plugged In\n");
        }
        if (axp.isBattPlugInIRQ()) {
            DEBUG_MSG("Battery inserted\n");
        }
        if (axp.isBattRemoveIRQ()) {
            DEBUG_MSG("Battery removed\n");
        }
        if (axp.isPEKShortPressIRQ()) {
            DEBUG_MSG("PEK short button press\n");
        }

        readPowerStatus();
        axp.clearIRQ();
    }

#endif // T_BEAM_V10

}
