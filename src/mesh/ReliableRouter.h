#pragma once

#include "NextHopRouter.h"

/**
 * This is a mixin that extends Router with the ability to do (one hop only) reliable message sends.
 */
class ReliableRouter : public NextHopRouter
{
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
    virtual ErrorCode send(meshtastic_MeshPacket *p) override;

    /// The ladder send() would start for `p`: five for a reliable unicast, three for a reliable
    /// broadcast, one without want_ack.
    uint8_t sendAttempts(const meshtastic_MeshPacket *p) const override
    {
        if (!p->want_ack)
            return 1;
        return isBroadcast(p->to) ? NUM_RELIABLE_RETX : NUM_RELIABLE_UNICAST_ATTEMPTS;
    }

  protected:
    /**
     * Look for acks/naks or someone retransmitting us
     */
    virtual void sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c) override;

    /**
     * We hook this method so we can see packets before FloodingRouter says they should be discarded
     */
    virtual bool shouldFilterReceived(const meshtastic_MeshPacket *p) override;

    /**
     * Header-only implicit ACK for our own overheard rebroadcast (also usable before decode).
     */
    virtual void perhapsGenerateImplicitAckForOwnOverheard(const meshtastic_MeshPacket *p) override;

  private:
    /**
     * Should this packet be ACKed with a want_ack for reliable delivery?
     */
    bool shouldSuccessAckWithWantAck(const meshtastic_MeshPacket *p);

    /**
     * May this ack/nak act on our pending send for `originalId`? Always true unless the ack carries
     * a pairwise proof that fails to verify AND enforcement is on. An absent proof is not a failure:
     * only peers we share a PKI key with can produce one at all.
     */
    bool ackProofPermitsAction(const meshtastic_MeshPacket *p, PacketId originalId);
};