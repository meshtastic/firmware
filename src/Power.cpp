/**
 * @file Power.cpp
 * @brief This file contains the implementation of the Power class, which is
 * responsible for managing power-related functionality of the device. It
 * includes battery level sensing, power management unit (PMU) control, and
 * power state machine management. The Power class is used by the main device
 * class to manage power-related functionality.
 *
 * The file also includes implementations of various battery level sensors, such
 * as the AnalogBatteryLevel class, which assumes the battery voltage is
 * attached via a voltage-divider to an analog input.
 *
 * This file is part of the Meshtastic project.
 * For more information, see: https://meshtastic.org/
 */
#include "power.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Throttle.h"
#include "buzz/buzz.h"
#include "configuration.h"
#include "main.h"
#include "meshUtils.h"
#include "power/BatteryLevel.h"
#include "power/PowerHAL.h"
#include "sleep.h"
#ifdef ARCH_ESP32
#endif

#if defined(ARCH_PORTDUINO)
#include "api/WiFiServerAPI.h"
#include "input/LinuxInputImpl.h"
#endif

// Working USB detection for powered/charging states on the RAK platform
#ifdef NRF_APM
#include "nrfx_power.h"
#endif

#ifndef DELAY_FOREVER
#define DELAY_FOREVER portMAX_DELAY
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#if __has_include(<Adafruit_INA219.h>)
INA219Sensor ina219Sensor;
#else
NullSensor ina219Sensor;
#endif

#if __has_include(<INA226.h>)
INA226Sensor ina226Sensor;
#else
NullSensor ina226Sensor;
#endif

#if __has_include(<Adafruit_INA260.h>)
INA260Sensor ina260Sensor;
#else
NullSensor ina260Sensor;
#endif

#if __has_include(<INA3221.h>)
INA3221Sensor ina3221Sensor;
#else
NullSensor ina3221Sensor;
#endif

#endif

#if !MESHTASTIC_EXCLUDE_I2C
#if HAS_TELEMETRY && (!MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY)
#if __has_include(<Adafruit_MAX1704X.h>)
MAX17048Sensor max17048Sensor;
#else
NullSensor max17048Sensor;
#endif
#endif
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && HAS_RAKPROT
RAK9154Sensor rak9154Sensor;
#endif

#ifdef HAS_PPM
// note: XPOWERS_CHIP_XXX must be defined in variant.h
#include <XPowersLib.h>
XPowersPPM *PPM = NULL;
#endif

#ifdef HAS_PMU
XPowersLibInterface *PMU = NULL;
#endif

bool pmu_irq = false;

Power *power;

using namespace meshtastic;

/**
 * If this board has a battery level sensor, set this to a valid implementation
 */
static HasBatteryLevel *batteryLevel; // Default to NULL for no battery level sensor

static AnalogBatteryLevel analogLevel;

Power::Power() : OSThread("Power")
{
    statusHandler = {};
    low_voltage_counter = 0;
#ifdef DEBUG_HEAP
    lastheap = memGet.getFreeHeap();
#endif
}

bool Power::analogInit()
{
#ifdef EXT_PWR_DETECT
    pinMode(EXT_PWR_DETECT, EXT_PWR_DETECT_MODE);
#endif
#ifdef EXT_CHRG_DETECT
    pinMode(EXT_CHRG_DETECT, EXT_CHRG_DETECT_MODE);
#endif

#ifdef BATTERY_PIN
    LOG_DEBUG("Use analog input %d for battery level", BATTERY_PIN);

    // disable any internal pullups
    pinMode(BATTERY_PIN, INPUT);

#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 10
#endif

#ifdef ARCH_STM32WL
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
#elif defined(ARCH_ESP32)
    if (!battery_adcInit()) {
        return false;
    }
#endif // ARCH_ESP32

    // NRF52 ADC init moved to powerHAL_init in nrf52 platform

#if !defined(ARCH_ESP32) && !defined(ARCH_STM32WL)
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
#endif

    batteryLevel = &analogLevel;
    return true;
#else
    return false;
#endif
}

/**
 * Initializes the Power class.
 *
 * @return true if the setup was successful, false otherwise.
 */
