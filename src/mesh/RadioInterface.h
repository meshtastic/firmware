#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "WorkerThread.h"
#include "mesh.pb.h"

#define MAX_TX_QUEUE 16 // max number of packets which can be waiting for transmission

#define MAX_RHPACKETLEN 256

/**
 * This structure has to exactly match the wire layout when sent over the radio link.  Used to keep compatibility
 * wtih the old radiohead implementation.
 */
typedef struct {
    uint8_t to, from, id, flags;
} PacketHeader;

typedef enum {
    Bw125Cr45Sf128 = 0, ///< Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Default medium range
    Bw500Cr45Sf128,     ///< Bw = 500 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Fast+short range
    Bw31_25Cr48Sf512,   ///< Bw = 31.25 kHz, Cr = 4/8, Sf = 512chips/symbol, CRC on. Slow+long range
    Bw125Cr48Sf4096,    ///< Bw = 125 kHz, Cr = 4/8, Sf = 4096chips/symbol, CRC on. Slow+long range
} ModemConfigChoice;

/**
 * Basic operations all radio chipsets must implement.
 *
 * This defines the SOLE API for talking to radios (because soon we will have alternate radio implementations)
 */
class RadioInterface : protected NotifiedWorkerThread
{
    friend class MeshRadio; // for debugging we let that class touch pool
    PointerQueue<MeshPacket> *rxDest = NULL;

  protected:
    MeshPacket *sendingPacket = NULL; // The packet we are currently sending
    PointerQueue<MeshPacket> txQueue;
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
    float freq = 915.0; // FIXME, init all these params from user setings
    int8_t power = 17;
    ModemConfigChoice modemConfig;

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
    /***
     * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of bytes to send (including the
     * PacketHeader & payload).
     *
     * Used as the first step of
     */
    size_t beginSending(MeshPacket *p);

    virtual void loop() {} // Idle processing
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
};
