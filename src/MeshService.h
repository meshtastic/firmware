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
    /// FIXME - save this to flash on deep sleep
    PointerQueue<MeshPacket> toPhoneQueue;

    /// Packets which have just arrived from the radio, ready to be processed by this service and possibly
    /// forwarded to the phone.
    PointerQueue<MeshPacket> fromRadioQueue;

    /// The current nonce for the newest packet which has been queued for the phone
    uint32_t fromNum;

public:

    MeshRadio radio;

    MeshService();

    void init();
    
    /// Do idle processing (mostly processing messages which have been queued from the radio)
    void loop();

    /// Return the next packet destined to the phone.  FIXME, somehow use fromNum to allow the phone to retry the
    /// last few packets if needs to.
    MeshPacket *getForPhone() { return toPhoneQueue.dequeuePtr(0); }

    /// Allows the bluetooth handler to free packets after they have been sent
    void releaseToPool(MeshPacket *p) { packetPool.release(p); }

    /// Given a ToRadio buffer (from bluetooth) parse it and properly handle it (setup radio, owner or send packet into the mesh)
    void handleToRadio(std::string s);

    /// The radioConfig object just changed, call this to force the hw to change to the new settings
    void reloadConfig() { radio.reloadConfig(); }

    /// The owner User record just got updated, update our node DB and broadcast the info into the mesh
    void reloadOwner() { DEBUG_MSG("FIXME implement reloadOwner\n"); }
    
private:

    /// Send a packet into the mesh - note p is read only and should be copied into a pool based MeshPacket before
    /// sending.
    void sendToMesh(const MeshPacket &p);

};

extern MeshService service;

