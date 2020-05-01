#pragma once

#include "PeriodicTask.h"
#include "Router.h"
#include <vector>

/**
 * A record of a recent message broadcast
 */
struct BroadcastRecord {
    NodeNum sender;
    PacketId id;
    uint32_t rxTimeMsec; // Unix time in msecs - the time we received it
};

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
class FloodingRouter : public Router, public PeriodicTask
{
  private:
    /** FIXME: really should be a std::unordered_set with the key being sender,id.
     * This would make checking packets in wasSeenRecently faster.
     */
    std::vector<BroadcastRecord> recentBroadcasts;

    /**
     * Packets we've received that we need to resend after a short delay
     */
    PointerQueue<MeshPacket> toResend;

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
    virtual ErrorCode send(MeshPacket *p);

  protected:
    /**
     * Called from loop()
     * Handle any packet that is received by an interface on this node.
     * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
     *
     * Note: this method will free the provided packet
     */
    virtual void handleReceived(MeshPacket *p);

    virtual void doTask();

  private:
    /**
     * Update recentBroadcasts and return true if we have already seen this packet
     */
    bool wasSeenRecently(const MeshPacket *p);
};
