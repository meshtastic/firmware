#pragma once

#include "FloodingRouter.h"

/*
  Router which only relays if it is the next hop for a packet.
  The next hop is set by the current relayer of a packet, which bases this on information from either the NeighborInfoModule, or a
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

  protected:
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

  private:
    /**
     * Get the next hop for a destination, given the current relayer
     * @return the node number of the next hop, 0 if no preference (fallback to FloodingRouter)
     */
    uint32_t getNextHop(NodeNum to, NodeNum current_relayer);
};