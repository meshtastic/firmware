#pragma once

#include "FloodingRouter.h"
#include <unordered_map>

/**
 * An identifier for a globally unique message - a pair of the sending nodenum and the packet id assigned
 * to that message
 */
struct GlobalPacketId {
    NodeNum node;
    PacketId id;

    bool operator==(const GlobalPacketId &p) const { return node == p.node && id == p.id; }

    explicit GlobalPacketId(const meshtastic_MeshPacket *p)
    {
        node = getFrom(p);
        id = p->id;
    }

    GlobalPacketId(NodeNum _from, PacketId _id)
    {
        node = _from;
        id = _id;
    }
};

/**
 * A packet queued for retransmission
 */
struct PendingPacket {
    meshtastic_MeshPacket *packet;

    /** The next time we should try to retransmit this packet */
    uint32_t nextTxMsec = 0;

    /** Starts at NUM_RETRANSMISSIONS -1 and counts down.  Once zero it will be removed from the list */
    uint8_t numRetransmissions = 0;

    PendingPacket() {}
    explicit PendingPacket(meshtastic_MeshPacket *p, uint8_t numRetransmissions);
};

class GlobalPacketIdHashFunction
{
  public:
    size_t operator()(const GlobalPacketId &p) const { return (std::hash<NodeNum>()(p.node)) ^ (std::hash<PacketId>()(p.id)); }
};

/*
  Router for direct messages, which only relays if it is the next hop for a packet. The next hop is set by the current
  relayer of a packet, which bases this on information from a previous successful delivery to the destination via flooding.
  Namely, in the PacketHistory, we keep track of (up to 3) relayers of a packet. When the ACK is delivered back to us via a node
  that also relayed the original packet, we use that node as next hop for the destination from then on. This makes sure that only
  when there’s a two-way connection, we assign a next hop. Both the ReliableRouter and NextHopRouter will do retransmissions (the
  NextHopRouter only 1 time). For the final retry, if no one actually relayed the packet, it will reset the next hop in order to
  fall back to the FloodingRouter again. Note that thus also intermediate hops will do a single retransmission if the intended
  next-hop didn’t relay, in order to fix changes in the middle of the route.
*/
class NextHopRouter : public FloodingRouter
{
  public:
    /**
     * Constructor
     *
     */
    NextHopRouter();

    /**
     * Send a packet
     * @return an error code
     */
    virtual ErrorCode send(meshtastic_MeshPacket *p) override;

    /** Do our retransmission handling */
    virtual int32_t runOnce() override
    {
        // Note: We must doRetransmissions FIRST, because it might queue up work for the base class runOnce implementation
        doRetransmissions();

        int32_t r = FloodingRouter::runOnce();

        // Also after calling runOnce there might be new packets to retransmit
        auto d = doRetransmissions();
        return min(d, r);
    }

    // The number of retransmissions intermediate nodes will do (actually 1 less than this)
    constexpr static uint8_t NUM_INTERMEDIATE_RETX = 2;
    // The number of retransmissions the original sender will do
    constexpr static uint8_t NUM_RELIABLE_RETX = 3;

  protected:
    /**
     * Pending retransmissions
     */
    std::unordered_map<GlobalPacketId, PendingPacket, GlobalPacketIdHashFunction> pending;

    /**
     * Should this incoming filter be dropped?
     *
     * Called immediately on reception, before any further processing.
     * @return true to abandon the packet
     */
    virtual bool shouldFilterReceived(const meshtastic_MeshPacket *p) override;

    /**
     * Look for packets we need to relay
     */
    virtual void sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c) override;

    /**
     * Try to find the pending packet record for this ID (or NULL if not found)
     */
    PendingPacket *findPendingPacket(NodeNum from, PacketId id) { return findPendingPacket(GlobalPacketId(from, id)); }
    PendingPacket *findPendingPacket(GlobalPacketId p);

    /**
     * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
     */
    PendingPacket *startRetransmission(meshtastic_MeshPacket *p, uint8_t numReTx = NUM_INTERMEDIATE_RETX);

    /**
     * Stop any retransmissions we are doing of the specified node/packet ID pair
     *
     * @return true if we found and removed a transmission with this ID
     */
    bool stopRetransmission(NodeNum from, PacketId id);
    bool stopRetransmission(GlobalPacketId p);

    /**
     * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
     *
     * @return the number of msecs until our next retransmission or MAXINT if none scheduled
     */
    int32_t doRetransmissions();

    void setNextTx(PendingPacket *pending);

  private:
    /**
     * Get the next hop for a destination, given the relay node
     * @return the node number of the next hop, 0 if no preference (fallback to FloodingRouter)
     */
    uint8_t getNextHop(NodeNum to, uint8_t relay_node);

    /** Check if we should be relaying this packet if so, do so.
     *  @return true if we did relay */
    bool perhapsRelay(const meshtastic_MeshPacket *p);
};