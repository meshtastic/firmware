#pragma once

#include "FloodingRouter.h"
#include "PeriodicTask.h"
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

    PendingPacket() {}
    PendingPacket(MeshPacket *p);

    void setNextTx() { nextTxMsec = millis() + random(10 * 1000, 12 * 1000); }
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
     * Called from loop()
     * Handle any packet that is received by an interface on this node.
     * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
     *
     * Note: this method will free the provided packet
     */
    virtual void handleReceived(MeshPacket *p);

  private:
    /**
     * Send an ack or a nak packet back towards whoever sent idFrom
     */
    void sendAckNak(bool isAck, NodeNum to, PacketId idFrom);

    /**
     * Stop any retransmissions we are doing of the specified node/packet ID pair
     */
    void stopRetransmission(NodeNum from, PacketId id);
    void stopRetransmission(GlobalPacketId p);

    /**
     * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
     */
    void startRetransmission(MeshPacket *p);

    /**
     * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
     */
    void doRetransmissions();
};
