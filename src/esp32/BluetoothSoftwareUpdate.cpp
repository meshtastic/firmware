#include <Arduino.h>

#include "../concurrency/LockGuard.h"
#include "../timing.h"
#include "BluetoothSoftwareUpdate.h"
#include "RadioLibInterface.h"
#include "configuration.h"

#include <CRC32.h>
#include <Update.h>

#include "CallbackCharacteristic.h"

int16_t updateResultHandle;

static CRC32 crc;
static uint32_t rebootAtMsec = 0; // If not zero we will reboot at this time (used to reboot shortly after the update completes)

static uint32_t updateExpectedSize, updateActualSize;

static concurrency::Lock *updateLock;

/// Handle writes & reads to total size
int totalSize_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    concurrency::LockGuard g(updateLock);
    // Check if there is enough to OTA Update
    uint32_t len = getValue32(c, 0);
    updateExpectedSize = len;
    updateActualSize = 0;
    crc.reset();
    bool canBegin = Update.begin(len);
    DEBUG_MSG("Setting update size %u, result %d\n", len, canBegin);
    if (!canBegin) {
        // Indicate failure by forcing the size to 0
        uint32_t zero = 0;
        c->setValue(zero);
    } else {
        // This totally breaks abstraction to up up into the app layer for this, but quick hack to make sure we only
        // talk to one service during the sw update.
        // DEBUG_MSG("FIXME, crufty shutdown of mesh bluetooth for sw update.");
        // void stopMeshBluetoothService();
        // stopMeshBluetoothService();

        if (RadioLibInterface::instance)
            RadioLibInterface::instance->sleep(); // FIXME, nasty hack - the RF95 ISR/SPI code on ESP32 can fail while we are
                                                  // writing flash - shut the radio off during updates
    }
}

#define MAX_BLOCKSIZE 512

/// Handle writes to data
int totalSize_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    concurrency::LockGuard g(updateLock);
    std::string value = c->getValue();
    uint32_t len = value.length();
    assert(len <= MAX_BLOCKSIZE);
    static uint8_t
        data[MAX_BLOCKSIZE]; // we temporarily copy here because I'm worried that a fast sender might be able overwrite srcbuf
    memcpy(data, c->getData(), len);
    // DEBUG_MSG("Writing %u\n", len);
    crc.update(data, len);
    Update.write(data, len);
    updateActualSize += len;
    powerFSM.trigger(EVENT_RECEIVED_TEXT_MSG); // Not exactly correct, but we want to force the device to not sleep now
}

/// Handle writes to crc32
int totalSize_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    concurrency::LockGuard g(updateLock);
    uint32_t expectedCRC = getValue32(c, 0);
    uint32_t actualCRC = crc.finalize();
    DEBUG_MSG("expected CRC %u\n", expectedCRC);

    uint8_t result = 0xff;

    if (updateActualSize != updateExpectedSize) {
        DEBUG_MSG("Expected %u bytes, but received %u bytes!\n", updateExpectedSize, updateActualSize);
        result = 0xe1;                   // FIXME, use real error codes
    } else if (actualCRC != expectedCRC) // Check the CRC before asking the update to happen.
    {
        DEBUG_MSG("Invalid CRC! expected=%u, actual=%u\n", expectedCRC, actualCRC);
        result = 0xe0; // FIXME, use real error codes
    } else {
        if (Update.end()) {
            DEBUG_MSG("OTA done, rebooting in 5 seconds!\n");
            rebootAtMsec = timing::millis() + 5000;
        } else {
            DEBUG_MSG("Error Occurred. Error #: %d\n", Update.getError());
        }
        result = Update.getError();
    }

    if (RadioLibInterface::instance)
        RadioLibInterface::instance->startReceive(); // Resume radio

    assert(resultC);
    resultC->setValue(&result, 1);
    resultC->notify();
}

void bluetoothRebootCheck()
{
    if (rebootAtMsec && timing::millis() > rebootAtMsec) {
        DEBUG_MSG("Rebooting for update\n");
        ESP.restart();
    }
}

/*
See bluetooth-api.md

 */
void createUpdateService()
{
    if (!updateLock)
        updateLock = new concurrency::Lock();
}
