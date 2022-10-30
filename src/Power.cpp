#include "power.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "sleep.h"
#include "utils.h"
#include "buzz/buzz.h"

#ifdef HAS_PMU
#include "XPowersLibInterface.hpp"
#include "XPowersAXP2101.tpp"
#include "XPowersAXP192.tpp"
XPowersLibInterface *PMU = NULL;
#else
// Copy of the base class defined in axp20x.h.
// I'd rather not inlude axp20x.h as it brings Wire dependency.
class HasBatteryLevel
{
  public:
    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     */
    virtual int getBatteryPercent() { return -1; }

    /**
     * The raw voltage of the battery or NAN if unknown
     */
    virtual uint16_t getBattVoltage() { return 0; }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() { return false; }

    virtual bool isVbusIn() { return false; }
    virtual bool isCharging() { return false; }
};
#endif

bool pmu_irq = false;

Power *power;

using namespace meshtastic;

#ifndef AREF_VOLTAGE
#if defined(ARCH_NRF52)
/*
 * Internal Reference is +/-0.6V, with an adjustable gain of 1/6, 1/5, 1/4,
 * 1/3, 1/2 or 1, meaning 3.6, 3.0, 2.4, 1.8, 1.2 or 0.6V for the ADC levels.
 *
 * External Reference is VDD/4, with an adjustable gain of 1, 2 or 4, meaning
 * VDD/4, VDD/2 or VDD for the ADC levels.
 *
 * Default settings are internal reference with 1/6 gain (GND..3.6V ADC range)
 */
#define AREF_VOLTAGE 3.6
#else
#define AREF_VOLTAGE 3.3
#endif
#endif

/**
 * If this board has a battery level sensor, set this to a valid implementation
 */
static HasBatteryLevel *batteryLevel; // Default to NULL for no battery level sensor

/**
 * A simple battery level sensor that assumes the battery voltage is attached via a voltage-divider to an analog input
 */
class AnalogBatteryLevel : public HasBatteryLevel
{
    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     *
     * FIXME - use a lipo lookup table, the current % full is super wrong
     */
    virtual int getBatteryPercent() override
    {
        float v = getBattVoltage();

        if (v < noBatVolt)
            return -1; // If voltage is super low assume no battery installed

#ifdef ARCH_ESP32
        // This does not work on a RAK4631 with battery connected
        if (v > chargingVolt)
            return 0; // While charging we can't report % full on the battery
#endif

        return clamp((int)(100 * (v - emptyVolt) / (fullVolt - emptyVolt)), 0, 100);
    }

    /**
     * The raw voltage of the batteryin millivolts or NAN if unknown
     */
    virtual uint16_t getBattVoltage() override
    {

#ifndef ADC_MULTIPLIER
#define ADC_MULTIPLIER 2.0
#endif

#ifdef BATTERY_PIN
        // Override variant or default ADC_MULTIPLIER if we have the override pref
        float operativeAdcMultiplier = config.power.adc_multiplier_override > 0
                                           ? config.power.adc_multiplier_override
                                           : ADC_MULTIPLIER;
        // Do not call analogRead() often.
        const uint32_t min_read_interval = 5000;
        if (millis() - last_read_time_ms > min_read_interval) {
            last_read_time_ms = millis();

#ifdef BATTERY_SENSE_SAMPLES
//Set the number of samples, it has an effect of increasing sensitivity, especially in complex electromagnetic environment.
            uint32_t raw = 0;
            for(uint32_t i=0; i<BATTERY_SENSE_SAMPLES;i++){
                raw += analogRead(BATTERY_PIN);
            }
            raw = raw/BATTERY_SENSE_SAMPLES;
#else
            uint32_t raw = analogRead(BATTERY_PIN);
#endif

            float scaled;
#ifndef VBAT_RAW_TO_SCALED
            scaled = 1000.0 * operativeAdcMultiplier * (AREF_VOLTAGE / 1024.0) * raw;
#else
            scaled = VBAT_RAW_TO_SCALED(raw); // defined in variant.h
#endif
            // DEBUG_MSG("battery gpio %d raw val=%u scaled=%u\n", BATTERY_PIN, raw, (uint32_t)(scaled));
            last_read_value = scaled;
            return scaled;
        } else {
            return last_read_value;
        }
#else
        return 0;
#endif
    }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() override { return getBatteryPercent() != -1; }

