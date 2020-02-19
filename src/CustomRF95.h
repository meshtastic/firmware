#pragma once

#include <RH_RF95.h>
#include <RHMesh.h>
#include "MemoryPool.h"
#include "mesh.pb.h"
#include "PointerQueue.h"
#include "MeshTypes.h"

/**
 * A version of the RF95 driver which is smart enough to manage packets via queues (no polling or blocking in user threads!)
 */
class CustomRF95 : public RH_RF95
{
    MemoryPool<MeshPacket> &pool;
    PointerQueue<MeshPacket> &rxDest;
    PointerQueue<MeshPacket> txQueue;

    MeshPacket *sendingPacket; // The packet we are currently sending
public:
    /** pool is the pool we will alloc our rx packets from
     * rxDest is where we will send any rx packets, it becomes receivers responsibility to return packet to the pool
     */
    CustomRF95(MemoryPool<MeshPacket> &pool, PointerQueue<MeshPacket> &rxDest);

    /// Send a packet (possibly by enquing in a private fifo).  This routine will
    /// later free() the packet to pool.  This routine is not allowed to stall because it is called from
    /// bluetooth comms code.  If the txmit queue is empty it might return an error
    ErrorCode send(MeshPacket *p);

    bool init();

protected:
    // After doing standard behavior, check to see if a new packet arrived or one was sent and start a new send or receive as necessary
    virtual void handleInterrupt();

private:
    /// Send a new packet - this low level call can be called from either ISR or userspace
    void startSend(MeshPacket *txp);

    /// Return true if a higher pri task has woken
    bool handleIdleISR();
};