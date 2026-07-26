#pragma once

#include "Channels.h"
#include "MemoryPool.h"
#include "MeshTypes.h"
#include "Observer.h"
#include "PacketHistory.h"
#include "PointerQueue.h"
#include "RadioInterface.h"
#include "concurrency/OSThread.h"
#include <memory>

/**
 * A mesh aware router that supports multiple interfaces.
 */
class Router : protected concurrency::OSThread, protected PacketHistory
{
  private:
    /// Packets which have just arrived from the radio, ready to be processed by this service and possibly
    /// forwarded to the phone.
    PointerQueue<meshtastic_MeshPacket> fromRadioQueue;

  protected:
    std::unique_ptr<RadioInterface> iface = nullptr;

  public:
    /**
     * Constructor
     *
     */
    Router();

    /**
     * Currently we only allow one interface, that may change in the future
     */
    void addInterface(std::unique_ptr<RadioInterface> _iface) { iface = std::move(_iface); }

    /**
     * Borrowed (non-owning) access to the radio interface - used by NodeDB
     * after a lockdown unlock so it can push the freshly-loaded config to
     * the SX12xx via reconfigure(). Returns nullptr when no radio has been
     * attached (e.g. ARCH_PORTDUINO simulator before SimRadio bind).
     */
    RadioInterface *getRadioIface() { return iface.get(); }

    /**
     * do idle processing
     * Mostly looking in our incoming rxPacket queue and calling handleReceived.
     */
    virtual int32_t runOnce() override;

    /**
     * Works like send, but if we are sending to the local node, we directly put the message in the receive queue.
     * This is the primary method used for sending packets, because it handles both the remote and local cases.
     *
     * NOTE: This method will free the provided packet (even if we return an error code)
     */
    ErrorCode sendLocal(meshtastic_MeshPacket *p, RxSource src = RX_SRC_RADIO);

    /** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
    bool cancelSending(NodeNum from, PacketId id);

    /** Attempt to find a packet in the TxQueue. Returns true if the packet was found. */
    bool findInTxQueue(NodeNum from, PacketId id);

    /** Allocate and return a meshpacket which defaults as send to broadcast from the current node.
     * The returned packet is guaranteed to have a unique packet ID already assigned
     */
    [[nodiscard]] meshtastic_MeshPacket *allocForSending();

    /** Return Underlying interface's TX queue status */
    [[nodiscard]] meshtastic_QueueStatus getQueueStatus();

    /**
     * @return our local nodenum */
    [[nodiscard]] NodeNum getNodeNum();

    /** Wake up the router thread ASAP, because we just queued a message for it.
     * FIXME, this is kinda a hack because we don't have a nice way yet to say 'wake us because we are 'blocked on this queue'
     */
    void setReceivedMessage();

    /**
     * RadioInterface calls this to queue up packets that have been received from the radio.  The router is now responsible for
     * freeing the packet
     */
    virtual void enqueueReceivedMessage(meshtastic_MeshPacket *p);

    /**
     * Send a packet on a suitable interface.  This routine will
     * later free() the packet to pool.  This routine is not allowed to stall.
     * If the txmit queue is full it might return an error
     *
     * NOTE: This method will free the provided packet (even if we return an error code)
     */
    virtual ErrorCode send(meshtastic_MeshPacket *p);

    /* Statistics for the amount of duplicate received packets and the amount of times we cancel a relay because someone did it
        before us */
    uint32_t rxDupe = 0, txRelayCanceled = 0;

  protected:
    friend class RoutingModule;

    /**
     * Should this incoming filter be dropped?
     *
     * FIXME, move this into the new RoutingModule and do the filtering there using the regular module logic
     *
     * Called immediately on reception, before any further processing.
     * @return true to abandon the packet
     */
    virtual bool shouldFilterReceived(const meshtastic_MeshPacket *p) { return false; }

    /** Relay an opaque packet without admitting it to local routing/history state. */
    virtual bool relayOpaquePacket(const meshtastic_MeshPacket *) { return false; }

    /**
     * Determine if hop_limit should be decremented for a relay operation.
     * Returns false (preserve hop_limit) only if all conditions are met:
     * - It's NOT the first hop (first hop must always decrement)
     * - Local device is a ROUTER, ROUTER_LATE, or CLIENT_BASE
     * - Previous relay is a favorite ROUTER, ROUTER_LATE, or CLIENT_BASE
     *
     * @param p The packet being relayed
     * @return true if hop_limit should be decremented, false to preserve it
     */
    bool shouldDecrementHopLimit(const meshtastic_MeshPacket *p);