    /// If we see a battery voltage higher than physics allows - assume charger is pumping
    /// in power
    virtual bool isVbusIn() override { return getBattVoltage() > chargingVolt; }

    /// Assume charging if we have a battery and external power is connected.
    /// we can't be smart enough to say 'full'?
    virtual bool isCharging() override { return isBatteryConnect() && isVbusIn(); }

  private:
    /// If we see a battery voltage higher than physics allows - assume charger is pumping
    /// in power

#ifndef BAT_FULLVOLT
#define BAT_FULLVOLT 4200
#endif 
#ifndef BAT_EMPTYVOLT
#define BAT_EMPTYVOLT 3270
#endif 
#ifndef BAT_CHARGINGVOLT
#define BAT_CHARGINGVOLT 4210
#endif 
#ifndef BAT_NOBATVOLT
#define BAT_NOBATVOLT 2230
#endif 

    /// For heltecs with no battery connected, the measured voltage is 2204, so raising to 2230 from 2100
    const float fullVolt = BAT_FULLVOLT, emptyVolt = BAT_EMPTYVOLT, chargingVolt = BAT_CHARGINGVOLT, noBatVolt = BAT_NOBATVOLT;
    float last_read_value = 0.0;
    uint32_t last_read_time_ms = 0;
};

AnalogBatteryLevel analogLevel;

Power::Power() : OSThread("Power")
{
    statusHandler = {};
    low_voltage_counter = 0;
}

bool Power::analogInit()
{
#ifdef BATTERY_PIN
    DEBUG_MSG("Using analog input %d for battery level\n", BATTERY_PIN);

    // disable any internal pullups
    pinMode(BATTERY_PIN, INPUT);

#ifdef ARCH_ESP32
    // ESP32 needs special analog stuff
    adcAttachPin(BATTERY_PIN);
#endif
#ifdef ARCH_NRF52
#ifdef VBAT_AR_INTERNAL
    analogReference(VBAT_AR_INTERNAL);
#else
    analogReference(AR_INTERNAL); // 3.6V
#endif
#endif

#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 10
#endif

    // adcStart(BATTERY_PIN);
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS); // Default of 12 is not very linear. Recommended to use 10 or 11
                                                         // depending on needed resolution.
    batteryLevel = &analogLevel;
    return true;
#else
    return false;
#endif
}

bool Power::setup()
{
    bool found = axpChipInit();

    if (!found) {
        found = analogInit();
    }
    enabled = found;
    low_voltage_counter = 0;

    return found;
}

void Power::shutdown()
{
    screen->setOn(false);
#if defined(USE_EINK) && defined(PIN_EINK_EN)
    digitalWrite(PIN_EINK_EN, LOW); //power off backlight first
#endif

#ifdef HAS_PMU
    DEBUG_MSG("Shutting down\n");
    if(PMU) {
        PMU->setChargingLedMode(XPOWERS_CHG_LED_OFF);
        PMU->shutdown();
    }
#elif defined(ARCH_NRF52)
    playBeep();
    ledOff(PIN_LED1);
    ledOff(PIN_LED2);
    doDeepSleep(DELAY_FOREVER);
#endif
}

