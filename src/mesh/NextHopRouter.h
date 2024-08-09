#pragma once

#include "FloodingRouter.h"

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

    /** Starts at NUM_RETRANSMISSIONS -1(normally 3) and counts down.  Once zero it will be removed from the list */
    uint8_t numRetransmissions = 0;

    PendingPacket() {}
    explicit PendingPacket(meshtastic_MeshPacket *p);
};

class GlobalPacketIdHashFunction
{
  public:
    size_t operator()(const GlobalPacketId &p) const { return (std::hash<NodeNum>()(p.node)) ^ (std::hash<PacketId>()(p.id)); }
};

/*
  Router which only relays if it is the next hop for a packet.
  The next hop is set by the relay node of a packet, which bases this on information from either the NeighborInfoModule, or a
  previous successful delivery via flooding. It is only used for DMs and not used for broadcasts. Using the NeighborInfoModule, it
  can derive the next hop of neighbors and that of neighbors of neighbors. For others, it has no information in the beginning,
  which results into falling back to the FloodingRouter. Upon successful delivery via flooding, it updates the next hop of the
  recipient to the node that last relayed the ACK to us. When the ReliableRouter is doing retransmissions, at the last retry, it
  will reset the next hop, in order to fall back to the FloodingRouter.
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
        auto d = doRetransmissions();

        int32_t r = FloodingRouter::runOnce();

        return min(d, r);
    }

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

    constexpr static uint8_t NO_NEXT_HOP_PREFERENCE = 0;

    /**
     * Try to find the pending packet record for this ID (or NULL if not found)
     */
    PendingPacket *findPendingPacket(NodeNum from, PacketId id) { return findPendingPacket(GlobalPacketId(from, id)); }
    PendingPacket *findPendingPacket(GlobalPacketId p);

    /**
     * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
     */
    PendingPacket *startRetransmission(meshtastic_MeshPacket *p);

  private:
    /**
     * Get the next hop for a destination, given the relay node
     * @return the node number of the next hop, 0 if no preference (fallback to FloodingRouter)
     */
    uint8_t getNextHop(NodeNum to, uint8_t relay_node);

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
};