#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include "configuration.h"
#include <esp_gatt_defs.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <Update.h>
#include <CRC32.h>
#include "CallbackCharacteristic.h"

CRC32 crc;
uint32_t rebootAtMsec = 0; // If not zero we will reboot at this time (used to reboot shortly after the update completes)

class TotalSizeCharacteristic : public CallbackCharacteristic
{
public:
    TotalSizeCharacteristic()
        : CallbackCharacteristic("e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ)
    {
    }

    void onWrite(BLECharacteristic *c)
    {
        BLEKeepAliveCallbacks::onWrite(c);

        // Check if there is enough to OTA Update
        uint32_t len = getValue32(c, 0);
        crc.reset();
        bool canBegin = Update.begin(len);
        DEBUG_MSG("Setting update size %u, result %d\n", len, canBegin);
        if (!canBegin)
            // Indicate failure by forcing the size to 0
            c->setValue(0UL);
        else {
            // This totally breaks abstraction to up up into the app layer for this, but quick hack to make sure we only
            // talk to one service during the sw update.
            //DEBUG_MSG("FIXME, crufty shutdown of mesh bluetooth for sw update.");
            //void stopMeshBluetoothService();
            //stopMeshBluetoothService();
        }
    }
};

class DataCharacteristic : public CallbackCharacteristic
{
public:
    DataCharacteristic()
        : CallbackCharacteristic(
              "e272ebac-d463-4b98-bc84-5cc1a39ee517", BLECharacteristic::PROPERTY_WRITE)
    {
    }

    void onWrite(BLECharacteristic *c)
    {
        BLEKeepAliveCallbacks::onWrite(c);

        std::string value = c->getValue();
        uint32_t len = value.length();
        uint8_t *data = c->getData();
        // DEBUG_MSG("Writing %u\n", len);
        crc.update(data, len);
        Update.write(data, len);
    }
};

static BLECharacteristic *resultC;

class CRC32Characteristic : public CallbackCharacteristic
{
public:
    CRC32Characteristic()
        : CallbackCharacteristic(
              "4826129c-c22a-43a3-b066-ce8f0d5bacc6", BLECharacteristic::PROPERTY_WRITE)
    {
    }

    void onWrite(BLECharacteristic *c)
    {
        BLEKeepAliveCallbacks::onWrite(c);

        uint32_t expectedCRC = getValue32(c, 0);
        DEBUG_MSG("expected CRC %u\n", expectedCRC);

        uint8_t result = 0xff;

        // Check the CRC before asking the update to happen.
        if (crc.finalize() != expectedCRC)
        {
            DEBUG_MSG("Invalid CRC!\n");
            result = 0xe0; // FIXME, use real error codes
        }
        else
        {
            if (Update.end())
            {
                DEBUG_MSG("OTA done, rebooting in 5 seconds!\n");
                rebootAtMsec = millis() + 5000;
            }
            else
            {
                DEBUG_MSG("Error Occurred. Error #: %d\n", Update.getError());
            }
            result = Update.getError();
        }
        assert(resultC);
        resultC->setValue(&result, 1);
        resultC->notify();
    }
};



void bluetoothRebootCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec)
        ESP.restart();
}

/*
SoftwareUpdateService UUID cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30

Characteristics

UUID                                 properties          description
e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e write|read          total image size, 32 bit, write this first, then read read back to see if it was acceptable (0 mean not accepted)
e272ebac-d463-4b98-bc84-5cc1a39ee517 write               data, variable sized, recommended 512 bytes, write one for each block of file
4826129c-c22a-43a3-b066-ce8f0d5bacc6 write               crc32, write last - writing this will complete the OTA operation, now you can read result
5e134862-7411-4424-ac4a-210937432c77 read|notify         result code, readable but will notify when the OTA operation completes

We also implement the following standard GATT entries because SW update probably needs them:

ESP_GATT_UUID_SW_VERSION_STR/0x2a28
ESP_GATT_UUID_MANU_NAME/0x2a29
ESP_GATT_UUID_HW_VERSION_STR/0x2a27

 */
BLEService *createUpdateService(BLEServer *server, std::string hwVendor, std::string swVersion, std::string hwVersion)
{
    // Create the BLE Service
    BLEService *service = server->createService(BLEUUID("cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30"), 25, 0);

    assert(!resultC);
    resultC = new BLECharacteristic("5e134862-7411-4424-ac4a-210937432c77", BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    addWithDesc(service, new TotalSizeCharacteristic, "total image size");
    addWithDesc(service, new DataCharacteristic, "data");
    addWithDesc(service, new CRC32Characteristic, "crc32");
    addWithDesc(service, resultC, "result code");

    resultC->addDescriptor(addBLEDescriptor(new BLE2902())); // Needed so clients can request notification

    BLECharacteristic *swC = new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_SW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
    swC->setValue(swVersion);
    service->addCharacteristic(addBLECharacteristic(swC));

    BLECharacteristic *mfC = new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_MANU_NAME), BLECharacteristic::PROPERTY_READ);
    mfC->setValue(hwVendor);
    service->addCharacteristic(addBLECharacteristic(mfC));

    BLECharacteristic *hwvC = new BLECharacteristic(BLEUUID((uint16_t)ESP_GATT_UUID_HW_VERSION_STR), BLECharacteristic::PROPERTY_READ);
    hwvC->setValue(hwVersion);
    service->addCharacteristic(addBLECharacteristic(hwvC));

    return service;
}

void destroyUpdateService()
{
    assert(resultC);

    resultC = NULL;
}