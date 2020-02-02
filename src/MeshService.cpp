
#include <Arduino.h>
#include <assert.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "mesh.pb.h"
#include "MeshService.h"

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

MeshService service;

#define MAX_PACKETS 32    // max number of packets which can be in flight (either queued from reception or queued for sending)
#define MAX_RX_TOPHONE 16 // max number of packets which can be waiting for delivery to android

MeshService::MeshService() : packetPool(MAX_PACKETS), toPhoneQueue(MAX_RX_TOPHONE), radio(packetPool, toPhoneQueue)
{
}

void MeshService::init()
{
    if (!radio.init())
        Serial.println("radio init failed");
}

/// Do idle processing (mostly processing messages which have been queued from the radio)
void MeshService::loop()
{
    radio.loop(); // FIXME, possibly move radio interaction to own thread
}

/// Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
void MeshService::handleToRadio(std::string s)
{
    static ToRadio r; // this is a static scratch object, any data must be copied elsewhere before returning

    pb_istream_t stream = pb_istream_from_buffer((const uint8_t *)s.c_str(), s.length());
    if (!pb_decode(&stream, ToRadio_fields, &r))
    {
        Serial.printf("Error: can't decode ToRadio %s\n", PB_GET_ERROR(&stream));
    }
    else
    {
        switch (r.which_variant)
        {
        case ToRadio_packet_tag:
            sendToMesh(r.variant.packet);
            break;

        default:
            Serial.println("Error: unexpected ToRadio variant");
            break;
        }
    }
}

/// Send a packet into the mesh - note p is read only and should be copied into a pool based MeshPacket before
/// sending.
void MeshService::sendToMesh(const MeshPacket &pIn)
{
    MeshPacket *pOut = packetPool.allocCopy(pIn);
    assert(pOut); // FIXME

    assert(radio.send(pOut) == pdTRUE);
}
