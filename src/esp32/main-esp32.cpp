#include "BluetoothUtil.h"
#include "MeshBluetoothService.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
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
        []() { screen.stopBluetoothPinScreen(); }, getDeviceName(), HW_VENDOR, xstr(APP_VERSION),
        xstr(HW_VERSION)); // FIXME, use a real name based on the macaddr
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