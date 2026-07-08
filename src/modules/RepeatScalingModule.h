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

    // Call each time we hear a duplicate rebroadcast of a packet we ourselves have queued to
    // rebroadcast. Tracks the running count for (p->from, p->id) and logs the decision. Returns
    // true once the configured per-portnum threshold has been reached, in which case the caller
    // should cancel its own queued rebroadcast; tracking for the packet is cleared as soon as
    // this returns true, so a reused ring slot can't leak into an unrelated future packet.
    // Virtual solely so tests of FloodingRouter's role-gating can substitute a test double instead
    // of driving the real threshold/ring-buffer logic (which is tested directly in
    // test/test_repeat_scaling_module).
    virtual bool shouldCancelDupe(const meshtastic_MeshPacket *p);

  protected:
    // How many duplicate rebroadcasts of a packet we require to hear (see the per-portnum switch
    // in RepeatScalingModule.cpp) before giving up on our own scheduled rebroadcast of it. Virtual
    // solely so tests can override it to inject a threshold without needing a real portnum case
    // (see test/test_repeat_scaling_module).
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

extern RepeatScalingModule *repeatScalingModule;
