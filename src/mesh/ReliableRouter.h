#pragma once

#include "FloodingRouter.h"
#include "PeriodicTask.h"

/**
 * This is a mixin that extends Router with the ability to do (one hop only) reliable message sends.
 */
class ReliableRouter : public FloodingRouter
{
  private:
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

    /**
     * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
     */
    void startRetransmission(MeshPacket *p);

    /**
     * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
     */
    void doRetransmissions();
};
