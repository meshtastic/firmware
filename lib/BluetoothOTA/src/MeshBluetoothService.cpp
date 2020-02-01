#include "BluetoothUtil.h"
#include "MeshBluetoothService.h"
#include <esp_gatt_defs.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <assert.h>


/*
receivedPacketQueue - this is a queue of messages we've received from the mesh, which we are keeping to deliver to the phone.
It is implemented with a FreeRTos queue (wrapped with a little RTQueue class) of pointers to MeshPacket protobufs (which were alloced with new).
After a packet ptr is removed from the queue and processed it should be deleted.  (eventually we should move sent packets into a 'sentToPhone' queue
of packets we can delete just as soon as we are sure the phone has acked those packets - when the phone writes to FromNum)

mesh - an instance of Mesh class.  Which manages the interface to the mesh radio library, reception of packets from other nodes, arbitrating to select
a node number and keeping the current nodedb.


typedef in32_t NodeNum;

class NodeInfo {
    position;
    last_seen
    user
};

class NodeDB {
    NodeNum provisionalNodeNum; // if we are trying to find a node num this is our current attempt

    NodeNum ourNodeNum; // -1 if not yet found

    HashMap<NodeNum, NodeInfo> nodes;
public:
    /// don't do mesh based algoritm for node id assignment (initially) - instead just store in flash - possibly even in the initial alpha release do this hack

    /// if returns false, that means our node should send a DenyNodeNum response.  If true, we think the number is okay for use
    // bool handleWantNodeNum(NodeNum n);

    void handleDenyNodeNum(NodeNum FIXME read mesh proto docs, perhaps picking a random node num is not a great idea
    and instead we should use a special 'im unconfigured node number' and include our desired node number in the wantnum message.  the
    unconfigured node num would only be used while initially joining the mesh so low odds of conflicting (especially if we randomly select
    from a small number of nodenums which can be used temporarily for this operation).  figure out what the lower level
    mesh sw does if it does conflict?  would it be better for people who are replying with denynode num to just broadcast their denial?)
};





*/

/**
 * Top level app for this service.  keeps the mesh, the radio config and the queue of received packets.
 * 
 */
class MeshService {
public:
};


static BLECharacteristic meshFromRadioCharacteristic("8ba2bcc2-ee02-4a55-a531-c525c5e454d5", BLECharacteristic::PROPERTY_READ);
static BLECharacteristic meshToRadioCharacteristic("f75c76d2-129e-4dad-a1dd-7866124401e7", BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic meshFromNumCharacteristic("ed9da18c-a800-4f66-a670-aa7547e34453", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);


/// Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
static void handleToRadio(std::string s) {

}

class BluetoothMeshCallbacks : public BLECharacteristicCallbacks
{
    void onRead(BLECharacteristic *c) {
        Serial.println("Got on read");

        if(c == &meshFromRadioCharacteristic) {
            // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
            // or make empty if the queue is empty
            // c->setValue(byteptr, len);
        }
    }

    void onWrite(BLECharacteristic *c)
    {
        // dumpCharacteristic(pCharacteristic);
        Serial.println("Got on write");

        if(c == &meshToRadioCharacteristic) {
            handleToRadio(c->getValue());
        }
        else {
            assert(0); // Not yet implemented
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

Re: queue management
Not all messages are kept in the fromradio queue (filtered based on SubPacket):
* only the most recent Position and User messages for a particular node are kept
* all Data SubPackets are kept
* No WantNodeNum / DenyNodeNum messages are kept
A variable keepAllPackets, if set to true will suppress this behavior and instead keep everything for forwarding to the phone (for debugging)

 */
BLEService *createMeshBluetoothService(BLEServer* server) {
    // Create the BLE Service
    BLEService *service = server->createService("6ba1b218-15a8-461f-9fa8-5dcae273eafd");

    addWithDesc(service, &meshFromRadioCharacteristic, "fromRadio");
    addWithDesc(service, &meshToRadioCharacteristic, "toRadio");
    addWithDesc(service, &meshFromNumCharacteristic, "fromNum");

    meshFromRadioCharacteristic.setCallbacks(&btMeshCb);
    meshToRadioCharacteristic.setCallbacks(&btMeshCb);
    meshFromNumCharacteristic.setCallbacks(&btMeshCb);
    
    meshFromNumCharacteristic.addDescriptor(new BLE2902()); // Needed so clients can request notification

    return service;
}

