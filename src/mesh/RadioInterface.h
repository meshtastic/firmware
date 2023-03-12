#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "Observer.h"
#include "PointerQueue.h"
#include "airtime.h"

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

    /** The channel hash - used as a hint for the decoder to limit which channels we consider */
    uint8_t channel;
} PacketHeader;

/**
 * Basic operations all radio chipsets must implement.
 *
 * This defines the SOLE API for talking to radios (because soon we will have alternate radio implementations)
 */
class RadioInterface
{
    friend class MeshRadio; // for debugging we let that class touch pool

    CallbackObserver<RadioInterface, void *> configChangedObserver =
        CallbackObserver<RadioInterface, void *>(this, &RadioInterface::reloadConfig);

    CallbackObserver<RadioInterface, void *> preflightSleepObserver =
        CallbackObserver<RadioInterface, void *>(this, &RadioInterface::preflightSleepCb);

    CallbackObserver<RadioInterface, void *> notifyDeepSleepObserver =
        CallbackObserver<RadioInterface, void *>(this, &RadioInterface::notifyDeepSleepCb);

  protected:
    bool disabled = false;

    float bw = 125;
    uint8_t sf = 9;
    uint8_t cr = 7;
    /** Slottime is the minimum time to wait, consisting of:
      - CAD duration (maximum of SX126x and SX127x);
      - roundtrip air propagation time (assuming max. 30km between nodes);
      - Tx/Rx turnaround time (maximum of SX126x and SX127x);
      - MAC processing time (measured on T-beam) */
    uint32_t slotTimeMsec = 8.5 * pow(2, sf) / bw + 0.2 + 0.4 + 7;
    uint16_t preambleLength = 16;      // 8 is default, but we use longer to increase the amount of sleep time when receiving
    uint32_t preambleTimeMsec = 165;   // calculated on startup, this is the default for LongFast
    uint32_t maxPacketTimeMsec = 3246; // calculated on startup, this is the default for LongFast
    const uint32_t PROCESSING_TIME_MSEC =
        4500;                // time to construct, process and construct a packet again (empirically determined)
    const uint8_t CWmin = 2; // minimum CWsize
    const uint8_t CWmax = 8; // maximum CWsize

    meshtastic_MeshPacket *sendingPacket = NULL; // The packet we are currently sending
    uint32_t lastTxStart = 0L;

    /**
     * A temporary buffer used for sending/receving packets, sized to hold the biggest buffer we might need
     * */
    uint8_t radiobuf[MAX_RHPACKETLEN];

    /**
     * Enqueue a received packet for the registered receiver
     */
    void deliverToReceiver(meshtastic_MeshPacket *p);

  public:
    /** pool is the pool we will alloc our rx packets from
     */
    RadioInterface();

    virtual ~RadioInterface() {}

    /**
     * Return true if we think the board can go to sleep (i.e. our tx queue is empty, we are not sending or receiving)
     *
     * This method must be used before putting the CPU into deep or light sleep.
     */
    virtual bool canSleep() { return true; }

    virtual bool wideLora() { return false; }

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep() { return true; }

    /// Disable this interface (while disabled, no packets can be sent or received)
    void disable()
    {
        disabled = true;
        sleep();
    }

    /**
     * Send a packet (possibly by enquing in a private fifo).  This routine will
     * later free() the packet to pool.  This routine is not allowed to stall.
     * If the txmit queue is full it might return an error
     */
    virtual ErrorCode send(meshtastic_MeshPacket *p) = 0;

    /** Return TX queue status */
    virtual meshtastic_QueueStatus getQueueStatus()
    {
        meshtastic_QueueStatus qs;
        qs.res = qs.mesh_packet_id = qs.free = qs.maxlen = 0;
        return qs;
    }

    /** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
    virtual bool cancelSending(NodeNum from, PacketId id) { return false; }

    // methods from radiohead

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init();

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure();

    /** The delay to use for retransmitting dropped packets */
    uint32_t getRetransmissionMsec(const meshtastic_MeshPacket *p);

    /** The delay to use when we want to send something */
    uint32_t getTxDelayMsec();

    /** The delay to use when we want to flood a message. Use a weighted scale based on SNR */
    uint32_t getTxDelayMsecWeighted(float snr);

    /**
     * Calculate airtime per
     * https://www.rs-online.com/designspark/rel-assets/ds-assets/uploads/knowledge-items/application-notes-for-the-internet-of-things/LoRa%20Design%20Guide.pdf
     * section 4
     *
     * @return num msecs for the packet
     */
    uint32_t getPacketTime(const meshtastic_MeshPacket *p);
    uint32_t getPacketTime(uint32_t totalPacketLen);

    /**
     * Get the channel we saved.
     */
    uint32_t getChannelNum();

    /**
     * Get the frequency we saved.
     */
    virtual float getFreq();

    /// Some boards (1st gen Pinetab Lora module) have broken IRQ wires, so we need to poll via i2c registers
    virtual bool isIRQPending() { return false; }

  protected:
    int8_t power = 17; // Set by applyModemConfig()

    float savedFreq;
    uint32_t savedChannelNum;

    /***
     * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of bytes to send (including the
     * PacketHeader & payload).
     *
     * Used as the first step of
     */
    size_t beginSending(meshtastic_MeshPacket *p);

    /**
     * Some regulatory regions limit xmit power.
     * This function should be called by subclasses after setting their desired power.  It might lower it
     */
    void limitPower();

    /**
     * Save the frequency we selected for later reuse.
     */
    virtual void saveFreq(float savedFreq);

    /**
     * Save the chanel we selected for later reuse.
     */
    virtual void saveChannelNum(uint32_t savedChannelNum);

  private:
    /**
     * Convert our modemConfig enum into wf, sf, etc...
     *
     * These paramaters will be pull from the channelSettings global
     */
    void applyModemConfig();

    /// Return 0 if sleep is okay
    int preflightSleepCb(void *unused = NULL) { return canSleep() ? 0 : 1; }

    int notifyDeepSleepCb(void *unused = NULL);

    int reloadConfig(void *unused)
    {
        reconfigure();
        return 0;
    }
};

/// Debug printing for packets
void printPacket(const char *prefix, const meshtastic_MeshPacket *p);
