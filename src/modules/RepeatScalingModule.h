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

    // Note a heard duplicate for (p->from, p->id); returns true (and clears tracking) once the
    // per-portnum threshold is reached, meaning the caller should cancel its own rebroadcast.
    // Virtual so FloodingRouter role-gating tests can substitute a double.
    virtual bool shouldCancelDupe(const meshtastic_MeshPacket *p);

    // Cache the portnum of a rebroadcast we've scheduled, so later encrypted duplicates of it can
    // still be classified by getDupeCancelThreshold(). Pass -1 if it couldn't be decoded.
    void noteScheduled(NodeNum sender, PacketId id, int32_t portnum);

    // Duplicates heard (and tolerated) so far for (sender, id), or 0. For logging at TX time.
    uint8_t getToleratedDupeCount(NodeNum sender, PacketId id) const;

  protected:
    // Duplicates to tolerate before cancelling our own rebroadcast. Virtual so tests can inject a
    // threshold without relying on a real portnum case.
    virtual uint8_t getDupeCancelThreshold(const meshtastic_MeshPacket *p);

    // Ephemeral ring buffer of per-(sender, id) heard-duplicate counts (not persistent state).
    uint8_t registerDupeHeard(NodeNum sender, PacketId id);
    void clearDupeCount(NodeNum sender, PacketId id);
    int32_t lookupNotedPortnum(NodeNum sender, PacketId id) const;

  private:
    // Decoded portnum if available, else the one cached by noteScheduled() (or -1).
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
};

extern RepeatScalingModule *repeatScalingModule;
