#pragma once

#include <Arduino.h>
#include <assert.h>

#include "MemoryPool.h"
#include "MeshRadio.h"
#include "Observer.h"
#include "PointerQueue.h"
#include "mesh.pb.h"

/**
 * Top level app for this service.  keeps the mesh, the radio config and the queue of received packets.
 *
 */
class MeshService : private Observer
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
    void reloadConfig();

    /// The owner User record just got updated, update our node DB and broadcast the info into the mesh
    void reloadOwner() { sendOurOwner(); }

    /// Allocate and return a meshpacket which defaults as send to broadcast from the current node.
    MeshPacket *allocForSending();

    /// Called when the user wakes up our GUI, normally sends our latest location to the mesh (if we have it), otherwise at least
    /// sends our owner
    void sendNetworkPing(NodeNum dest, bool wantReplies = false);

    /// Send our owner info to a particular node
    void sendOurOwner(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  private:
    /// Broadcasts our last known position
    void sendOurPosition(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

    /// Send a packet into the mesh - note p must have been allocated from packetPool.  We will return it to that pool after
    /// sending. This is the ONLY function you should use for sending messages into the mesh, because it also updates the nodedb
    /// cache
    void sendToMesh(MeshPacket *p);

    /// Called when our gps position has changed - updates nodedb and sends Location message out into the mesh
    void onGPSChanged();

    virtual void onNotify(Observable *o);

    /// handle all the packets that just arrived from the mesh radio
    void handleFromRadio();

    /// Handle a packet that just arrived from the radio
    void handleFromRadio(MeshPacket *p);

    /// handle a user packet that just arrived on the radio, return NULL if we should not process this packet at all
    MeshPacket *handleFromRadioUser(MeshPacket *mp);

    /// look at inbound packets and if they contain a position with time, possibly set our clock
    void handleIncomingPosition(MeshPacket *mp);
};

extern MeshService service;
