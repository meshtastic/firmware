#pragma once

#include "CustomRF95.h"
#include "MemoryPool.h"
#include "MeshTypes.h"
#include "Observer.h"
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
    // Kinda ugly way of selecting different radio implementations, but soon this MeshRadio class will be going away
    // entirely.  At that point we can make things pretty.
#ifdef RF95_IRQ_GPIO
    CustomRF95
        radioIf; // the raw radio interface - for now I'm leaving public - because this class is shrinking to be almost nothing
#else
    SimRadio radioIf;
#endif

    /** pool is the pool we will alloc our rx packets from
     * rxDest is where we will send any rx packets, it becomes receivers responsibility to return packet to the pool
     */
    MeshRadio(MemoryPool<MeshPacket> &pool, PointerQueue<MeshPacket> &rxDest);

    bool init();

    /// Do loop callback operations (we currently FIXME poll the receive mailbox here)
    /// for received packets it will call the rx handler
    void loop();

  private:
    /// Used for the tx timer watchdog, to check for bugs in our transmit code, msec of last time we did a send
    uint32_t lastTxStart = 0;

    CallbackObserver<MeshRadio, void *> configChangedObserver =
        CallbackObserver<MeshRadio, void *>(this, &MeshRadio::reloadConfig);

    CallbackObserver<MeshRadio, void *> preflightSleepObserver =
        CallbackObserver<MeshRadio, void *>(this, &MeshRadio::preflightSleepCb);

    CallbackObserver<MeshRadio, void *> notifyDeepSleepObserver =
        CallbackObserver<MeshRadio, void *>(this, &MeshRadio::notifyDeepSleepDb);

    CallbackObserver<MeshRadio, MeshPacket *> sendPacketObserver; /*  =
        CallbackObserver<MeshRadio, MeshPacket *>(this, &MeshRadio::send); */

    /// Send a packet (possibly by enquing in a private fifo).  This routine will
    /// later free() the packet to pool.  This routine is not allowed to stall because it is called from
    /// bluetooth comms code.  If the txmit queue is empty it might return an error.
    ///
    /// Returns 1 for success or 0 for failure (and if we fail it is the _callers_ responsibility to free the packet)
    int send(MeshPacket *p);

    /// The radioConfig object just changed, call this to force the hw to change to the new settings
    int reloadConfig(void *unused = NULL);

    /// Return 0 if sleep is okay
    int preflightSleepCb(void *unused = NULL) { return radioIf.canSleep() ? 0 : 1; }

    int notifyDeepSleepDb(void *unused = NULL)
    {
        radioIf.sleep();
        return 0;
    }
};
