#pragma once

#include <RH_RF95.h>
#include <RHMesh.h>
#include "MemoryPool.h"
#include "mesh.pb.h"
#include "PointerQueue.h"
#include "MeshTypes.h"
#include "configuration.h"

// US channel settings
#define CH0_US          903.08f // MHz
#define CH_SPACING_US   2.16f // MHz
#define NUM_CHANNELS_US    13

// FIXME add defs for other regions and use them here
#define CH0 CH0_US
#define CH_SPACING CH_SPACING_US
#define NUM_CHANNELS NUM_CHANNELS_US


#define MAX_TX_QUEUE 8 // max number of packets which can be waiting for transmission


/**
 * A raw low level interface to our mesh.  Only understands nodenums and bytes (not protobufs or node ids)
 */
class MeshRadio {
public:
    /** pool is the pool we will alloc our rx packets from
     * rxDest is where we will send any rx packets, it becomes receivers responsibility to return packet to the pool
     */
    MeshRadio(MemoryPool<MeshPacket> &pool, PointerQueue<MeshPacket> &rxDest);

    bool init();

    /// Prepare the radio to enter sleep mode, where it should draw only 0.2 uA
    void sleep();

    /// Send a packet (possibly by enquing in a private fifo).  This routine will
    /// later free() the packet to pool.  This routine is not allowed to stall because it is called from
    /// bluetooth comms code.  If the txmit queue is empty it might return an error
    ErrorCode send(MeshPacket *p);

    /// Do loop callback operations (we currently FIXME poll the receive mailbox here)
    /// for received packets it will call the rx handler
    void loop();

    /// The radioConfig object just changed, call this to force the hw to change to the new settings
    void reloadConfig();

private:
    RH_RF95 rf95; // the raw radio interface

    // RHDatagram manager;
    // RHReliableDatagram manager; // don't use mesh yet
    RHMesh manager;
    // MeshRXHandler rxHandler;

    MemoryPool<MeshPacket> &pool;
    PointerQueue<MeshPacket> &rxDest;
    PointerQueue<MeshPacket> txQueue;

    /// low level send, might block for mutiple seconds
    ErrorCode sendTo(NodeNum dest, const uint8_t *buf, size_t len);

    /// enqueue a received packet in rxDest
    void handleReceive(MeshPacket *p);
};