bool Power::setup()
{
    bool found = false;
    if (axpChipInit()) {
        found = true;
    } else if (cw2015Init()) {
        found = true;
    } else if (max17048Init()) {
        found = true;
    } else if (lipoChargerInit()) {
        found = true;
    } else if (serialBatteryInit()) {
        found = true;
    } else if (meshSolarInit()) {
        found = true;
    } else if (analogInit()) {
        found = true;
    } else {
#ifdef NRF_APM
        found = true;
#endif
    }
    attachPowerInterrupts();
    enabled = found;
    low_voltage_counter = 0;

#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    // Used to detach and reattach interrupts
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif

    return found;
}

void Power::powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        LOG_INFO("Rebooting");
        reboot();
    }

    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        shutdownAtMsec = 0;
        shutdown();
    }
}

void Power::reboot()
{
    notifyReboot.notifyObservers(NULL);
#if defined(ARCH_ESP32)
    ESP.restart();
#elif defined(ARCH_NRF52)
    NVIC_SystemReset();
#elif defined(ARCH_RP2040)
    rp2040.reboot();
#elif defined(ARCH_PORTDUINO)
    deInitApiServer();
    if (aLinuxInputImpl)
        aLinuxInputImpl->deInit();
    SPI.end();
    Wire.end();
    Serial1.end();
    if (screen) {
        delete screen;
        screen = nullptr;
    }
    LOG_DEBUG("final reboot!");
    ::reboot();
#elif defined(ARCH_STM32WL)
    HAL_NVIC_SystemReset();
#else
    rebootAtMsec = -1;
    LOG_WARN("FIXME implement reboot for this platform. Note that some settings "
             "require a restart to be applied");
#endif
}

void Power::shutdown()
{

#if HAS_SCREEN
    if (screen) {
#ifdef T_DECK_PRO
        screen->showSimpleBanner("Device is powered off.\nConnect USB to start!",
                                 0); // T-Deck Pro has no power button
#elif defined(USE_EINK)
        screen->showSimpleBanner("Shutting Down...",
                                 2250); // dismiss after 3 seconds to avoid the
                                        // banner on the sleep screen
#else
        screen->showSimpleBanner("Shutting Down...", 0); // stays on screen
#endif
    }
#endif
#if !defined(ARCH_STM32WL)
    playShutdownMelody();
#endif
    nodeDB->saveToDisk();
#if HAS_SCREEN
    messageStore.saveToFlash();
#endif
#if defined(ARCH_NRF52) || defined(ARCH_ESP32) || defined(ARCH_RP2040)
#ifdef PIN_LED1
    ledOff(PIN_LED1);
#endif
#ifdef PIN_LED2
    ledOff(PIN_LED2);
#endif
#ifdef PIN_LED3
    ledOff(PIN_LED3);
#endif
#ifdef LED_NOTIFICATION
    ledOff(LED_NOTIFICATION);
#endif
    doDeepSleep(DELAY_FOREVER, true, true);
#elif defined(ARCH_PORTDUINO)
    exit(EXIT_SUCCESS);
#else
    LOG_WARN("FIXME implement shutdown for this platform");
#endif
}

