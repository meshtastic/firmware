#pragma once

#include "MeshTypes.h"
#include "mesh/mesh-pb-constants.h"

/**
 * RepeatScalingModule owns the "how many duplicate rebroadcasts of a packet we ourselves have
 * queued to rebroadcast should we tolerate before giving up" decision (see
 * FloodingRouter::perhapsCancelDupe, its sole caller).
 *
 * The historical behavior is to cancel our own queued rebroadcast as soon as we hear the very
 * first duplicate from another node. This module allows some packet types (see
 * getDupeCancelThreshold in RepeatScalingModule.cpp) to instead tolerate a configurable number of
 * heard duplicates first, trading a little extra airtime for better delivery odds - unless the
 * mesh is already busy/dense, in which case it always falls back to the historical behavior.
 */
class RepeatScalingModule
{
  public:
    RepeatScalingModule() = default;
    virtual ~RepeatScalingModule() = default;

    // True once enough duplicates are heard to cancel ours. Virtual for test doubles.
    virtual bool shouldCancelDupe(const meshtastic_MeshPacket *p);

    // Portnum of a rebroadcast we queued (-1 if undecoded), for its later encrypted duplicates.
    void noteScheduled(NodeNum sender, PacketId id, int32_t portnum);

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

    static constexpr uint8_t DUPE_COUNT_TRACKER_SIZE = 8;
    struct DupeCountEntry {
        NodeNum sender = 0;
        PacketId id = 0;
        uint8_t count = 0;
        int32_t portnum = -1;
    };
    DupeCountEntry dupeCounts[DUPE_COUNT_TRACKER_SIZE];
    uint8_t dupeCountsNextSlot = 0;

    const DupeCountEntry *findEntry(NodeNum sender, PacketId id) const;
    DupeCountEntry *findEntry(NodeNum sender, PacketId id);
    // A cleared slot if any, else the oldest.
    DupeCountEntry &claimEntry(NodeNum sender, PacketId id);
};

extern RepeatScalingModule *repeatScalingModule;