/// Reads power status to powerStatus singleton.
//
// TODO(girts): move this and other axp stuff to power.h/power.cpp.
void Power::readPowerStatus()
{
    if (batteryLevel) {
        bool hasBattery = batteryLevel->isBatteryConnect();
        int batteryVoltageMv = 0;
        int8_t batteryChargePercent = 0;
        if (hasBattery) {
            batteryVoltageMv = batteryLevel->getBattVoltage();
            // If the AXP192 returns a valid battery percentage, use it
            if (batteryLevel->getBatteryPercent() >= 0) {
                batteryChargePercent = batteryLevel->getBatteryPercent();
            } else {
                // If the AXP192 returns a percentage less than 0, the feature is either not supported or there is an error
                // In that case, we compute an estimate of the charge percent based on maximum and minimum voltages defined in
                // power.h
                batteryChargePercent =
                    clamp((int)(((batteryVoltageMv - BAT_MILLIVOLTS_EMPTY) * 1e2) / (BAT_MILLIVOLTS_FULL - BAT_MILLIVOLTS_EMPTY)),
                          0, 100);
            }
        }

        // Notify any status instances that are observing us
        const PowerStatus powerStatus2 =
            PowerStatus(hasBattery ? OptTrue : OptFalse, batteryLevel->isVbusIn() ? OptTrue : OptFalse,
                        batteryLevel->isCharging() ? OptTrue : OptFalse, batteryVoltageMv, batteryChargePercent);
        DEBUG_MSG("Battery: usbPower=%d, isCharging=%d, batMv=%d, batPct=%d\n", powerStatus2.getHasUSB(),
                  powerStatus2.getIsCharging(), powerStatus2.getBatteryVoltageMv(), powerStatus2.getBatteryChargePercent());
        newStatus.notifyObservers(&powerStatus2);

// If we have a battery at all and it is less than 10% full, force deep sleep if we have more than 3 low readings in a row
// Supect fluctuating voltage on the RAK4631 to force it to deep sleep even if battery is at 85% after only a few days
#ifdef ARCH_NRF52
        if (powerStatus2.getHasBattery() && !powerStatus2.getHasUSB()) {
            if (batteryLevel->getBattVoltage() < MIN_BAT_MILLIVOLTS) {
                low_voltage_counter++;
                if (low_voltage_counter > 3)
                    powerFSM.trigger(EVENT_LOW_BATTERY);
            } else {
                low_voltage_counter = 0;
            }
        }
#else
        // If we have a battery at all and it is less than 10% full, force deep sleep
        if (powerStatus2.getHasBattery() && !powerStatus2.getHasUSB() && batteryLevel->getBattVoltage() < MIN_BAT_MILLIVOLTS)
            powerFSM.trigger(EVENT_LOW_BATTERY);
#endif
    } else {
        // No power sensing on this board - tell everyone else we have no idea what is happening
        const PowerStatus powerStatus3 = PowerStatus(OptUnknown, OptUnknown, OptUnknown, -1, -1);
        newStatus.notifyObservers(&powerStatus3);
    }
}

int32_t Power::runOnce()
{
    readPowerStatus();

#ifdef HAS_PMU
    // WE no longer use the IRQ line to wake the CPU (due to false wakes from sleep), but we do poll
    // the IRQ status by reading the registers over I2C
    if(PMU) {

        PMU->getIrqStatus();

        if(PMU->isVbusRemoveIrq()){
            DEBUG_MSG("USB unplugged\n");
            powerFSM.trigger(EVENT_POWER_DISCONNECTED);
        }

        if (PMU->isVbusInsertIrq()) {
            DEBUG_MSG("USB plugged In\n");
            powerFSM.trigger(EVENT_POWER_CONNECTED);
        }

        /*
        Other things we could check if we cared...

        if (PMU->isBatChagerStartIrq()) {
            DEBUG_MSG("Battery start charging\n");
        }
        if (PMU->isBatChagerDoneIrq()) {
            DEBUG_MSG("Battery fully charged\n");
        }
        if (PMU->isBatInsertIrq()) {
            DEBUG_MSG("Battery inserted\n");
        }
        if (PMU->isBatRemoveIrq()) {
            DEBUG_MSG("Battery removed\n");
        }
        */
        if (PMU->isPekeyLongPressIrq()) {
            DEBUG_MSG("PEK long button press\n");
            screen->setOn(false);
        }

        PMU->clearIrqStatus();
    }
#endif
    // Only read once every 20 seconds once the power status for the app has been initialized
    return (statusHandler && statusHandler->isInitialized()) ? (1000 * 20) : RUN_SAME;
}

/**
 * Init the power manager chip
 *
 * axp192 power
    DCDC1 0.7-3.5V @ 1200mA max -> OLED // If you turn this off you'll lose comms to the axp192 because the OLED and the axp192
 share the same i2c bus, instead use ssd1306 sleep mode DCDC2 -> unused DCDC3 0.7-3.5V @ 700mA max -> ESP32 (keep this on!) LDO1
 30mA -> charges GPS backup battery // charges the tiny J13 battery by the GPS to power the GPS ram (for a couple of days), can
 not be turned off LDO2 200mA -> LORA LDO3 200mA -> GPS
 * 
 */