/// Reads power status to powerStatus singleton.
//
// TODO(girts): move this and other axp stuff to power.h/power.cpp.
void Power::readPowerStatus()
{
    int32_t batteryVoltageMv = -1; // Assume unknown
    int8_t batteryChargePercent = -1;
    OptionalBool usbPowered = OptUnknown;
    OptionalBool hasBattery = OptUnknown; // These must be static because NRF_APM
                                          // code doesn't run every time
    OptionalBool isChargingNow = OptUnknown;

    if (batteryLevel) {
        hasBattery = batteryLevel->isBatteryConnect() ? OptTrue : OptFalse;
#ifndef NRF_APM
        usbPowered = batteryLevel->isVbusIn() ? OptTrue : OptFalse;
        isChargingNow = batteryLevel->isCharging() ? OptTrue : OptFalse;
#endif
        if (hasBattery) {
            batteryVoltageMv = batteryLevel->getBattVoltage();
            // If the AXP192 returns a valid battery percentage, use it
            if (batteryLevel->getBatteryPercent() >= 0) {
                batteryChargePercent = batteryLevel->getBatteryPercent();
            } else {
                // If the AXP192 returns a percentage less than 0, the feature is either
                // not supported or there is an error In that case, we compute an
                // estimate of the charge percent based on open circuit voltage table
                // defined in power.h
                batteryChargePercent = clamp((int)(((batteryVoltageMv - (OCV[NUM_OCV_POINTS - 1] * NUM_CELLS)) * 1e2) /
                                                   ((OCV[0] * NUM_CELLS) - (OCV[NUM_OCV_POINTS - 1] * NUM_CELLS))),
                                             0, 100);
            }
        }
    }

// FIXME: IMO we shouldn't be littering our code with all these ifdefs.  Way
// better instead to make a Nrf52IsUsbPowered subclass (which shares a
// superclass with the BatteryLevel stuff) that just provides a few methods. But
// in the interest of fixing this bug I'm going to follow current practice.
#ifdef NRF_APM // Section of code detects USB power on the RAK4631 and updates
               // the power states.  Takes 20 seconds or so to detect changes.

    nrfx_power_usb_state_t nrf_usb_state = nrfx_power_usbstatus_get();
    // LOG_DEBUG("NRF Power %d", nrf_usb_state);

    // If changed to DISCONNECTED
    if (nrf_usb_state == NRFX_POWER_USB_STATE_DISCONNECTED)
        isChargingNow = usbPowered = OptFalse;
    // If changed to CONNECTED / READY
    else
        isChargingNow = usbPowered = OptTrue;

#endif

    // Notify any status instances that are observing us
    const PowerStatus powerStatus2 = PowerStatus(hasBattery, usbPowered, isChargingNow, batteryVoltageMv, batteryChargePercent);
    if (millis() > lastLogTime + 50 * 1000) {
        LOG_DEBUG("Battery: usbPower=%d, isCharging=%d, batMv=%d, batPct=%d", powerStatus2.getHasUSB(),
                  powerStatus2.getIsCharging(), powerStatus2.getBatteryVoltageMv(), powerStatus2.getBatteryChargePercent());
        lastLogTime = millis();
    }
    newStatus.notifyObservers(&powerStatus2);
#ifdef DEBUG_HEAP
    if (lastheap != memGet.getFreeHeap()) {
        // Use stack-allocated buffer to avoid heap allocations in monitoring code
        char threadlist[256] = "Threads running:";
        int threadlistLen = strlen(threadlist);
        int running = 0;
        for (int i = 0; i < MAX_THREADS; i++) {
            auto thread = concurrency::mainController.get(i);
            if ((thread != nullptr) && (thread->enabled)) {
                // Use snprintf to safely append to stack buffer without heap allocation
                int remaining = sizeof(threadlist) - threadlistLen - 1;
                if (remaining > 0) {
                    int written = snprintf(threadlist + threadlistLen, remaining, " %s", thread->ThreadName.c_str());
                    if (written > 0 && written < remaining) {
                        threadlistLen += written;
                    }
                }
                running++;
            }
        }
        LOG_HEAP(threadlist);
        LOG_HEAP("Heap status: %d/%d bytes free (%d), running %d/%d threads", memGet.getFreeHeap(), memGet.getHeapSize(),
                 memGet.getFreeHeap() - lastheap, running, concurrency::mainController.size(false));
        lastheap = memGet.getFreeHeap();
    }
#ifdef DEBUG_HEAP_MQTT
    if (mqtt) {
        // send MQTT-Packet with Heap-Size
        uint8_t dmac[6];
        getMacAddr(dmac); // Get our hardware ID
        char mac[18];
        sprintf(mac, "!%02x%02x%02x%02x", dmac[2], dmac[3], dmac[4], dmac[5]);

        auto newHeap = memGet.getFreeHeap();
        // Use stack-allocated buffers to avoid heap allocations in monitoring code
        char heapTopic[128];
        snprintf(heapTopic, sizeof(heapTopic), "%s/2/heap/%s", (*moduleConfig.mqtt.root ? moduleConfig.mqtt.root : "msh"), mac);
        char heapString[16];
        snprintf(heapString, sizeof(heapString), "%u", newHeap);
        mqtt->pubSub.publish(heapTopic, heapString, false);

        auto wifiRSSI = WiFi.RSSI();
        char wifiTopic[128];
        snprintf(wifiTopic, sizeof(wifiTopic), "%s/2/wifi/%s", (*moduleConfig.mqtt.root ? moduleConfig.mqtt.root : "msh"), mac);
        char wifiString[16];
        snprintf(wifiString, sizeof(wifiString), "%d", wifiRSSI);
        mqtt->pubSub.publish(wifiTopic, wifiString, false);
    }
#endif

#endif

    // If we have a battery at all and it is less than 0%, force deep sleep if we
    // have more than 10 low readings in a row. NOTE: min LiIon/LiPo voltage
    // is 2.0 to 2.5V, current OCV min is set to 3100 that is large enough.
    //

    if (batteryLevel && powerStatus2.getHasBattery() && !powerStatus2.getHasUSB()) {
        if (batteryLevel->getBattVoltage() < OCV[NUM_OCV_POINTS - 1]) {
            low_voltage_counter++;
            LOG_DEBUG("Low voltage counter: %d/10", low_voltage_counter);
            if (low_voltage_counter > 10) {
                LOG_INFO("Low voltage detected, trigger deep sleep");
                powerFSM.trigger(EVENT_LOW_BATTERY);
            }
        } else {
            low_voltage_counter = 0;
        }
    }
}

