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

class ProtobufCharacteristic : public CallbackCharacteristic
{
    const pb_msgdesc_t *fields;
    void *my_struct;

  public:
    ProtobufCharacteristic(const char *uuid, uint32_t btprops, const pb_msgdesc_t *_fields, void *_my_struct)
        : CallbackCharacteristic(uuid, btprops), fields(_fields), my_struct(_my_struct)
    {
        setCallbacks(this);
    }

    void onRead(BLECharacteristic *c)
    {
        size_t numbytes = pb_encode_to_bytes(trBytes, sizeof(trBytes), fields, my_struct);
        DEBUG_MSG("pbread from %s returns %d bytes\n", c->getUUID().toString().c_str(), numbytes);
        c->setValue(trBytes, numbytes);
    }

    void onWrite(BLECharacteristic *c) { writeToDest(c, my_struct); }

  protected:
    /// like onWrite, but we provide an different destination to write to, for use by subclasses that
    /// want to optionally ignore parts of writes.
    /// returns true for success
    bool writeToDest(BLECharacteristic *c, void *dest)
    {
        // dumpCharacteristic(pCharacteristic);
        std::string src = c->getValue();
        DEBUG_MSG("pbwrite to %s of %d bytes\n", c->getUUID().toString().c_str(), src.length());
        return pb_decode_from_bytes((const uint8_t *)src.c_str(), src.length(), fields, dest);
    }
};

class ToRadioCharacteristic : public CallbackCharacteristic
{
  public:
    ToRadioCharacteristic() : CallbackCharacteristic("f75c76d2-129e-4dad-a1dd-7866124401e7", BLECharacteristic::PROPERTY_WRITE) {}

    void onWrite(BLECharacteristic *c)
    {
        DEBUG_MSG("Got on write\n");

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
}

void destroyMeshBluetoothService()
{
    assert(meshService);
    delete meshService;

    meshFromNumCharacteristic = NULL;
}
