#include "Pmu_axp192.h"
#include "utils.h"
#include "configuration.h"
#include "../configs.h"
#include <Arduino.h>

namespace powermanager {

bool Pmu_axp192::isBatteryConnect() {
    return _axp.isBatteryConnect();
}

bool Pmu_axp192::status() {
    /** @todo TBD */
    return true;
}

float Pmu_axp192::getBattVoltage() {
    return _axp.getBattVoltage();
}

bool Pmu_axp192::isChargeing() {
    return _axp.isChargeing();
}

bool Pmu_axp192::isVBUSPlug() {
    return _axp.isVBUSPlug();
}

bool Pmu_axp192::getHasBattery() {
    return _axp.isBatteryConnect();
}

bool Pmu_axp192::getHasUSB() {
    return true; // TBD _axp.getHasUSB();
}

/**
 * @brief Init the power manager chip
 *
 * @details _axp192 power
    DCDC1 0.7-3.5V @ 1200mA max -> OLED // If you turn this off you'll lose comms to the _axp192 because the OLED and the _axp192
 share the same i2c bus, instead use ssd1306 sleep mode DCDC2 -> unused DCDC3 0.7-3.5V @ 700mA max -> ESP32 (keep this on!) LDO1
 30mA -> charges GPS backup battery // charges the tiny J13 battery by the GPS to power the GPS ram (for a couple of days), can
 not be turned off LDO2 200mA -> LORA LDO3 200mA -> GPS
 */
void Pmu_axp192::init(bool irq)
{
    if (!_axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
        _irq = irq;

        DEBUG_MSG("_axp192 Begin PASS\n");

        // _axp.setChgLEDMode(LED_BLINK_4HZ);
        DEBUG_MSG("DCDC1: %s\n", _axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("DCDC2: %s\n", _axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("LDO2: %s\n", _axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("LDO3: %s\n", _axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("DCDC3: %s\n", _axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("Exten: %s\n", _axp.isExtenEnable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("----------------------------------------\n");

        _axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // LORA radio
        _axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // GPS main power
        _axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
        _axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
        _axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
        _axp.setDCDC1Voltage(3300); // for the OLED power

        DEBUG_MSG("DCDC1: %s\n", _axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("DCDC2: %s\n", _axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("LDO2: %s\n", _axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("LDO3: %s\n", _axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("DCDC3: %s\n", _axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
        DEBUG_MSG("Exten: %s\n", _axp.isExtenEnable() ? "ENABLE" : "DISABLE");

        _axp.setChargeControlCur(AXP1XX_CHARGE_CUR_1320MA); // actual limit (in HW) on the tbeam is 450mA

        // Not connected
        //val = 0xfc;
        //_axp._writeByte(AXP202_VHTF_CHGSET, 1, &val); // Set temperature protection

        //not used
        //val = 0x46;
        //_axp._writeByte(AXP202_OFF_CTL, 1, &val); // enable bat detection

        _axp.debugCharging();

        if(_irq) {
            pinMode(PMU_IRQ, INPUT);
            /*
            attachInterrupt(
                PMU_IRQ,  this->setIRQ (true), FALLING);
            */

            _axp.adc1Enable(AXP202_BATT_CUR_ADC1, 1);
            _axp.enableIRQ(AXP202_BATT_REMOVED_IRQ | AXP202_BATT_CONNECT_IRQ | AXP202_CHARGING_FINISHED_IRQ | AXP202_CHARGING_IRQ |
                                AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_PEK_SHORTPRESS_IRQ,
                            1);

            _axp.clearIRQ();
        }
    } else {
        DEBUG_MSG("APX192 Begin FAIL\n");
    }
}

uint8_t Pmu_axp192::getBattPercentage() {
    if (_axp.getBattPercentage() >= 0) {
        return _axp.getBattPercentage();
    } else {
        // If the _axp192 returns a percentage less than 0, the feature is either not supported or there is an error
        // In that case, we compute an estimate of the charge percent based on maximum and minimum voltages defined in power.h
        float batteryVoltageMv = _axp.getBattVoltage();

        return clamp((int)(((batteryVoltageMv - BAT_MILLIVOLTS_EMPTY) * 1e2) / (BAT_MILLIVOLTS_FULL - BAT_MILLIVOLTS_EMPTY)), 0, 100);
    }
}

void Pmu_axp192::IRQloop() {
    if(!_irq)
        return;

    _axp.readIRQ();

    DEBUG_MSG("pmu irq!\n");

    if (_axp.isChargingIRQ())
        DEBUG_MSG("Battery start charging\n");

    if (_axp.isChargingDoneIRQ())
        DEBUG_MSG("Battery fully charged\n");

    if (_axp.isVbusRemoveIRQ())
        DEBUG_MSG("USB unplugged\n");

    if (_axp.isVbusPlugInIRQ())
        DEBUG_MSG("USB plugged In\n");

    if (_axp.isBattPlugInIRQ())
        DEBUG_MSG("Battery inserted\n");

    if (_axp.isBattRemoveIRQ())
        DEBUG_MSG("Battery removed\n");

    if (_axp.isPEKShortPressIRQ())
        DEBUG_MSG("PEK short button press\n");

    _axp.clearIRQ();
}

void Pmu_axp192::setIRQ(bool pmu_irq) {
    _irq = pmu_irq;
}

} // namespace powermanager
