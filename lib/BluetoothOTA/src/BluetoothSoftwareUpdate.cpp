#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include "configuration.h"
#include <esp_gatt_defs.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <Update.h>
#include <CRC32.h>

static BLECharacteristic swUpdateTotalSizeCharacteristic("e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
static BLECharacteristic swUpdateDataCharacteristic("e272ebac-d463-4b98-bc84-5cc1a39ee517", BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic swUpdateCRC32Characteristic("4826129c-c22a-43a3-b066-ce8f0d5bacc6", BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic swUpdateResultCharacteristic("5e134862-7411-4424-ac4a-210937432c77", BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

CRC32 crc;
uint32_t rebootAtMsec = 0; // If not zero we will reboot at this time (used to reboot shortly after the update completes)

class UpdateCallbacks : public BLECharacteristicCallbacks
{
    void onRead(BLECharacteristic *pCharacteristic) {
        DEBUG_MSG("Got on read\n");
    }

    void onWrite(BLECharacteristic *pCharacteristic)
    {
        // dumpCharacteristic(pCharacteristic);

        if (pCharacteristic == &swUpdateTotalSizeCharacteristic)
        {
            // Check if there is enough to OTA Update
            uint32_t len = getValue32(pCharacteristic, 0);
            crc.reset();
            bool canBegin = Update.begin(len);
            DEBUG_MSG("Setting update size %u, result %d\n", len, canBegin);
            if(!canBegin)
                // Indicate failure by forcing the size to 0
                pCharacteristic->setValue(0UL);
        }
        else if (pCharacteristic == &swUpdateDataCharacteristic)
        {
            std::string value = pCharacteristic->getValue();
            uint32_t len = value.length();
            uint8_t *data = pCharacteristic->getData();
            // DEBUG_MSG("Writing %u\n", len);
            crc.update(data, len);
            Update.write(data, len);
        }
        else if (pCharacteristic == &swUpdateCRC32Characteristic)
        {
            uint32_t expectedCRC = getValue32(pCharacteristic, 0);
            DEBUG_MSG("expected CRC %u\n", expectedCRC);

            uint8_t result = 0xff;

            // Check the CRC before asking the update to happen.
            if(crc.finalize() != expectedCRC) {
                DEBUG_MSG("Invalid CRC!\n");
                result = 0xe0; // FIXME, use real error codes
            }
            else {
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
            swUpdateResultCharacteristic.setValue(&result, 1);
            swUpdateResultCharacteristic.notify();
        }
        else {
            DEBUG_MSG("unexpected write\n");
        }
    }
};

void bluetoothRebootCheck() {
    if(rebootAtMsec && millis() > rebootAtMsec)
        ESP.restart();
}

static UpdateCallbacks updateCb;

/*
SoftwareUpdateService UUID cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30

Characteristics

UUID                                 properties          description
e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e write|read          total image size, 32 bit, write this first, then read read back to see if it was acceptable (0 mean not accepted)
e272ebac-d463-4b98-bc84-5cc1a39ee517 write               data, variable sized, recommended 512 bytes, write one for each block of file
4826129c-c22a-43a3-b066-ce8f0d5bacc6 write               crc32, write last - writing this will complete the OTA operation, now you can read result
5e134862-7411-4424-ac4a-210937432c77 read|notify         result code, readable but will notify when the OTA operation completes
 */
BLEService *createUpdateService(BLEServer* server) {
    // Create the BLE Service
    BLEService *service = server->createService("cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30");

    addWithDesc(service, &swUpdateTotalSizeCharacteristic, "total image size");
    addWithDesc(service, &swUpdateDataCharacteristic, "data");
    addWithDesc(service, &swUpdateCRC32Characteristic, "crc32");
    addWithDesc(service, &swUpdateResultCharacteristic, "result code");

    swUpdateTotalSizeCharacteristic.setCallbacks(&updateCb);
    swUpdateDataCharacteristic.setCallbacks(&updateCb);
    swUpdateCRC32Characteristic.setCallbacks(&updateCb);
    
    swUpdateResultCharacteristic.addDescriptor(new BLE2902()); // Needed so clients can request notification

    return service;
}