int32_t Power::runOnce()
{
    readPowerStatus();

#ifdef HAS_PMU
    // WE no longer use the IRQ line to wake the CPU (due to false wakes from
    // sleep), but we do poll the IRQ status by reading the registers over I2C
    if (PMU) {

        PMU->getIrqStatus();

        if (PMU->isVbusRemoveIrq()) {
            LOG_INFO("USB unplugged");
            powerFSM.trigger(EVENT_POWER_DISCONNECTED);
        }

        if (PMU->isVbusInsertIrq()) {
            LOG_INFO("USB plugged In");
            powerFSM.trigger(EVENT_POWER_CONNECTED);
        }

#ifdef T_WATCH_S3
        /*
            In the T-Watch S3 this code fragment reacts to the short press of the button by switching the
            display on and off
        */
        if (PMU->isPekeyShortPressIrq()) {
            LOG_INFO("Input: Corona Button Click");
            InputEvent event = {.inputEvent = (input_broker_event)INPUT_BROKER_CANCEL, .kbchar = 0, .touchX = 0, .touchY = 0};
            inputBroker->injectInputEvent(&event);
        }
#endif
        /*
        Other things we could check if we cared...

        if (PMU->isBatChagerStartIrq()) {
            LOG_DEBUG("Battery start charging");
        }
        if (PMU->isBatChagerDoneIrq()) {
            LOG_DEBUG("Battery fully charged");
        }
        if (PMU->isBatInsertIrq()) {
            LOG_DEBUG("Battery inserted");
        }
        if (PMU->isBatRemoveIrq()) {
            LOG_DEBUG("Battery removed");
        }
        */
#ifndef T_WATCH_S3 // FIXME - why is this triggering on the T-Watch S3?
        if (PMU->isPekeyLongPressIrq()) {
            LOG_DEBUG("PEK long button press");
            if (screen)
                screen->setOn(false);
        }
#endif

        PMU->clearIrqStatus();
    }
#endif
    // Only read once every 20 seconds once the power status for the app has been
    // initialized
    return (statusHandler && statusHandler->isInitialized()) ? (1000 * 20) : RUN_SAME;
}

#ifdef ARCH_ESP32

// Detach our class' interrupts before lightsleep
// Allows sleep.cpp to configure its own interrupts, which wake the device on user-button press
int Power::beforeLightSleep(void *unused)
{
    LOG_WARN("Detaching power interrupts for sleep");
    detachPowerInterrupts();
    return 0; // Indicates success
}

// Reconfigure our interrupts
// Our class' interrupts were disconnected during sleep, to allow the user button to wake the device from sleep
int Power::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachPowerInterrupts();
    return 0; // Indicates success
}

#endif

/*
 * Attach (or re-attach) hardware interrupts for power management
 * Public method. Used outside class when waking from MCU sleep
 */
void Power::attachPowerInterrupts()
{
#ifdef EXT_PWR_DETECT
    attachInterrupt(
        EXT_PWR_DETECT,
        []() {
            power->setIntervalFromNow(0);
            runASAP = true;
        },
        CHANGE);
#endif
#ifdef BATTERY_CHARGING_INV
    attachInterrupt(
        BATTERY_CHARGING_INV,
        []() {
            power->setIntervalFromNow(0);
            runASAP = true;
        },
        CHANGE);
#endif
#ifdef EXT_CHRG_DETECT
    attachInterrupt(
        EXT_CHRG_DETECT,
        []() {
            power->setIntervalFromNow(0);
            runASAP = true;
            BaseType_t higherWake = 0;
        },
        CHANGE);
#endif
#ifdef PMU_IRQ
    if (PMU) {
        attachInterrupt(
            PMU_IRQ,
            [] {
                pmu_irq = true;
                power->setIntervalFromNow(0);
                runASAP = true;
            },
            FALLING);
    }
#endif
}

