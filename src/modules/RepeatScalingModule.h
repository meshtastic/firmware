#pragma once

#include "MeshTypes.h"
#include "mesh/mesh-pb-constants.h"

/**
 * Decides how many duplicates of a packet we have queued to rebroadcast must be heard before
 * FloodingRouter::perhapsCancelDupe cancels our copy. The default is 1: cancel on the first.
 *
 * - Text messages (plain and compressed) tolerate one heard duplicate (threshold 2): chat has no
 *   broadcast ACK/retry.
 * - A packet we cannot decode is judged from its plaintext header: a DM-shaped flood (to a single
 *   node, NO_NEXT_HOP_PREFERENCE) gets threshold 2; broadcasts and routed DMs get 1. noteScheduled()
 *   caches the portnum when we queue a rebroadcast (except threshold-1 broadcasts, which the header
 *   already gets right), so its later encrypted duplicates use the table.
 * - On a busy mesh (channel or TX airtime above the BUSY_* limits, or, on HAS_VARIABLE_HOPS builds, too
 *   many direct neighbours) every threshold is 1, today's behaviour.
 *
 * Thresholds are compile-time. Counts live in a small RAM ring, keyed by (sender, id).
 */
class RepeatScalingModule
{
  public:
    RepeatScalingModule() = default;
    virtual ~RepeatScalingModule() = default;

    // True once enough duplicates are heard to cancel ours. Virtual for test doubles.
    virtual bool shouldCancelDupe(const meshtastic_MeshPacket *p);

    // Caches a queued rebroadcast's portnum unless the header alone already judges its duplicates.
    void noteScheduled(const meshtastic_MeshPacket *p);
    // Unconditional form: portnum of a rebroadcast we queued, for its later encrypted duplicates.
    void noteScheduled(NodeNum sender, PacketId id, int32_t portnum);

    // One slot per packet the radio can hold queued; RepeatScalingModule.cpp asserts the match.
    static constexpr uint8_t DUPE_COUNT_TRACKER_SIZE = 16;

    // Above any of these the mesh is too busy for extra repeats.
    static constexpr float BUSY_CHANNEL_UTIL_PERCENT = 10.0f;
    static constexpr float BUSY_AIR_UTIL_TX_PERCENT = 4.0f;
    static constexpr uint16_t BUSY_DIRECT_ACTIVE_NODES = 10;

    // Takes its inputs so tests can reach it; AirTime's buckets are private.
    static bool meshTooBusy(float channelUtilPercent, float airUtilTxPercent, uint16_t directActiveNodes);

  protected:
    // Duplicates to hear before cancelling ours. Virtual for tests.
    virtual uint8_t getDupeCancelThreshold(const meshtastic_MeshPacket *p);

    // meshTooBusy() on the live globals. Virtual for tests.
    virtual bool meshTooBusyForExtraRepeats();

    uint8_t registerDupeHeard(NodeNum sender, PacketId id);
    void clearDupeCount(NodeNum sender, PacketId id);
    int32_t lookupNotedPortnum(NodeNum sender, PacketId id) const;

  private:
    // Decoded portnum, else the one noteScheduled() cached, else -1.
    int32_t resolvePortnum(const meshtastic_MeshPacket *p) const;
    // Threshold from a known portnum, and from the plaintext header when the portnum is unknown.
    static uint8_t portnumThreshold(int32_t portnum);
    static uint8_t headerThreshold(const meshtastic_MeshPacket *p);

    struct DupeCountEntry {
        NodeNum sender = 0;
        PacketId id = 0;
        uint8_t count = 0;
        int32_t portnum = -1;
        uint32_t claimedSeq = 0; // claim order, so the oldest claim is the one evicted
    };
    DupeCountEntry dupeCounts[DUPE_COUNT_TRACKER_SIZE];
    uint32_t claimSeq = 0;

    const DupeCountEntry *findEntry(NodeNum sender, PacketId id) const;
    DupeCountEntry *findEntry(NodeNum sender, PacketId id);
    // A cleared slot if any, else the oldest.
    DupeCountEntry &claimEntry(NodeNum sender, PacketId id);
};

extern RepeatScalingModule *repeatScalingModule;
