#pragma once

#include "FloodingRouter.h"
#include "../concurrency/PeriodicTask.h"
#include "../timing.h"
#include <unordered_map>

/**
 * An identifier for a globalally unique message - a pair of the sending nodenum and the packet id assigned
 * to that message
 */
struct GlobalPacketId {
    NodeNum node;
    PacketId id;

    bool operator==(const GlobalPacketId &p) const { return node == p.node && id == p.id; }

    GlobalPacketId(const MeshPacket *p)
    {
        node = p->from;
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
    MeshPacket *packet;

    /** The next time we should try to retransmit this packet */
    uint32_t nextTxMsec;

    /** Starts at NUM_RETRANSMISSIONS -1(normally 3) and counts down.  Once zero it will be removed from the list */
    uint8_t numRetransmissions;

    /** True if we have started trying to find a route - for DSR usage
     * While trying to find a route we don't actually send the data packet.  We just leave it here pending until
     * we have a route or we've failed to find one.
     */
    bool wantRoute = false;

    PendingPacket() {}
    PendingPacket(MeshPacket *p);

    void setNextTx() { nextTxMsec = timing::millis() + random(20 * 1000L, 22 * 1000L); }
};

class GlobalPacketIdHashFunction
{
  public:
    size_t operator()(const GlobalPacketId &p) const { return (hash<NodeNum>()(p.node)) ^ (hash<PacketId>()(p.id)); }
};

/**
 * This is a mixin that extends Router with the ability to do (one hop only) reliable message sends.
 */
class ReliableRouter : public FloodingRouter
{
  private:
    unordered_map<GlobalPacketId, PendingPacket, GlobalPacketIdHashFunction> pending;

  public:
    /**
     * Constructor
     *
     */
    // ReliableRouter();

    /**
     * Send a packet on a suitable interface.  This routine will
     * later free() the packet to pool.  This routine is not allowed to stall.
     * If the txmit queue is full it might return an error
     */
    virtual ErrorCode send(MeshPacket *p);

    /** Do our retransmission handling */
    virtual void loop()
    {
        doRetransmissions();
        FloodingRouter::loop();
    }

  protected:
    /**
     * Look for acks/naks or someone retransmitting us
     */
    virtual void sniffReceived(const MeshPacket *p);

    /**
     * Try to find the pending packet record for this ID (or NULL if not found)
     */
    PendingPacket *findPendingPacket(NodeNum from, PacketId id) { return findPendingPacket(GlobalPacketId(from, id)); }
    PendingPacket *findPendingPacket(GlobalPacketId p);

    /**
     * We hook this method so we can see packets before FloodingRouter says they should be discarded
     */
    virtual bool shouldFilterReceived(const MeshPacket *p);

    /**
     * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
     */
    PendingPacket *startRetransmission(MeshPacket *p);

  private:
    /**
     * Send an ack or a nak packet back towards whoever sent idFrom
     */
    void sendAckNak(bool isAck, NodeNum to, PacketId idFrom);

    /**
     * Stop any retransmissions we are doing of the specified node/packet ID pair
     *
     * @return true if we found and removed a transmission with this ID
     */
    bool stopRetransmission(NodeNum from, PacketId id);
    bool stopRetransmission(GlobalPacketId p);

    /**
     * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
     */
    void doRetransmissions();
};
