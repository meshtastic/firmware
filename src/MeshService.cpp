
#include <Arduino.h>
#include <assert.h>

#include "mesh-pb-constants.h"
#include "MeshService.h"
#include "MeshBluetoothService.h"
#include "NodeDB.h"
#include "GPS.h"

/*
receivedPacketQueue - this is a queue of messages we've received from the mesh, which we are keeping to deliver to the phone.
It is implemented with a FreeRTos queue (wrapped with a little RTQueue class) of pointers to MeshPacket protobufs (which were alloced with new).
After a packet ptr is removed from the queue and processed it should be deleted.  (eventually we should move sent packets into a 'sentToPhone' queue
of packets we can delete just as soon as we are sure the phone has acked those packets - when the phone writes to FromNum)

mesh - an instance of Mesh class.  Which manages the interface to the mesh radio library, reception of packets from other nodes, arbitrating to select
a node number and keeping the current nodedb.

*/

MeshService service;

#define MAX_PACKETS 32 // max number of packets which can be in flight (either queued from reception or queued for sending)

#define MAX_RX_FROMRADIO 4 // max number of packets destined to our queue, we dispatch packets quickly so it doesn't need to be big

MeshService::MeshService()
    : packetPool(MAX_PACKETS),
      toPhoneQueue(MAX_RX_TOPHONE),
      fromRadioQueue(MAX_RX_FROMRADIO),
      fromNum(0),
      radio(packetPool, fromRadioQueue)
{
    // assert(MAX_RX_TOPHONE == 32); // FIXME, delete this, just checking my clever macro
}

void MeshService::init()
{
    nodeDB.init();

    if (!radio.init())
        DEBUG_MSG("radio init failed\n");

    gps.addObserver(this);
}

/// Do idle processing (mostly processing messages which have been queued from the radio)
void MeshService::loop()
{
    radio.loop(); // FIXME, possibly move radio interaction to own thread

    MeshPacket *mp;
    uint32_t oldFromNum = fromNum;
    while ((mp = fromRadioQueue.dequeuePtr(0)) != NULL)
    {
        nodeDB.updateFrom(*mp); // update our DB state based off sniffing every RX packet from the radio

        fromNum++;
        assert(toPhoneQueue.enqueue(mp, 0) == pdTRUE); // FIXME, instead of failing for full queue, delete the oldest mssages
    }
    if (oldFromNum != fromNum) // We don't want to generate extra notifies for multiple new packets
        bluetoothNotifyFromNum(fromNum);
}

/// Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
void MeshService::handleToRadio(std::string s)
{
    static ToRadio r; // this is a static scratch object, any data must be copied elsewhere before returning

    if (pb_decode_from_bytes((const uint8_t *)s.c_str(), s.length(), ToRadio_fields, &r))
    {
        switch (r.which_variant)
        {
        case ToRadio_packet_tag:
            sendToMesh(packetPool.allocCopy(r.variant.packet));
            break;

        default:
            DEBUG_MSG("Error: unexpected ToRadio variant\n");
            break;
        }
    }
}

void MeshService::sendToMesh(MeshPacket *p)
{
    nodeDB.updateFrom(*p);
    assert(radio.send(p) == pdTRUE);
}

void MeshService::onGPSChanged()
{
    MeshPacket *p = packetPool.allocZeroed();

    p->has_payload = true;
    p->from = nodeDB.getNodeNum();
    p->to = NODENUM_BROADCAST;
    p->payload.which_variant = SubPacket_position_tag;
    Position &pos = p->payload.variant.position;
    if (gps.altitude.isValid())
        pos.altitude = gps.altitude.value();
    pos.latitude = gps.location.lat();
    pos.longitude = gps.location.lng();

    sendToMesh(p);
}

void MeshService::onNotify(Observable *o)
{
    DEBUG_MSG("got gps notify\n");
    onGPSChanged();
}