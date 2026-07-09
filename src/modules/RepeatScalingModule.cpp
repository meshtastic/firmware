#include "RepeatScalingModule.h"
#include "DebugConfiguration.h"
#include "airtime.h"
#include "configuration.h"
#include "modules/HopScalingModule.h"

RepeatScalingModule *repeatScalingModule;

// Design notes for getDupeCancelThreshold()'s policy (kept here rather than inline):
//
// Historically we cancel our own queued rebroadcast the instant we hear one duplicate from another
// node. This module lets selected packet types instead tolerate a few heard duplicates first,
// trading a little extra airtime for better delivery odds. Thresholds live in a compile-time
// per-portnum switch (not runtime config) because the right values aren't yet settled.
//
// The switch applies uniformly to broadcasts and DMs - NextHopRouter routes duplicates through the
// same FloodingRouter::perhapsCancelDupe -> shouldCancelDupe path - so a DM of a listed portnum
// gets the same tolerance as a broadcast of it, and only ever for a *decodable* DM (a DM not
// addressed to us can't be decoded by us).
//
// When the portnum can't be determined (packet still encrypted, no noteScheduled() cache hit), we
// fall back to a next_hop gate: NO_NEXT_HOP_PREFERENCE (flood-relayed) gets text-message tolerance
// since it has the same uncertain single-path shape; a specific next_hop does not, as delivery
// there is already backed by the sender's end-to-end ACK/retry.
//
// Either way, meshTooBusyForExtraRepeats() forces the threshold back to 1 on a busy/dense mesh.

namespace
{
// Thresholds above which the mesh is busy/dense enough that extra repeats aren't worth the airtime.
constexpr float BUSY_CHANNEL_UTIL_PERCENT = 10.0f;
constexpr float BUSY_AIR_UTIL_TX_PERCENT = 4.0f;
constexpr uint16_t BUSY_DIRECT_ACTIVE_NODES = 10;

// True if channel/air utilization or direct-neighbor density says the mesh is too busy for extra
// repeats. Logs which condition tripped.
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
    // perHop[0] is HopScalingModule's estimate of active direct (hop_away == 0) neighbors.
    if (hopScalingModule && hopScalingModule->getLastPerHopCounts().perHop[0] > BUSY_DIRECT_ACTIVE_NODES) {
        LOG_DEBUG("[REPEATSCALE] Mesh busy: directActiveNodes=%u > %u", hopScalingModule->getLastPerHopCounts().perHop[0],
                  BUSY_DIRECT_ACTIVE_NODES);
        return true;
    }
#endif
    return false;
}
} // namespace

// Per-portnum duplicate-tolerance threshold; see the design notes above for the full rationale.
uint8_t RepeatScalingModule::getDupeCancelThreshold(const meshtastic_MeshPacket *p)
{
    const int32_t portnum = resolvePortnum(p);

    uint8_t threshold;
    if (portnum >= 0) {
        switch (portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
            // User-visible chat: no broadcast ACK/retry safety net, so tolerate one heard repeat.
            threshold = 2;
            break;
        default:
            threshold = 1;
            break;
        }
    } else {
        // Portnum unknowable (undecodable packet): fall back to the plaintext next_hop header.
        threshold = (p->next_hop == NO_NEXT_HOP_PREFERENCE) ? 2 : 1;
        LOG_DEBUG("[REPEATSCALE] portnum unknown for 0x%08x from=0x%08x; next_hop=0x%x -> threshold=%u", p->id, p->from,
                  p->next_hop, threshold);
    }

    // A busy/dense mesh overrides any extra tolerance decided above.
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
    // Not tracked yet: claim the next ring slot, evicting whatever was there.
    DupeCountEntry &slot = dupeCounts[dupeCountsNextSlot];
    dupeCountsNextSlot = (dupeCountsNextSlot + 1) % DUPE_COUNT_TRACKER_SIZE;
    slot.sender = sender;
    slot.id = id;
    slot.count = 1;
    slot.portnum = -1; // no noteScheduled() preceded this
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
    // Not tracked yet: claim the next ring slot. count starts at 0 (not 1) since scheduling our
    // own rebroadcast is not itself a heard duplicate.
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

int32_t RepeatScalingModule::resolvePortnum(const meshtastic_MeshPacket *p) const
{
    return (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) ? p->decoded.portnum
                                                                           : lookupNotedPortnum(p->from, p->id);
}

uint8_t RepeatScalingModule::getToleratedDupeCount(NodeNum sender, PacketId id) const
{
    for (auto &entry : dupeCounts) {
        if (entry.id == id && entry.sender == sender)
            return entry.count;
    }
    return 0;
}

bool RepeatScalingModule::shouldCancelDupe(const meshtastic_MeshPacket *p)
{
    const uint8_t threshold = getDupeCancelThreshold(p);
    const uint8_t dupesHeard = registerDupeHeard(p->from, p->id);
    const int32_t portnum = resolvePortnum(p); // for logging only

    if (dupesHeard >= threshold) {
        LOG_INFO("[REPEATSCALE] Giving up own rebroadcast of 0x%08x from=0x%08x portnum=%d: heard %u/%u duplicate(s)", p->id,
                 p->from, portnum, dupesHeard, threshold);
        clearDupeCount(p->from, p->id);
        return true;
    }

    LOG_DEBUG("[REPEATSCALE] Tolerated duplicate %u/%u of 0x%08x from=0x%08x portnum=%d: will still transmit our own "
              "rebroadcast",
              dupesHeard, threshold, p->id, p->from, portnum);
    return false;
}
