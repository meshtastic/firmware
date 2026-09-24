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

    virtual meshtastic_MeshPacket_AckProofStatus ackProofStatusFor(const meshtastic_MeshPacket &p) const override;

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
     * May this ack/nak act on our pending send for `originalId`? Always true unless enforcement is
     * on AND the packet is a success ack that either carries a proof failing to verify, or does not
     * come from the node we addressed. An absent proof is never a failure: only peers we share a
     * PKI key with can produce one at all.
     *
     * isAck separates the two: a nak legitimately arrives from an intermediate rather than from the
     * destination, so it is never held to the sender check.
     */
    bool ackProofPermitsAction(const meshtastic_MeshPacket *p, PacketId originalId, bool isAck);

    /** The last verdict ackProofPermitsAction() reached, keyed by the ack that carried it. */
    struct AckProofVerdict {
        NodeNum from = 0;
        PacketId id = 0;
        meshtastic_MeshPacket_AckProofStatus status = meshtastic_MeshPacket_AckProofStatus_ACK_PROOF_ABSENT;
    };
    AckProofVerdict lastAckProof;
};