/*
 * Detach the "normal" button interrupts.
 * Public method. Used before attaching a "wake-on-button" interrupt for MCU sleep
 */
void Power::detachPowerInterrupts()
{
#ifdef EXT_PWR_DETECT
    detachInterrupt(EXT_PWR_DETECT);
#endif
#ifdef BATTERY_CHARGING_INV
    detachInterrupt(BATTERY_CHARGING_INV);
#endif
#ifdef EXT_CHRG_DETECT
    detachInterrupt(EXT_CHRG_DETECT);
#endif
#ifdef PMU_IRQ
    if (PMU) {
        detachInterrupt(PMU_IRQ);
    }
#endif
}

/**
 * Init the power manager chip
 *
 * axp192 power
    DCDC1 0.7-3.5V @ 1200mA max -> OLED // If you turn this off you'll lose
 comms to the axp192 because the OLED and the axp192 share the same i2c bus,
 instead use ssd1306 sleep mode DCDC2 -> unused DCDC3 0.7-3.5V @ 700mA max ->
 ESP32 (keep this on!) LDO1 30mA -> charges GPS backup battery // charges the
 tiny J13 battery by the GPS to power the GPS ram (for a couple of days), can
 not be turned off LDO2 200mA -> LORA LDO3 200mA -> GPS
 *
 */
