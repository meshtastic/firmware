#pragma once

#include "CustomRF95.h"
#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "configuration.h"
#include "mesh.pb.h"
#include <RHMesh.h>

// US channel settings
#define CH0_US 903.08f      // MHz
#define CH_SPACING_US 2.16f // MHz
#define NUM_CHANNELS_US 13

// EU433 channel settings
#define CH0_EU433 433.175f    // MHz
#define CH_SPACING_EU433 0.2f // MHz
#define NUM_CHANNELS_EU433 8

// EU865 channel settings
#define CH0_EU865 865.2f      // MHz
#define CH_SPACING_EU865 0.3f // MHz
#define NUM_CHANNELS_EU865 10

// CN channel settings
#define CH0_CN 470.0f      // MHz
#define CH_SPACING_CN 2.0f // MHz FIXME, this is just a guess for 470-510
#define NUM_CHANNELS_CN 20

// JP channel settings
#define CH0_JP 920.0f      // MHz
#define CH_SPACING_JP 0.5f // MHz FIXME, this is just a guess for 920-925
#define NUM_CHANNELS_JP 10

// FIXME add defs for other regions and use them here
#ifdef HW_VERSION_US
#define CH0 CH0_US
#define CH_SPACING CH_SPACING_US
#define NUM_CHANNELS NUM_CHANNELS_US
#elif defined(HW_VERSION_EU433)
#define CH0 CH0_EU433
#define CH_SPACING CH_SPACING_EU433
#define NUM_CHANNELS NUM_CHANNELS_EU433
#elif defined(HW_VERSION_EU865)
#define CH0 CH0_EU865
#define CH_SPACING CH_SPACING_EU865
#define NUM_CHANNELS NUM_CHANNELS_EU865
#elif defined(HW_VERSION_CN)
#define CH0 CH0_CN
#define CH_SPACING CH_SPACING_CN
#define NUM_CHANNELS NUM_CHANNELS_CN
#elif defined(HW_VERSION_JP)
#define CH0 CH0_JP
#define CH_SPACING CH_SPACING_JP
#define NUM_CHANNELS NUM_CHANNELS_JP
#else
#error "HW_VERSION not set"
#endif

/**
 * A raw low level interface to our mesh.  Only understands nodenums and bytes (not protobufs or node ids)
 */
class MeshRadio
{
  public:
    CustomRF95
        rf95; // the raw radio interface - for now I'm leaving public - because this class is shrinking to be almost nothing

    /** pool is the pool we will alloc our rx packets from
     * rxDest is where we will send any rx packets, it becomes receivers responsibility to return packet to the pool
     */
    MeshRadio(MemoryPool<MeshPacket> &pool, PointerQueue<MeshPacket> &rxDest);

    bool init();

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
    // RHDatagram manager;
    // RHReliableDatagram manager; // don't use mesh yet
    RHMesh manager;
    // MeshRXHandler rxHandler;

    /// low level send, might block for mutiple seconds
    ErrorCode sendTo(NodeNum dest, const uint8_t *buf, size_t len);

    /// enqueue a received packet in rxDest
    void handleReceive(MeshPacket *p);
};
