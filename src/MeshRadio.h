#pragma once

#include <RH_RF95.h>
#include <RHMesh.h>
#include "MemoryPool.h"
#include "mesh.pb.h"
#include "PointerQueue.h"

#define NODENUM_BROADCAST 255
#define ERRNO_OK 0
#define ERRNO_UNKNOWN 32 // pick something that doesn't conflict with RH_ROUTER_ERROR_UNABLE_TO_DELIVER

typedef int ErrorCode;
typedef uint8_t NodeNum;

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
    void sleep() { rf95.sleep(); }

    /// Send a packet (possibly by enquing in a private fifo).  This routine will
    /// later free() the packet to pool.  This routine is not allowed to stall because it is called from
    /// bluetooth comms code.  If the txmit queue is empty it might return an error
    ErrorCode send(MeshPacket *p);

    /// Do loop callback operations (we currently FIXME poll the receive mailbox here)
    /// for received packets it will call the rx handler
    void loop();

private:
    RH_RF95 rf95; // the raw radio interface
    RHMesh manager;
    // MeshRXHandler rxHandler;

    MemoryPool<MeshPacket> &pool;
    PointerQueue<MeshPacket> &rxDest;
    PointerQueue<MeshPacket> txQueue;

    /// low level send, might block for mutiple seconds
    ErrorCode sendTo(NodeNum dest, const uint8_t *buf, size_t len);
};

