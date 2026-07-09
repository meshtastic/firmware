#include "RepeatScalingModule.h"
#include "DebugConfiguration.h"
#include "airtime.h"
#include "configuration.h"
#include "modules/HopScalingModule.h"

RepeatScalingModule *repeatScalingModule;

namespace
{
// Above any of these, the mesh is busy/dense enough that extra-repeat tolerance (see
// RepeatScalingModule::getDupeCancelThreshold) would just be adding airtime to an already-congested
// channel for little extra delivery benefit - so we fall back to the historical "cancel on first
// duplicate" behavior regardless of what the portnum table says.
constexpr float BUSY_CHANNEL_UTIL_PERCENT = 10.0f;
constexpr float BUSY_AIR_UTIL_TX_PERCENT = 4.0f;
constexpr uint16_t BUSY_DIRECT_ACTIVE_NODES = 10;

// True if channel/air utilization or estimated direct-neighbor density indicates the mesh is
// too busy to spend extra airtime on repeat-tolerant rebroadcasts. Logs which specific condition
// (if any) tripped, so the reason a packet fell back to the historical threshold is visible.
bool meshTooBusyForExtraRepeats()
{
    if (airTime && airTime->channelUtilizationPercent() > BUSY_CHANNEL_UTIL_PERCENT) {
        LOG_DEBUG("[REPEATSCALE] Mesh busy: chUtil=%.1f%% > %.1f%%", airTime->channelUtilizationPercent(),
                  BUSY_CHANNEL_UTIL_PERCENT);
        return true;
    }
    if (airTime && airTime->utilizationTXPercent() > BUSY_AIR_UTIL_TX_PERCENT) {
        LOG_DEBUG("[REPEATSCALE] Mesh busy: airUtilTX=%.1f%% > %.1f%%", airTime->utilizationTXPercent(),
                  BUSY_AIR_UTIL_TX_PERCENT);
        return true;
    }
#if HAS_VARIABLE_HOPS
    // getLastPerHopCounts().perHop[0] is HopScalingModule's estimate of active (heard within its
    // rolling window) direct (hop_away == 0) neighbors - the same "active and direct" notion used
    // for its own hop-limit recommendation.
    if (hopScalingModule && hopScalingModule->getLastPerHopCounts().perHop[0] > BUSY_DIRECT_ACTIVE_NODES) {
        LOG_DEBUG("[REPEATSCALE] Mesh busy: directActiveNodes=%u > %u", hopScalingModule->getLastPerHopCounts().perHop[0],
                  BUSY_DIRECT_ACTIVE_NODES);
        return true;
    }
#endif
    return false;
}
} // namespace

// Compile-time, per-portnum threshold for how many duplicate rebroadcasts of a packet we must hear
// before giving up on our own scheduled rebroadcast of it. The default of 1 (used for any portnum
// not given a case below, and for packets we can't decode - see below) preserves the historical
// behavior: cancel our own rebroadcast as soon as we hear the first duplicate. Raising a portnum's
// threshold makes this node more persistent for that traffic type, continuing to retransmit even
// after hearing some other nodes already repeat it - at the cost of extra airtime for packets
// that are, on balance, probably going to reach their destination anyway.
//
// This same switch applies uniformly to broadcasts and DMs: NextHopRouter::shouldFilterReceived
// calls the same (inherited, non-overridden) FloodingRouter::perhapsCancelDupe -> shouldCancelDupe
// path when it hears a duplicate it wasn't explicitly asked to relay - both in the fallback-to-
// flooding case and the general "duplicate heard, we're not the assigned next hop" case - so a DM
// of a listed portnum gets the same extra tolerance as a broadcast of that portnum, with no
// separate to/from-based gating needed.
//
// This is a compile-time table (not a runtime/protobuf config) because the desired threshold is
// expected to vary per packet type rather than being a single device-wide knob; edit it to tune
// for a given deployment.
//
// Regardless of the table below, meshTooBusyForExtraRepeats() (channel/air utilization or direct-
// neighbor density too high) always forces the threshold back down to 1 - see its definition above.
uint8_t RepeatScalingModule::getDupeCancelThreshold(const meshtastic_MeshPacket *p)
{
    int32_t portnum;
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        portnum = p->decoded.portnum;
    } else {
        // Portnum is only visible once a packet is decoded (i.e. we hold the channel key). A
        // duplicate heard over the air is, in the overwhelmingly common case, still encrypted to
        // us here - only the one copy that made it through Router::handleReceived ever gets
        // decoded. Fall back to the portnum we cached for this same (sender, id) when we
        // ourselves scheduled a rebroadcast of it (see noteScheduled()); if we never scheduled
        // one (or its ring slot was since evicted/cleared), we have no per-portnum signal to act
        // on, so use the default of 1.
        portnum = lookupNotedPortnum(p->from, p->id);
        if (portnum < 0)
            return 1;
    }

    uint8_t threshold;
    switch (portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
    case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
        // User-visible chat, broadcast or DM: no ACK/retry safety net for broadcasts, and no other
        // portnum-specific reason to prefer airtime savings over delivery odds. Tolerate one repeat
        // heard from another node before giving up on our own rebroadcast.
        threshold = 2;
        break;
    default:
        threshold = 1;
        break;
    }

    // A busy/dense mesh overrides any portnum-specific extra tolerance: not worth spending more
    // airtime on repeats when the channel is already congested or there are many direct neighbors
    // to reach anyway.
    if (threshold > 1 && meshTooBusyForExtraRepeats()) {
        LOG_DEBUG("[REPEATSCALE] portnum=%d wanted threshold=%u but mesh is busy; falling back to 1", portnum, threshold);
        return 1;
    }

    return threshold;
}

