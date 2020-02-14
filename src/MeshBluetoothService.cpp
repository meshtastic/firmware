#include "BluetoothUtil.h"
#include "MeshBluetoothService.h"
#include <esp_gatt_defs.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <assert.h>

#include "mesh.pb.h"
#include "MeshService.h"
#include "mesh-pb-constants.h"
#include "NodeDB.h"
#include "configuration.h"

// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in proccess at once
static uint8_t trBytes[_max(_max(_max(_max(ToRadio_size, RadioConfig_size), User_size), MyNodeInfo_size), FromRadio_size)];

class ProtobufCharacteristic : public BLECharacteristic, public BLECharacteristicCallbacks
{
    const pb_msgdesc_t *fields;
    void *my_struct;

public:
    ProtobufCharacteristic(const char *uuid, uint32_t btprops, const pb_msgdesc_t *_fields, void *_my_struct)
        : BLECharacteristic(uuid, btprops),
          fields(_fields),
          my_struct(_my_struct)
    {
        setCallbacks(this);
    }

    void onRead(BLECharacteristic *c)
    {
        DEBUG_MSG("Got proto read\n");
        size_t numbytes = pb_encode_to_bytes(trBytes, sizeof(trBytes), fields, my_struct);
        c->setValue(trBytes, numbytes);
    }

    void onWrite(BLECharacteristic *c)
    {
        writeToDest(c, my_struct);
    }

protected:
    /// like onWrite, but we provide an different destination to write to, for use by subclasses that
    /// want to optionally ignore parts of writes.
    /// returns true for success
    bool writeToDest(BLECharacteristic *c, void *dest)
    {
        // dumpCharacteristic(pCharacteristic);
        DEBUG_MSG("Got on proto write\n");
        std::string src = c->getValue();
        return pb_decode_from_bytes((const uint8_t *)src.c_str(), src.length(), fields, dest);
    }
};

class NodeInfoCharacteristic : public BLECharacteristic, public BLECharacteristicCallbacks
{
public:
    NodeInfoCharacteristic()
        : BLECharacteristic("d31e02e0-c8ab-4d3f-9cc9-0b8466bdabe8", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ)
    {
        setCallbacks(this);
    }

    void onRead(BLECharacteristic *c)
    {
        DEBUG_MSG("Got nodeinfo read\n");
        const NodeInfo *info = nodeDB.readNextInfo();

        if (info)
        {
            size_t numbytes = pb_encode_to_bytes(trBytes, sizeof(trBytes), NodeInfo_fields, info);
            c->setValue(trBytes, numbytes);
        }
        else
        {
            c->setValue(trBytes, 0); // Send an empty response
        }
    }

    void onWrite(BLECharacteristic *c)
    {
        // dumpCharacteristic(pCharacteristic);
        DEBUG_MSG("Got on nodeinfo write\n");
        nodeDB.resetReadPointer();
    }
};

// wrap our protobuf version with something that forces the service to reload the config
class RadioCharacteristic : public ProtobufCharacteristic
{
public:
    RadioCharacteristic()
        : ProtobufCharacteristic("b56786c8-839a-44a1-b98e-a1724c4a0262", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ, RadioConfig_fields, &radioConfig)
    {
    }

    void onWrite(BLECharacteristic *c)
    {
        ProtobufCharacteristic::onWrite(c);
        service.reloadConfig();
    }
};

// wrap our protobuf version with something that forces the service to reload the owner
class OwnerCharacteristic : public ProtobufCharacteristic
{
public:
    OwnerCharacteristic()
        : ProtobufCharacteristic("6ff1d8b6-e2de-41e3-8c0b-8fa384f64eb6", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ, User_fields, &owner)
    {
    }

    void onWrite(BLECharacteristic *c)
    {
        static User o; // if the phone doesn't set ID we are careful to keep ours, we also always keep our macaddr
        if (writeToDest(c, &o))
        {
            if (*o.long_name)
                strcpy(owner.long_name, o.long_name);
            if (*o.short_name)
                strcpy(owner.short_name, o.short_name);
            if (*o.id)
                strcpy(owner.id, o.id);

            service.reloadOwner();
        }
    }
};

