#pragma once

#include "Router.h"

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
class FloodingRouter : public Router
{
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

    /* Check if we should rebroadcast this packet, and do so if needed */
    virtual bool perhapsRebroadcast(const meshtastic_MeshPacket *p) = 0;

    /* Check if we should handle an upgraded packet (with higher hop_limit)
     * @return true if we handled it (so stop processing)
     */
    bool perhapsHandleUpgradedPacket(const meshtastic_MeshPacket *p);

    /* Call when we receive a packet that needs some reprocessing, but afterwards should be filtered */
    void reprocessPacket(const meshtastic_MeshPacket *p);

    // Return false for roles like ROUTER which should always rebroadcast even when we've heard another rebroadcast of
    // the same packet
    bool roleAllowsCancelingDupe(const meshtastic_MeshPacket *p);

    /* Call when receiving a duplicate packet to check whether we should cancel a packet in the Tx queue */
    void perhapsCancelDupe(const meshtastic_MeshPacket *p);

    // Return true if we are a rebroadcaster
    bool isRebroadcaster();

    // How many duplicate rebroadcasts of a packet we require to hear (see the per-portnum switch in
    // FloodingRouter.cpp) before giving up on our own scheduled rebroadcast of it. Virtual solely so
    // tests can override it to inject a threshold without needing a real portnum case (see
    // test/test_flooding_router).
    virtual uint8_t getDupeCancelThreshold(const meshtastic_MeshPacket *p);

    // Tracks how many duplicates we've heard so far, per (sender, id), for packets we currently
    // have one queued to rebroadcast ourselves. Bounded, ephemeral ring buffer - not a persistent
    // record like PacketHistory: entries are only meaningful while our own rebroadcast is still
    // pending, and naturally get evicted/reused as the ring wraps.
    uint8_t registerDupeHeard(NodeNum sender, PacketId id);

    // Clears tracking state for a (sender, id) once we've acted on it (cancelled our rebroadcast),
    // so a reused ring slot can't cause a stale hit against an unrelated future packet.
    void clearDupeCount(NodeNum sender, PacketId id);

  private:
    static constexpr uint8_t DUPE_COUNT_TRACKER_SIZE = 8;
    struct DupeCountEntry {
        NodeNum sender = 0;
        PacketId id = 0;
        uint8_t count = 0;
    };
    DupeCountEntry dupeCounts[DUPE_COUNT_TRACKER_SIZE];
    uint8_t dupeCountsNextSlot = 0;
};