uint8_t RepeatScalingModule::registerDupeHeard(NodeNum sender, PacketId id)
{
    for (auto &entry : dupeCounts) {
        if (entry.id == id && entry.sender == sender) {
            if (entry.count < UINT8_MAX)
                entry.count++;
            return entry.count;
        }
    }
    // Not tracked yet: claim the next ring slot, evicting whatever packet (if any) was there.
    DupeCountEntry &slot = dupeCounts[dupeCountsNextSlot];
    dupeCountsNextSlot = (dupeCountsNextSlot + 1) % DUPE_COUNT_TRACKER_SIZE;
    slot.sender = sender;
    slot.id = id;
    slot.count = 1;
    slot.portnum = -1; // no noteScheduled() call preceded this - no portnum signal available
    return slot.count;
}

void RepeatScalingModule::clearDupeCount(NodeNum sender, PacketId id)
{
    for (auto &entry : dupeCounts) {
        if (entry.id == id && entry.sender == sender) {
            entry.sender = 0;
            entry.id = 0;
            entry.count = 0;
            entry.portnum = -1;
            return;
        }
    }
}

void RepeatScalingModule::noteScheduled(NodeNum sender, PacketId id, int32_t portnum)
{
    for (auto &entry : dupeCounts) {
        if (entry.id == id && entry.sender == sender) {
            entry.portnum = portnum;
            return;
        }
    }
    // Not tracked yet: claim the next ring slot, evicting whatever packet (if any) was there.
    // count starts at 0 (as opposed to registerDupeHeard's 1) because scheduling our own
    // rebroadcast is not itself a heard duplicate.
    DupeCountEntry &slot = dupeCounts[dupeCountsNextSlot];
    dupeCountsNextSlot = (dupeCountsNextSlot + 1) % DUPE_COUNT_TRACKER_SIZE;
    slot.sender = sender;
    slot.id = id;
    slot.count = 0;
    slot.portnum = portnum;
}

int32_t RepeatScalingModule::lookupNotedPortnum(NodeNum sender, PacketId id) const
{
    for (auto &entry : dupeCounts) {
        if (entry.id == id && entry.sender == sender)
            return entry.portnum;
    }
    return -1;
}

bool RepeatScalingModule::shouldCancelDupe(const meshtastic_MeshPacket *p)
{
    const uint8_t threshold = getDupeCancelThreshold(p);
    const uint8_t dupesHeard = registerDupeHeard(p->from, p->id);
    const int32_t portnum =
        (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) ? p->decoded.portnum : lookupNotedPortnum(p->from, p->id);

    if (dupesHeard >= threshold) {
        LOG_INFO("[REPEATSCALE] Giving up own rebroadcast of 0x%08x from=0x%08x portnum=%d: heard %u/%u duplicate(s)", p->id,
                 p->from, portnum, dupesHeard, threshold);
        clearDupeCount(p->from, p->id);
        return true;
    }

    LOG_DEBUG("[REPEATSCALE] Tolerating duplicate %u/%u of 0x%08x from=0x%08x portnum=%d: keeping own rebroadcast queued",
              dupesHeard, threshold, p->id, p->from, portnum);
    return false;
}
