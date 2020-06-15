#include "MeshBluetoothService.h"
#include "BluetoothUtil.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <assert.h>
#include <esp_gatt_defs.h>

#include "CallbackCharacteristic.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PhoneAPI.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh.pb.h"

// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in
// proccess at once
static uint8_t trBytes[_max(_max(_max(_max(ToRadio_size, RadioConfig_size), User_size), MyNodeInfo_size), FromRadio_size)];

static CallbackCharacteristic *meshFromNumCharacteristic;

BLEService *meshService;

class BluetoothPhoneAPI : public PhoneAPI
{
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum)
    {
        PhoneAPI::onNowHasData(fromRadioNum);

        if (meshFromNumCharacteristic) { // this ptr might change from sleep to sleep, or even be null
            meshFromNumCharacteristic->setValue(fromRadioNum);
            meshFromNumCharacteristic->notify();
        }
    }
};

BluetoothPhoneAPI *bluetoothPhoneAPI;


class ToRadioCharacteristic : public CallbackCharacteristic
{
  public:
    ToRadioCharacteristic() : CallbackCharacteristic("f75c76d2-129e-4dad-a1dd-7866124401e7", BLECharacteristic::PROPERTY_WRITE) {}

    void onWrite(BLECharacteristic *c)
    {
        bluetoothPhoneAPI->handleToRadio(c->getData(), c->getValue().length());
    }
};

class FromRadioCharacteristic : public CallbackCharacteristic
{
  public:
    FromRadioCharacteristic() : CallbackCharacteristic("8ba2bcc2-ee02-4a55-a531-c525c5e454d5", BLECharacteristic::PROPERTY_READ)
    {
    }

    void onRead(BLECharacteristic *c)
    {
        size_t numBytes = bluetoothPhoneAPI->getFromRadio(trBytes);

        // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
        // or make empty if the queue is empty
        if (numBytes) {
            c->setValue(trBytes, numBytes);
        } else {
            c->setValue((uint8_t *)"", 0);
        }
    }
};

class FromNumCharacteristic : public CallbackCharacteristic
{
  public:
    FromNumCharacteristic()
        : CallbackCharacteristic("ed9da18c-a800-4f66-a670-aa7547e34453", BLECharacteristic::PROPERTY_WRITE |
                                                                             BLECharacteristic::PROPERTY_READ |
                                                                             BLECharacteristic::PROPERTY_NOTIFY)
    {
        // observe(&service.fromNumChanged);
    }

    void onRead(BLECharacteristic *c) { DEBUG_MSG("FIXME implement fromnum read\n"); }
};

/*
See bluetooth-api.md for documentation.
 */
BLEService *createMeshBluetoothService(BLEServer *server)
{
    // Only create our phone API object once
    if (!bluetoothPhoneAPI) {
        bluetoothPhoneAPI = new BluetoothPhoneAPI();
        bluetoothPhoneAPI->init();
    }

    // Create the BLE Service, we need more than the default of 15 handles
    BLEService *service = server->createService(BLEUUID("6ba1b218-15a8-461f-9fa8-5dcae273eafd"), 30, 0);

    assert(!meshFromNumCharacteristic);
    meshFromNumCharacteristic = new FromNumCharacteristic;

    addWithDesc(service, meshFromNumCharacteristic, "fromRadio");
    addWithDesc(service, new ToRadioCharacteristic, "toRadio");
    addWithDesc(service, new FromRadioCharacteristic, "fromNum");

    meshFromNumCharacteristic->addDescriptor(addBLEDescriptor(new BLE2902())); // Needed so clients can request notification

    service->start();

    // We only add to advertisting once, because the ESP32 arduino code is dumb and that object never dies
    static bool firstTime = true;
    if (firstTime) {
        firstTime = false;
        server->getAdvertising()->addServiceUUID(service->getUUID());
    }

    DEBUG_MSG("*** Mesh service:\n");
    service->dump();

    meshService = service;
    return service;
}

void stopMeshBluetoothService()
{
    assert(meshService);
    meshService->stop();
    meshService->executeDelete();
}

void destroyMeshBluetoothService()
{
    assert(meshService);
    delete meshService;

    meshFromNumCharacteristic = NULL;
}
