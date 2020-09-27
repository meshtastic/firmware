#pragma once

#include "../concurrency/NotifiedWorkerThread.h"
#include "MemoryPool.h"
#include "MeshTypes.h"
#include "Observer.h"
#include "PointerQueue.h"
#include "mesh.pb.h"

#define MAX_TX_QUEUE 16 // max number of packets which can be waiting for transmission

#define MAX_RHPACKETLEN 256

#define PACKET_FLAGS_HOP_MASK 0x07
#define PACKET_FLAGS_WANT_ACK_MASK 0x08

/**
 * This structure has to exactly match the wire layout when sent over the radio link.  Used to keep compatibility
 * wtih the old radiohead implementation.
 */
typedef struct {
    NodeNum to, from; // can be 1 byte or four bytes

    PacketId id; // can be 1 byte or 4 bytes

    /**
     * Usage of flags:
     *
     * The bottom three bits of flags are use to store hop_limit when sent over the wire.
     **/
    uint8_t flags;
} PacketHeader;

/**
 * Basic operations all radio chipsets must implement.
 *
 * This defines the SOLE API for talking to radios (because soon we will have alternate radio implementations)
 */
class RadioInterface : protected concurrency::NotifiedWorkerThread
{
    friend class MeshRadio; // for debugging we let that class touch pool
    PointerQueue<MeshPacket> *rxDest = NULL;

    CallbackObserver<RadioInterface, void *> configChangedObserver =
        CallbackObserver<RadioInterface, void *>(this, &RadioInterface::reloadConfig);

    CallbackObserver<RadioInterface, void *> preflightSleepObserver =
        CallbackObserver<RadioInterface, void *>(this, &RadioInterface::preflightSleepCb);

    CallbackObserver<RadioInterface, void *> notifyDeepSleepObserver =
        CallbackObserver<RadioInterface, void *>(this, &RadioInterface::notifyDeepSleepDb);

  protected:
    MeshPacket *sendingPacket = NULL; // The packet we are currently sending
    uint32_t lastTxStart = 0L;

    /**
     * A temporary buffer used for sending/receving packets, sized to hold the biggest buffer we might need
     * */
    uint8_t radiobuf[MAX_RHPACKETLEN];

    /**
     * Enqueue a received packet for the registered receiver
     */
    void deliverToReceiver(MeshPacket *p);

  public:
    float freq = 915.0;

    /** pool is the pool we will alloc our rx packets from
     * rxDest is where we will send any rx packets, it becomes receivers responsibility to return packet to the pool
     */
    RadioInterface();

    /**
     * Set where to deliver received packets.  This method should only be used by the Router class
     */
    void setReceiver(PointerQueue<MeshPacket> *_rxDest) { rxDest = _rxDest; }

    /**
     * Return true if we think the board can go to sleep (i.e. our tx queue is empty, we are not sending or receiving)
     *
     * This method must be used before putting the CPU into deep or light sleep.
     */
    virtual bool canSleep() { return true; }

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep() { return true; }

    /**
     * Send a packet (possibly by enquing in a private fifo).  This routine will
     * later free() the packet to pool.  This routine is not allowed to stall.
     * If the txmit queue is full it might return an error
     */
    virtual ErrorCode send(MeshPacket *p) = 0;

    // methods from radiohead

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init();

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure() = 0;

  protected:
    int8_t power = 17; // Set by applyModemConfig()

    /***
     * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of bytes to send (including the
     * PacketHeader & payload).
     *
     * Used as the first step of
     */
    size_t beginSending(MeshPacket *p);

    virtual void loop() {} // Idle processing

    /**
     * Some regulatory regions limit xmit power.
     * This function should be called by subclasses after setting their desired power.  It might lower it
     */
    void limitPower();

    /**
     * Convert our modemConfig enum into wf, sf, etc...
     *
     * These paramaters will be pull from the channelSettings global
     */
    virtual void applyModemConfig();

  private:
    /// Return 0 if sleep is okay
    int preflightSleepCb(void *unused = NULL) { return canSleep() ? 0 : 1; }

    int notifyDeepSleepDb(void *unused = NULL)
    {
        sleep();
        return 0;
    }

    int reloadConfig(void *unused)
    {
        reconfigure();
        return 0;
    }
};

class SimRadio : public RadioInterface
{
  public:
    virtual ErrorCode send(MeshPacket *p);

    // methods from radiohead

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init() { return true; }

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure() { return true; }
};

/// Debug printing for packets
void printPacket(const char *prefix, const MeshPacket *p);