    /**
     * Every (non duplicate) packet this node receives will be passed through this method.  This allows subclasses to
     * update routing tables etc... based on what we overhear (even for messages not destined to our node)
     */
    virtual void sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c);

    /**
     * Send an ack or a nak packet back towards whoever sent idFrom
     */
    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit = 0,
                    bool ackWantsAck = false);

  private:
    /**
     * Called from loop()
     * Handle any packet that is received by an interface on this node.
     * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
     *
     * Note: this packet will never be called for messages sent/generated by this node.
     * Note: this method will free the provided packet.
     */
    void perhapsHandleReceived(meshtastic_MeshPacket *p);

    /**
     * Called from perhapsHandleReceived() - allows subclass message delivery behavior.
     * Handle any packet that is received by an interface on this node.
     * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
     *
     * Note: this packet will never be called for messages sent/generated by this node.
     * Note: this method will free the provided packet.
     */
    void handleReceived(meshtastic_MeshPacket *p, RxSource src = RX_SRC_RADIO);

    /**
     * The body of handleReceived(): decode, run modules, publish to MQTT. Split out so the
     * depth-guarded drain in handleReceived() can process a deferred packet without re-entering
     * the drain (and without touching handleDepth) - keeping the stack flat.
     */
    void dispatchReceived(meshtastic_MeshPacket *p, RxSource src);

    /**
     * Route a packet addressed to us (or a local broadcast we loop back) into handleReceived().
     * Called synchronously at the top level, but if a module sends this from inside callModules()
     * (handleDepth > 0) the packet is copied into the deferred queue instead, so we never stack a
     * second handleReceived() on top of a module handler - that nesting is what overflows the
     * nRF52 task stack on a config save. Does not consume p; the caller's existing free path is
     * unchanged.
     */
    void deliverLocal(meshtastic_MeshPacket *p, RxSource src);

    /// Depth of handleReceived() frames currently on the stack. >0 means a module is dispatching,
    /// so a locally-sent loopback packet must be deferred rather than handled synchronously.
    uint8_t handleDepth = 0;

    /// A local loopback packet whose handleReceived() was deferred because it was produced from
    /// inside callModules(). The queue owns the packet; its RxSource travels with it so the drain
    /// dispatches it with the origin the sender intended (RX_SRC_LOCAL stays local).
    struct DeferredLocal {
        meshtastic_MeshPacket *p;
        RxSource src;
    };

    /// Fixed, small ring buffer of deferred local packets. A config save fans out only a few
    /// loopback packets (a self-addressed reply plus a nodeinfo/config broadcast or two), so four
    /// slots cover the realistic nesting. On overflow the deferral is dropped (the packet still
    /// followed its normal non-loopback path) rather than blocking or growing the heap.
    static constexpr uint8_t deferredLocalCapacity = 4;
    DeferredLocal deferredLocalQueue[deferredLocalCapacity];
    uint8_t deferredLocalHead = 0;  // index of the oldest queued entry
    uint8_t deferredLocalCount = 0; // entries currently queued

    /// Queue a deferred local packet. Returns false (and queues nothing) when full.
    bool enqueueDeferredLocal(meshtastic_MeshPacket *p, RxSource src);
    /// Pop the oldest deferred local packet into out. Returns false when empty.
    bool dequeueDeferredLocal(DeferredLocal &out);

    /** Frees the provided packet, and generates a NAK indicating the specifed error while sending */
    void abortSendAndNak(meshtastic_Routing_Error err, meshtastic_MeshPacket *p);

#ifdef PIO_UNIT_TESTING
  public:
    /// High-water mark of handleDepth across this Router's life. The deferral must keep it at 1:
    /// a nested local send may never re-enter handleReceived() synchronously.
    uint8_t maxHandleDepthObserved = 0;
    /// Count of deferrals dropped because the queue was full or a copy could not be allocated.
    uint32_t deferredLocalDropped = 0;
    /// Number of deferred local packets currently queued.
    uint8_t deferredLocalPending() const { return deferredLocalCount; }
#endif
};

enum DecodeState { DECODE_SUCCESS, DECODE_FAILURE, DECODE_OPAQUE, DECODE_FATAL, DECODE_POLICY_REJECT };
enum class RoutingAuthVerdict { ACCEPT, OPAQUE_RELAY_ONLY, REJECT };

/** FIXME - move this into a mesh packet class
 * Remove any encryption and decode the protobufs inside this packet (if necessary).
 *
 * @return true for success, false for corrupt packet.
 */
DecodeState perhapsDecode(meshtastic_MeshPacket *p);

/** Apply receive authentication before routing state mutation; unknown-channel packets may remain opaque relay-only. */
RoutingAuthVerdict passesRoutingAuthGate(meshtastic_MeshPacket *p);
#ifdef PIO_UNIT_TESTING
uint32_t routingAuthEvaluationCount();
void resetRoutingAuthEvaluationCount();
#endif

/** Return 0 for success or a Routing_Error code for failure
 */
meshtastic_Routing_Error perhapsEncode(meshtastic_MeshPacket *p);

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
/** Enforce the configured XEdDSA receive policy. The caller must hold cryptLock.
 * Returns false when the packet must be dropped. */
bool checkXeddsaReceivePolicy(meshtastic_MeshPacket *p);
#endif

#if !(MESHTASTIC_EXCLUDE_PKI)
/**
 * Would perhapsEncode() PKC-encrypt this outgoing packet? Callers that must know the encryption a
 * packet will get before it is encoded (e.g. pinning a peer key at request time) have to ask this
 * rather than inspect p, whose pki_encrypted/public_key fields are only populated on the RX path.
 *
 * @param chIndex the channel index p carries before encoding rewrites it to a hash.
 * @param haveDestKey whether a public key for p->to was resolvable.
 */
bool wouldEncryptWithPKC(const meshtastic_MeshPacket *p, ChannelIndex chIndex, bool haveDestKey);
#endif

extern Router *router;

/// Generate a unique packet id
// FIXME, move this someplace better
PacketId generatePacketId();

#define BITFIELD_WANT_RESPONSE_SHIFT 1
#define BITFIELD_OK_TO_MQTT_SHIFT 0
#define BITFIELD_WANT_RESPONSE_MASK (1 << BITFIELD_WANT_RESPONSE_SHIFT)
#define BITFIELD_OK_TO_MQTT_MASK (1 << BITFIELD_OK_TO_MQTT_SHIFT)
