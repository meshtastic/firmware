#pragma once

#include <Arduino.h>
#include <assert.h>

#include "mesh.pb.h"
#include "MeshRadio.h"
#include "PointerQueue.h"
#include "MemoryPool.h"

/**
 * Top level app for this service.  keeps the mesh, the radio config and the queue of received packets.
 * 
 */
class MeshService
{
    MemoryPool<MeshPacket> packetPool;

    /// received packets waiting for the phone to process them
    /// FIXME, change to a DropOldestQueue and keep a count of the number of dropped packets to ensure
    /// we never hang because android hasn't been there in a while
    PointerQueue<MeshPacket> toPhoneQueue;

    /// Packets which have just arrived from the radio, ready to be processed by this service and possibly
    /// forwarded to the phone. Note: not using yet - seeing if I can just handle everything asap in handleFromRadio
    // PointerQueue<MeshPacket> fromRadioQueue;

public:

    MeshRadio radio;

    MeshService();

    void init();
    
    /// Do idle processing (mostly processing messages which have been queued from the radio)
    void loop();

#if 0
    /**
     * handle an incoming MeshPacket from the radio, update DB state and queue it for the phone
     */
    void handleFromRadio(NodeNum from, NodeNum to, const uint8_t *buf, size_t len) {
        MeshPacket *p = packetPool.allocZeroed();
        assert(p);

        pb_istream_t stream = pb_istream_from_buffer(buf, len);
        if (!pb_decode(&stream, MeshPacket_fields, p) || !p->has_payload)
        {
            Serial.printf("Error: can't decode MeshPacket %s\n", PB_GET_ERROR(&stream));
        }
        else
        {
            // FIXME - update DB state based on payload and show recevied texts

            toPhoneQueue.enqueue(p);
        }
    }
#endif

    /// Given a ToRadio buffer (from bluetooth) parse it and properly handle it (setup radio, owner or send packet into the mesh)
    void handleToRadio(std::string s);

private:

    /// Send a packet into the mesh - note p is read only and should be copied into a pool based MeshPacket before
    /// sending.
    void sendToMesh(const MeshPacket &p);

};

extern MeshService service;

