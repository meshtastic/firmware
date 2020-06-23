#include "BluetoothSoftwareUpdate.h"
#include "BluetoothUtil.h"
#include "CallbackCharacteristic.h"
#include "RadioLibInterface.h"
#include "configuration.h"
#include "lock.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <CRC32.h>
#include <Update.h>
#include <esp_gatt_defs.h>

using namespace meshtastic;

CRC32 crc;
uint32_t rebootAtMsec = 0; // If not zero we will reboot at this time (used to reboot shortly after the update completes)

uint32_t updateExpectedSize, updateActualSize;

Lock *updateLock;

class TotalSizeCharacteristic : public CallbackCharacteristic
{
  public:
    TotalSizeCharacteristic()
        : CallbackCharacteristic("e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e",
                                 BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ)
    {
    }

    void onWrite(BLECharacteristic *c)
    {
        LockGuard g(updateLock);
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
};

#define MAX_BLOCKSIZE 512

class DataCharacteristic : public CallbackCharacteristic
{
  public:
    DataCharacteristic() : CallbackCharacteristic("e272ebac-d463-4b98-bc84-5cc1a39ee517", BLECharacteristic::PROPERTY_WRITE) {}

    void onWrite(BLECharacteristic *c)
    {
        LockGuard g(updateLock);
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
};

static BLECharacteristic *resultC;

class CRC32Characteristic : public CallbackCharacteristic
{
  public:
    CRC32Characteristic() : CallbackCharacteristic("4826129c-c22a-43a3-b066-ce8f0d5bacc6", BLECharacteristic::PROPERTY_WRITE) {}

    void onWrite(BLECharacteristic *c)
    {
        LockGuard g(updateLock);
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
                rebootAtMsec = millis() + 5000;
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
};

void bluetoothRebootCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        DEBUG_MSG("Rebooting for update\n");
        ESP.restart();
    }
}

/*
See bluetooth-api.md

 */
BLEService *createUpdateService(BLEServer *server, std::string hwVendor, std::string swVersion, std::string hwVersion)
{
    if (!updateLock)
        updateLock = new Lock();

    // Create the BLE Service
    BLEService *service = server->createService(BLEUUID("cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30"), 25, 0);

    assert(!resultC);
    resultC = new BLECharacteristic("5e134862-7411-4424-ac4a-210937432c77",
                                    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    addWithDesc(service, new TotalSizeCharacteristic, "total image size");
    addWithDesc(service, new DataCharacteristic, "data");
    addWithDesc(service, new CRC32Characteristic, "crc32");
    addWithDesc(service, resultC, "result code");

    resultC->addDescriptor(addBLEDescriptor(new BLE2902())); // Needed so clients can request notification

    BLECharacteristic *swC =
        new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_SW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
    swC->setValue(swVersion);
    service->addCharacteristic(addBLECharacteristic(swC));

    BLECharacteristic *mfC = new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_MANU_NAME), BLECharacteristic::PROPERTY_READ);
    mfC->setValue(hwVendor);
    service->addCharacteristic(addBLECharacteristic(mfC));

    BLECharacteristic *hwvC =
        new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_HW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
    hwvC->setValue(hwVersion);
    service->addCharacteristic(addBLECharacteristic(hwvC));

    return service;
}

void destroyUpdateService()
{
    assert(resultC);

    resultC = NULL;
}