static BLECharacteristic
    meshFromRadioCharacteristic("8ba2bcc2-ee02-4a55-a531-c525c5e454d5", BLECharacteristic::PROPERTY_READ),
    meshToRadioCharacteristic("f75c76d2-129e-4dad-a1dd-7866124401e7", BLECharacteristic::PROPERTY_WRITE),
    meshFromNumCharacteristic("ed9da18c-a800-4f66-a670-aa7547e34453", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

static NodeInfoCharacteristic meshNodeInfoCharacteristic;

static ProtobufCharacteristic
    meshMyNodeCharacteristic("ea9f3f82-8dc4-4733-9452-1f6da28892a2", BLECharacteristic::PROPERTY_READ, MyNodeInfo_fields, &myNodeInfo);

static OwnerCharacteristic meshOwnerCharacteristic;
static RadioCharacteristic meshRadioCharacteristic;

/**
 * Tell any bluetooth clients that the number of rx packets has changed
 */
void bluetoothNotifyFromNum(uint32_t newValue)
{
    meshFromNumCharacteristic.setValue(newValue);
    meshFromNumCharacteristic.notify();
}

class BluetoothMeshCallbacks : public BLECharacteristicCallbacks
{
    void onRead(BLECharacteristic *c)
    {
        DEBUG_MSG("Got on read\n");

        if (c == &meshFromRadioCharacteristic)
        {
            MeshPacket *mp = service.getForPhone();

            // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
            // or make empty if the queue is empty
            if (!mp)
            {
                DEBUG_MSG("toPhone queue is empty\n");
                c->setValue((uint8_t *)"", 0);
            }
            else
            {
                DEBUG_MSG("delivering toPhone packet to phone\n");

                static FromRadio fradio;

                // Encapsulate as a ToRadio packet
                memset(&fradio, 0, sizeof(fradio));
                fradio.which_variant = FromRadio_packet_tag;
                fradio.variant.packet = *mp;

                service.releaseToPool(mp); // we just copied the bytes, so don't need this buffer anymore

                size_t numbytes = pb_encode_to_bytes(trBytes, sizeof(trBytes), FromRadio_fields, &fradio);
                c->setValue(trBytes, numbytes);
            }
        }
        else
        {
            // we are uninterested in the other reads
        }
    }

    void onWrite(BLECharacteristic *c)
    {
        // dumpCharacteristic(pCharacteristic);
        DEBUG_MSG("Got on write\n");

        if (c == &meshToRadioCharacteristic)
        {
            service.handleToRadio(c->getValue());
        }
        else
        {
            // we are uninterested in the other writes
        }
    }
};

static BluetoothMeshCallbacks btMeshCb;

/*
MeshBluetoothService UUID 6ba1b218-15a8-461f-9fa8-5dcae273eafd

FIXME - notify vs indication for fromradio output.  Using notify for now, not sure if that is best
FIXME - in the esp32 mesh managment code, occasionally mirror the current net db to flash, so that if we reboot we still have a good guess of users who are out there.
FIXME - make sure this protocol is guaranteed robust and won't drop packets

"According to the BLE specification the notification length can be max ATT_MTU - 3. The 3 bytes subtracted is the 3-byte header(OP-code (operation, 1 byte) and the attribute handle (2 bytes)).
In BLE 4.1 the ATT_MTU is 23 bytes (20 bytes for payload), but in BLE 4.2 the ATT_MTU can be negotiated up to 247 bytes."

MAXPACKET is 256? look into what the lora lib uses. FIXME

Characteristics:
UUID                                 
properties          
description

8ba2bcc2-ee02-4a55-a531-c525c5e454d5                                 
read                
fromradio - contains a newly received packet destined towards the phone (up to MAXPACKET bytes? per packet).
After reading the esp32 will put the next packet in this mailbox.  If the FIFO is empty it will put an empty packet in this
mailbox.

f75c76d2-129e-4dad-a1dd-7866124401e7                             
write               
toradio - write ToRadio protobufs to this charstic to send them (up to MAXPACKET len)

ed9da18c-a800-4f66-a670-aa7547e34453                                  
read|notify|write         
fromnum - the current packet # in the message waiting inside fromradio, if the phone sees this notify it should read messages
until it catches up with this number.
  The phone can write to this register to go backwards up to FIXME packets, to handle the rare case of a fromradio packet was dropped after the esp32 
callback was called, but before it arrives at the phone.  If the phone writes to this register the esp32 will discard older packets and put the next packet >= fromnum in fromradio.
When the esp32 advances fromnum, it will delay doing the notify by 100ms, in the hopes that the notify will never actally need to be sent if the phone is already pulling from fromradio.
  Note: that if the phone ever sees this number decrease, it means the esp32 has rebooted.

meshMyNodeCharacteristic("ea9f3f82-8dc4-4733-9452-1f6da28892a2", BLECharacteristic::PROPERTY_READ)
mynode - read this to access a MyNodeInfo protobuf

meshNodeInfoCharacteristic("d31e02e0-c8ab-4d3f-9cc9-0b8466bdabe8", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ),
nodeinfo - read this to get a series of node infos (ending with a null empty record), write to this to restart the read statemachine that returns all the node infos

meshRadioCharacteristic("b56786c8-839a-44a1-b98e-a1724c4a0262", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ),
radio - read/write this to access a RadioConfig protobuf

meshOwnerCharacteristic("6ff1d8b6-e2de-41e3-8c0b-8fa384f64eb6", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ)
owner - read/write this to access a User protobuf

Re: queue management
Not all messages are kept in the fromradio queue (filtered based on SubPacket):
* only the most recent Position and User messages for a particular node are kept
* all Data SubPackets are kept
* No WantNodeNum / DenyNodeNum messages are kept
A variable keepAllPackets, if set to true will suppress this behavior and instead keep everything for forwarding to the phone (for debugging)

 */
BLEService *createMeshBluetoothService(BLEServer *server)
{
    // Create the BLE Service, we need more than the default of 15 handles
    BLEService *service = server->createService(BLEUUID("6ba1b218-15a8-461f-9fa8-5dcae273eafd"), 25, 0);

    addWithDesc(service, &meshFromRadioCharacteristic, "fromRadio");
    addWithDesc(service, &meshToRadioCharacteristic, "toRadio");
    addWithDesc(service, &meshFromNumCharacteristic, "fromNum");

    meshFromRadioCharacteristic.setCallbacks(&btMeshCb);
    meshToRadioCharacteristic.setCallbacks(&btMeshCb);
    meshFromNumCharacteristic.setCallbacks(&btMeshCb);

    addWithDesc(service, &meshMyNodeCharacteristic, "myNode");
    addWithDesc(service, &meshRadioCharacteristic, "radio");
    addWithDesc(service, &meshOwnerCharacteristic, "owner");
    addWithDesc(service, &meshNodeInfoCharacteristic, "nodeinfo");

    meshFromNumCharacteristic.addDescriptor(new BLE2902()); // Needed so clients can request notification

    service->start();
    server->getAdvertising()->addServiceUUID(service->getUUID());

    DEBUG_MSG("*** Mesh service:\n");
    service->dump();

    return service;
}
