#include "BluetoothUtil.h"
#include "MeshBluetoothService.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "power.h"
#include "target_specific.h"

bool bluetoothOn;

// This routine is called multiple times, once each time we come back from sleep
void reinitBluetooth()
{
    DEBUG_MSG("Starting bluetooth\n");

    // FIXME - we are leaking like crazy
    // AllocatorScope scope(btPool);

    // Note: these callbacks might be coming in from a different thread.
    BLEServer *serve = initBLE(
        [](uint32_t pin) {
            powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
            screen.startBluetoothPinScreen(pin);
        },
        []() { screen.stopBluetoothPinScreen(); }, getDeviceName(), HW_VENDOR, optstr(APP_VERSION),
        optstr(HW_VERSION)); // FIXME, use a real name based on the macaddr
    createMeshBluetoothService(serve);

    // Start advertising - this must be done _after_ creating all services
    serve->getAdvertising()->start();
}

// Enable/disable bluetooth.
void setBluetoothEnable(bool on)
{
    if (on != bluetoothOn) {
        DEBUG_MSG("Setting bluetooth enable=%d\n", on);

        bluetoothOn = on;
        if (on) {
            Serial.printf("Pre BT: %u heap size\n", ESP.getFreeHeap());
            // ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
            reinitBluetooth();
        } else {
            // We have to totally teardown our bluetooth objects to prevent leaks
            stopMeshBluetoothService(); // Must do before shutting down bluetooth
            deinitBLE();
            destroyMeshBluetoothService(); // must do after deinit, because it frees our service
            Serial.printf("Shutdown BT: %u heap size\n", ESP.getFreeHeap());
            // ESP_ERROR_CHECK( heap_trace_stop() );
            // heap_trace_dump();
        }
    }
}

void getMacAddr(uint8_t *dmac)
{
    assert(esp_efuse_mac_get_default(dmac) == ESP_OK);
}

#ifdef TBEAM_V10

// FIXME. nasty hack cleanup how we load axp192
#undef AXP192_SLAVE_ADDRESS
#include "axp20x.h"
AXP20X_Class axp;
bool pmu_irq = false;

/// Reads power status to powerStatus singleton.
//
// TODO(girts): move this and other axp stuff to power.h/power.cpp.
void readPowerStatus()
{
    powerStatus.haveBattery = axp.isBatteryConnect();
    if (powerStatus.haveBattery) {
        powerStatus.batteryVoltageMv = axp.getBattVoltage();
    }
    powerStatus.usb = axp.isVBUSPlug();
    powerStatus.charging = axp.isChargeing();
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
void axp192Init()
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

void esp32Setup()
{
    uint32_t seed = esp_random();
    DEBUG_MSG("Setting random seed %u\n", seed);
    randomSeed(seed); // ESP docs say this is fairly random

    DEBUG_MSG("Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("Free PSRAM: %d\n", ESP.getFreePsram());

#ifdef AXP192_SLAVE_ADDRESS
    axp192Init();
#endif
}

#if 0
// Turn off for now

uint32_t axpDebugRead()
{
  axp.debugCharging();
  DEBUG_MSG("vbus current %f\n", axp.getVbusCurrent());
  DEBUG_MSG("charge current %f\n", axp.getBattChargeCurrent());
  DEBUG_MSG("bat voltage %f\n", axp.getBattVoltage());
  DEBUG_MSG("batt pct %d\n", axp.getBattPercentage());
  DEBUG_MSG("is battery connected %d\n", axp.isBatteryConnect());
  DEBUG_MSG("is USB connected %d\n", axp.isVBUSPlug());
  DEBUG_MSG("is charging %d\n", axp.isChargeing());

  return 30 * 1000;
}

Periodic axpDebugOutput(axpDebugRead);
#endif

/**
 * Per @spattinson
 * MIN_BAT_MILLIVOLTS seems high. Typical 18650 are different chemistry to LiPo, even for LiPos that chart seems a bit off, other
 * charts put 3690mV at about 30% for a lipo, for 18650 i think 10% remaining iis in the region of 3.2-3.3V. Reference 1st graph
 * in [this test report](https://lygte-info.dk/review/batteries2012/Samsung%20INR18650-30Q%203000mAh%20%28Pink%29%20UK.html)
 * looking at the red line - discharge at 0.2A - he gets a capacity of 2900mah, 90% of 2900 = 2610, that point in the graph looks
 * to be a shade above 3.2V
 */
#define MIN_BAT_MILLIVOLTS 3250 // millivolts. 10% per https://blog.ampow.com/lipo-voltage-chart/

/// loop code specific to ESP32 targets
void esp32Loop()
{
    loopBLE();

    // for debug printing
    // radio.radioIf.canSleep();

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

    if (powerStatus.haveBattery && !powerStatus.usb &&
        axp.getBattVoltage() < MIN_BAT_MILLIVOLTS) // If we have a battery at all and it is less than 10% full, force deep sleep
        powerFSM.trigger(EVENT_LOW_BATTERY);

#endif // T_BEAM_V10
}