bool Power::axpChipInit()
{

#ifdef HAS_PMU

    TwoWire *w = NULL;

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
            LOG_WARN("No AXP2101 power management");
            delete PMU;
            PMU = NULL;
        } else {
            LOG_INFO("AXP2101 PMU init succeeded");
        }
    }

    if (!PMU) {
        PMU = new XPowersAXP192(*w);
        if (!PMU->init()) {
            LOG_WARN("No AXP192 power management");
            delete PMU;
            PMU = NULL;
        } else {
            LOG_INFO("AXP192 PMU init succeeded");
        }
    }

    if (!PMU) {
        /*
         * In XPowersLib, if the XPowersAXPxxx object is released, Wire.end() will
         * be called at the same time. In order not to affect other devices, if the
         * initialization of the PMU fails, Wire needs to be re-initialized once, if
         * there are multiple devices sharing the bus.
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
        // disable it will cause abnormal communication between boot and AXP power
        // supply, do not turn it off
        PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
        // enable oled power
        PMU->enablePowerOutput(XPOWERS_DCDC1);

        // gnss module power channel -  now turned on in setGpsPower
        PMU->setPowerChannelVoltage(XPOWERS_LDO3, 3300);
        // PMU->enablePowerOutput(XPOWERS_LDO3);

        // protected oled power source
        PMU->setProtectedChannel(XPOWERS_DCDC1);
        // protected esp32 power source
        PMU->setProtectedChannel(XPOWERS_DCDC3);

        // disable not use channel
        PMU->disablePowerOutput(XPOWERS_DCDC2);

        // disable all axp chip interrupt
        PMU->disableIRQ(XPOWERS_AXP192_ALL_IRQ);

        // Set constant current charging current
        PMU->setChargerConstantCurr(XPOWERS_AXP192_CHG_CUR_450MA);

        // Set up the charging voltage
        PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);
    } else if (PMU->getChipModel() == XPOWERS_AXP2101) {

        /*The alternative version of T-Beam 1.1 differs from T-Beam V1.1 in that it
         * uses an AXP2101 power chip*/
        if (HW_VENDOR == meshtastic_HardwareModel_TBEAM) {
            // Unuse power channel
            PMU->disablePowerOutput(XPOWERS_DCDC2);
            PMU->disablePowerOutput(XPOWERS_DCDC3);
            PMU->disablePowerOutput(XPOWERS_DCDC4);
            PMU->disablePowerOutput(XPOWERS_DCDC5);
            PMU->disablePowerOutput(XPOWERS_ALDO1);
            PMU->disablePowerOutput(XPOWERS_ALDO4);
            PMU->disablePowerOutput(XPOWERS_BLDO1);
            PMU->disablePowerOutput(XPOWERS_BLDO2);
            PMU->disablePowerOutput(XPOWERS_DLDO1);
            PMU->disablePowerOutput(XPOWERS_DLDO2);

            // GNSS RTC PowerVDD 3300mV
            PMU->setPowerChannelVoltage(XPOWERS_VBACKUP, 3300);
            PMU->enablePowerOutput(XPOWERS_VBACKUP);

            // ESP32 VDD 3300mV
            //  ! No need to set, automatically open , Don't close it
            //  PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
            //  PMU->setProtectedChannel(XPOWERS_DCDC1);

            // LoRa VDD 3300mV
            PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO2);

            // GNSS VDD 3300mV
            PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO3);
        } else if (HW_VENDOR == meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE ||
                   HW_VENDOR == meshtastic_HardwareModel_T_WATCH_S3) {
            // t-beam s3 core
            /**
             * gnss module power channel
             * The default ALDO4 is off, you need to turn on the GNSS power first,
             * otherwise it will be invalid during initialization
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

            // sdcard (T-Beam S3) / gnns (T-Watch S3 Plus) power channel
            PMU->setPowerChannelVoltage(XPOWERS_BLDO1, 3300);
#ifndef T_WATCH_S3
            PMU->enablePowerOutput(XPOWERS_BLDO1);
#else
            // DRV2605 power channel
            PMU->setPowerChannelVoltage(XPOWERS_BLDO2, 3300);
            PMU->enablePowerOutput(XPOWERS_BLDO2);
#endif

            // PMU->setPowerChannelVoltage(XPOWERS_DCDC4, 3300);
            // PMU->enablePowerOutput(XPOWERS_DCDC4);

            // not use channel
            PMU->disablePowerOutput(XPOWERS_DCDC2); // not elicited
            PMU->disablePowerOutput(XPOWERS_DCDC5); // not elicited
            PMU->disablePowerOutput(XPOWERS_DLDO1); // Invalid power channel, it does not exist
            PMU->disablePowerOutput(XPOWERS_DLDO2); // Invalid power channel, it does not exist
            PMU->disablePowerOutput(XPOWERS_VBACKUP);
        }

        // disable all axp chip interrupt
        PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

        // Set the constant current charging current of AXP2101, temporarily use
        // 500mA by default
        PMU->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);

        // Set up the charging voltage
        PMU->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    }

    PMU->clearIrqStatus();

    // TBeam1.1 /T-Beam S3-Core has no external TS detection,
    // it needs to be disabled, otherwise it will cause abnormal charging
    PMU->disableTSPinMeasure();

    // PMU->enableSystemVoltageMeasure();
    PMU->enableVbusVoltageMeasure();
    PMU->enableBattVoltageMeasure();

    if (PMU->isChannelAvailable(XPOWERS_DCDC1)) {
        LOG_DEBUG("DC1  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC1) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC1));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC2)) {
        LOG_DEBUG("DC2  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC2));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC3)) {
        LOG_DEBUG("DC3  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC3) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC3));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC4)) {
        LOG_DEBUG("DC4  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC4) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC4));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO2)) {
        LOG_DEBUG("LDO2 : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_LDO2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_LDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO3)) {
        LOG_DEBUG("LDO3 : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_LDO3) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_LDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO1)) {
        LOG_DEBUG("ALDO1: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO1) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO2)) {
        LOG_DEBUG("ALDO2: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO3)) {
        LOG_DEBUG("ALDO3: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO3) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO4)) {
        LOG_DEBUG("ALDO4: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO4) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO4));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO1)) {
        LOG_DEBUG("BLDO1: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_BLDO1) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_BLDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO2)) {
        LOG_DEBUG("BLDO2: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_BLDO2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_BLDO2));
    }

// We can safely ignore this approach for most (or all) boards because MCU
// turned off earlier than battery discharged to 2.6V.
//
// Unfortunately for now we can't use this killswitch for RAK4630-based boards
// because they have a bug with battery voltage measurement. Probably it
// sometimes drops to low values.
#ifndef RAK4630
    // Set PMU shutdown voltage at 2.6V to maximize battery utilization
    PMU->setSysPowerDownVoltage(2600);
#endif

#ifdef PMU_IRQ
    uint64_t pmuIrqMask = 0;

    if (PMU->getChipModel() == XPOWERS_AXP192) {
        pmuIrqMask = XPOWERS_AXP192_VBUS_INSERT_IRQ | XPOWERS_AXP192_BAT_INSERT_IRQ | XPOWERS_AXP192_PKEY_SHORT_IRQ;
    } else if (PMU->getChipModel() == XPOWERS_AXP2101) {
        pmuIrqMask = XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_PKEY_SHORT_IRQ;
    }

    pinMode(PMU_IRQ, INPUT);

    // we do not look for AXPXXX_CHARGING_FINISHED_IRQ & AXPXXX_CHARGING_IRQ
    // because it occurs repeatedly while there is no battery also it could cause
    // inadvertent waking from light sleep just because the battery filled we
    // don't look for AXPXXX_BATT_REMOVED_IRQ because it occurs repeatedly while
    // no battery installed we don't look at AXPXXX_VBUS_REMOVED_IRQ because we
    // don't have anything hooked to vbus
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

#if !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_MAX1704X.h>)
static MAX17048BatteryLevel max17048Level;
#endif

bool Power::max17048Init()
{
#if !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_MAX1704X.h>)
    bool result = max17048Level.runOnce();
    LOG_DEBUG("Power::max17048Init lipo sensor is %s", result ? "ready" : "not ready yet");
    if (!result)
        return false;
    batteryLevel = &max17048Level;
    return true;
#else
    return false;
#endif
}

#if !MESHTASTIC_EXCLUDE_I2C && HAS_CW2015
static CW2015BatteryLevel cw2015Level;
#endif

bool Power::cw2015Init()
{
#if !MESHTASTIC_EXCLUDE_I2C && HAS_CW2015
    Wire.beginTransmission(CW2015_ADDR);
    uint8_t getInfo[] = {0x0a, 0x00};
    Wire.write(getInfo, 2);
    Wire.endTransmission();
    delay(10);
    Wire.beginTransmission(CW2015_ADDR);
    Wire.write(0x00);
    bool result = false;
    if (Wire.endTransmission() == 0) {
        if (Wire.requestFrom(CW2015_ADDR, (uint8_t)1)) {
            uint8_t data = Wire.read();
            LOG_DEBUG("CW2015 init read data: 0x%x", data);
            if (data == 0x73) {
                result = true;
                batteryLevel = &cw2015Level;
            }
        }
    }
    return result;
#else
    return false;
#endif
}

#if defined(HAS_PPM) && HAS_PPM
static LipoCharger lipoCharger;
#endif

bool Power::lipoChargerInit()
{
#if defined(HAS_PPM) && HAS_PPM
    bool result = lipoCharger.runOnce();
    LOG_DEBUG("Power::lipoChargerInit lipo sensor is %s", result ? "ready" : "not ready yet");
    if (!result)
        return false;
    batteryLevel = &lipoCharger;
    return true;
#else
    return false;
#endif
}

#ifdef HELTEC_MESH_SOLAR
static meshSolarBatteryLevel meshSolarLevel;
#endif

bool Power::meshSolarInit()
{
#ifdef HELTEC_MESH_SOLAR
    bool result = meshSolarLevel.runOnce();
    LOG_DEBUG("Power::meshSolarInit mesh solar sensor is %s", result ? "ready" : "not ready yet");
    if (!result)
        return false;
    batteryLevel = &meshSolarLevel;
    return true;
#else
    return false;
#endif
}

#ifdef HAS_SERIAL_BATTERY_LEVEL
static SerialBatteryLevel serialBatteryLevel;
#endif

bool Power::serialBatteryInit()
{
#ifdef HAS_SERIAL_BATTERY_LEVEL
#ifdef EXT_PWR_DETECT
    pinMode(EXT_PWR_DETECT, EXT_PWR_DETECT_MODE);
#endif
#ifdef EXT_CHRG_DETECT
    pinMode(EXT_CHRG_DETECT, EXT_CHRG_DETECT_MODE);
#endif
    bool result = serialBatteryLevel.runOnce();
    LOG_DEBUG("Power::serialBatteryInit serial battery sensor is %s", result ? "ready" : "not ready yet");
    if (!result)
        return false;
    batteryLevel = &serialBatteryLevel;
    return true;
#else
    return false;
#endif
}
