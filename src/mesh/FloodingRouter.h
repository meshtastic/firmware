#pragma once

#include "PacketHistory.h"
#include "Router.h"
#include "modules/NeighborInfoModule.h"
#include "modules/TraceRouteModule.h"

/**
 * This is a mixin that extends Router with the ability to do Naive Flooding (in the standard mesh protocol sense)
 *
 *   Rules for broadcasting (listing here for now, will move elsewhere eventually):

  If to==BROADCAST and id==0, this is a simple broadcast (0 hops).  It will be
  sent only by the current node and other nodes will not attempt to rebroadcast
  it.

  If to==BROADCAST and id!=0, this is a "naive flooding" broadcast.  The initial
  node will send it on all local interfaces.

  When other nodes receive this message, they will
  first check if their recentBroadcasts table contains the (from, id) pair that
  indicates this message.  If so, we've already seen it - so we discard it.  If
  not, we add it to the table and then resend this message on all interfaces.
  When resending we are careful to use the "from" ID of the original sender. Not
  our own ID.  When resending we pick a random delay between 0 and 10 seconds to
  decrease the chance of collisions with transmitters we can not even hear.

  Any entries in recentBroadcasts that are older than X seconds (longer than the
  max time a flood can take) will be discarded.
 */
class FloodingRouter : public Router, protected PacketHistory
{
  private:
  public:
    /**
     * Constructor
     *
     */
    FloodingRouter();

    /**
     * Send a packet on a suitable interface.  This routine will
     * later free() the packet to pool.  This routine is not allowed to stall.
     * If the txmit queue is full it might return an error
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
     * Look for broadcasts we need to rebroadcast
     */
    virtual void sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c) override;
};