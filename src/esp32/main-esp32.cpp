#include "BluetoothUtil.h"
#include "MeshBluetoothService.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "esp_task_wdt.h"
#include "main.h"
#include "sleep.h"
#include "target_specific.h"
#include "utils.h"

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

/*
static void printBLEinfo() {
        int dev_num = esp_ble_get_bond_device_num();

    esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    esp_ble_get_bond_device_list(&dev_num, dev_list);
    for (int i = 0; i < dev_num; i++) {
        // esp_ble_remove_bond_device(dev_list[i].bd_addr);
    }

} */

void esp32Setup()
{
    uint32_t seed = esp_random();
    DEBUG_MSG("Setting random seed %u\n", seed);
    randomSeed(seed); // ESP docs say this is fairly random

    DEBUG_MSG("Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("Free PSRAM: %d\n", ESP.getFreePsram());

    // enableModemSleep();

// Since we are turning on watchdogs rather late in the release schedule, we really don't want to catch any
// false positives.  The wait-to-sleep timeout for shutting down radios is 30 secs, so pick 45 for now.
#define APP_WATCHDOG_SECS 45

    auto res = esp_task_wdt_init(APP_WATCHDOG_SECS, true);
    assert(res == ESP_OK);

    res = esp_task_wdt_add(NULL);
    assert(res == ESP_OK);
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

/// loop code specific to ESP32 targets
void esp32Loop()
{
    esp_task_wdt_reset(); // service our app level watchdog
    loopBLE();

    // for debug printing
    // radio.radioIf.canSleep();
}