bool Power::axpChipInit()
{

#ifdef HAS_PMU

        TwoWire * w = NULL;

    // Use macro to distinguish which wire is used by PMU
#ifdef PMU_USE_WIRE1
        w = &Wire1;
#else
        w = &Wire;
#endif

    /**
     * It is not necessary to specify the wire pin, 
     * just input the wire, because the wire has been initialized in main.cpp
     */
    if (!PMU) {
        PMU = new XPowersAXP2101(*w);
        if (!PMU->init()) {
            DEBUG_MSG("Warning: Failed to find AXP2101 power management\n");
            delete PMU;
            PMU = NULL;
        } else {
            DEBUG_MSG("AXP2101 PMU init succeeded, using AXP2101 PMU\n");
        }
    }

    if (!PMU) {
        PMU = new XPowersAXP192(*w);
        if (!PMU->init()) {
            DEBUG_MSG("Warning: Failed to find AXP192 power management\n");
            delete PMU;
            PMU = NULL;
        } else {
            DEBUG_MSG("AXP192 PMU init succeeded, using AXP192 PMU\n");
        }
    }

    if (!PMU) {
                /*
        * In XPowersLib, if the XPowersAXPxxx object is released, Wire.end() will be called at the same time. 
        * In order not to affect other devices, if the initialization of the PMU fails, Wire needs to be re-initialized once, 
        * if there are multiple devices sharing the bus.
        * * */
#ifndef PMU_USE_WIRE1
        w->begin(I2C_SDA, I2C_SCL);
#endif
        return false;
    }

    batteryLevel = PMU;

    if (PMU->getChipModel() == XPOWERS_AXP192) {
        
        // lora radio power channel
        PMU->setPowerChannelVoltage(XPOWERS_LDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_LDO2);


        // oled module power channel,
        // disable it will cause abnormal communication between boot and AXP power supply, 
        // do not turn it off
        PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
        // enable oled power
        PMU->enablePowerOutput(XPOWERS_DCDC1);


        // gnss module power channel -  now turned on in setGpsPower
        PMU->setPowerChannelVoltage(XPOWERS_LDO3, 3300);
        // PMU->enablePowerOutput(XPOWERS_LDO3);


        //protected oled power source
        PMU->setProtectedChannel(XPOWERS_DCDC1);
        //protected esp32 power source
        PMU->setProtectedChannel(XPOWERS_DCDC3);

        //disable not use channel
        PMU->disablePowerOutput(XPOWERS_DCDC2);

        //disable all axp chip interrupt
        PMU->disableIRQ(XPOWERS_AXP192_ALL_IRQ);

        // Set constant current charging current
        PMU->setChargerConstantCurr(XPOWERS_AXP192_CHG_CUR_450MA);

    } else if (PMU->getChipModel() == XPOWERS_AXP2101) {

        // t-beam s3 core 

        /**
         * gnss module power channel 
         * The default ALDO4 is off, you need to turn on the GNSS power first, otherwise it will be invalid during initialization
         */
        PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO4);

        // lora radio power channel
        PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO3);

        // m.2 interface 
        PMU->setPowerChannelVoltage(XPOWERS_DCDC3, 3300);
        PMU->enablePowerOutput(XPOWERS_DCDC3);

        /**
        * ALDO2 cannot be turned off. 
        * It is a necessary condition for sensor communication. 
        * It must be turned on to properly access the sensor and screen
        * It is also responsible for the power supply of PCF8563
        */
        PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO2);

        // 6-axis , magnetometer ,bme280 , oled screen power channel 
        PMU->setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO1);

        // sdcard power channle 
        PMU->setPowerChannelVoltage(XPOWERS_BLDO1, 3300);
        PMU->enablePowerOutput(XPOWERS_BLDO1);
        
        // PMU->setPowerChannelVoltage(XPOWERS_DCDC4, 3300);
        // PMU->enablePowerOutput(XPOWERS_DCDC4);

        //not use channel
        PMU->disablePowerOutput(XPOWERS_DCDC2); //not elicited
        PMU->disablePowerOutput(XPOWERS_DCDC5); //not elicited
        PMU->disablePowerOutput(XPOWERS_DLDO1); //Invalid power channel, it does not exist
        PMU->disablePowerOutput(XPOWERS_DLDO2); //Invalid power channel, it does not exist
        PMU->disablePowerOutput(XPOWERS_VBACKUP);

        //disable all axp chip interrupt
        PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

        //Set the constant current charging current of AXP2101, temporarily use 500mA by default
        PMU->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);

    }
    

    PMU->clearIrqStatus();

    // TBeam1.1 /T-Beam S3-Core has no external TS detection, 
    // it needs to be disabled, otherwise it will cause abnormal charging
    PMU->disableTSPinMeasure();

    // PMU->enableSystemVoltageMeasure();
    PMU->enableVbusVoltageMeasure();
    PMU->enableBattVoltageMeasure();

    DEBUG_MSG("=======================================================================\n");
    if (PMU->isChannelAvailable(XPOWERS_DCDC1)) {
        DEBUG_MSG("DC1  : %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_DCDC1)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_DCDC1));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC2)) {
        DEBUG_MSG("DC2  : %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_DCDC2)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_DCDC2));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC3)) {
        DEBUG_MSG("DC3  : %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_DCDC3)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_DCDC3));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC4)) {
        DEBUG_MSG("DC4  : %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_DCDC4)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_DCDC4));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO2)) {
        DEBUG_MSG("LDO2 : %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_LDO2)   ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_LDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO3)) {
        DEBUG_MSG("LDO3 : %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_LDO3)   ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_LDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO1)) {
        DEBUG_MSG("ALDO1: %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_ALDO1)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_ALDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO2)) {
        DEBUG_MSG("ALDO2: %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_ALDO2)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_ALDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO3)) {
        DEBUG_MSG("ALDO3: %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_ALDO3)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_ALDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO4)) {
        DEBUG_MSG("ALDO4: %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_ALDO4)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_ALDO4));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO1)) {
        DEBUG_MSG("BLDO1: %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_BLDO1)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_BLDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO2)) {
        DEBUG_MSG("BLDO2: %s   Voltage:%u mV \n",  PMU->isPowerChannelEnable(XPOWERS_BLDO2)  ? "+" : "-",  PMU->getPowerChannelVoltage(XPOWERS_BLDO2));
    }
    DEBUG_MSG("=======================================================================\n");


    //Set up the charging voltage, AXP2101/AXP192 4.2V gear is the same
    // XPOWERS_AXP192_CHG_VOL_4V2 = XPOWERS_AXP2101_CHG_VOL_4V2
    PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);

    // Set PMU shutdown voltage at 2.6V to maximize battery utilization
    PMU->setSysPowerDownVoltage(2600);



#ifdef PMU_IRQ
        uint64_t pmuIrqMask = 0;

        if (PMU->getChipModel() == XPOWERS_AXP192) {
            pmuIrqMask = XPOWERS_AXP192_VBUS_INSERT_IRQ | XPOWERS_AXP192_BAT_INSERT_IRQ | XPOWERS_AXP192_PKEY_SHORT_IRQ;
        } else if (PMU->getChipModel() == XPOWERS_AXP2101) {
            pmuIrqMask = XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_PKEY_SHORT_IRQ;
        }

        pinMode(PMU_IRQ, INPUT);
        attachInterrupt(
            PMU_IRQ, [] { pmu_irq = true; }, FALLING);

        // we do not look for AXPXXX_CHARGING_FINISHED_IRQ & AXPXXX_CHARGING_IRQ because it occurs repeatedly while there is
        // no battery also it could cause inadvertent waking from light sleep just because the battery filled
        // we don't look for AXPXXX_BATT_REMOVED_IRQ because it occurs repeatedly while no battery installed
        // we don't look at AXPXXX_VBUS_REMOVED_IRQ because we don't have anything hooked to vbus
        PMU->enableIRQ(pmuIrqMask);

        PMU->clearIrqStatus();
#endif /*PMU_IRQ*/

            readPowerStatus();

    pmu_found = true;

    return pmu_found;

#else
    return false;
#endif
}
