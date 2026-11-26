#include "RepeatScalingModule.h"
#include "DebugConfiguration.h"
#include "airtime.h"
#include "configuration.h"
#include "mesh/RadioInterface.h"
#include "modules/HopScalingModule.h"
#include <algorithm>

static_assert(RepeatScalingModule::DUPE_COUNT_TRACKER_SIZE >= MAX_TX_QUEUE, "one tracker slot per queued packet");

RepeatScalingModule *repeatScalingModule;
static bool busy(const char *which)
{
    LOG_TRACE("[REPEATSCALE] mesh busy: %s", which);
    return true;
}

bool RepeatScalingModule::meshTooBusy(float channelUtilPercent, float airUtilTxPercent, uint16_t directActiveNodes)
{
    if (channelUtilPercent > BUSY_CHANNEL_UTIL_PERCENT) {
        return busy("chUtil");
    }
    if (airUtilTxPercent > BUSY_AIR_UTIL_TX_PERCENT) {
        return busy("airUtilTX");
    }
#if HAS_VARIABLE_HOPS
    // Estimated active direct neighbours (scaled perHop[0])
    if (directActiveNodes > BUSY_DIRECT_ACTIVE_NODES) {
        return busy("directActiveNodes");
    }
#else
    (void)directActiveNodes;
#endif
    return false;
}

bool RepeatScalingModule::meshTooBusyForExtraRepeats()
{
    const float channelUtilPercent = airTime ? airTime->channelUtilizationPercent() : 0.0f;
    const float airUtilTxPercent = airTime ? airTime->utilizationTXPercent() : 0.0f;
#if HAS_VARIABLE_HOPS
    uint16_t directActiveNodes = 0;
    if (hopScalingModule) {
        // perHop[] is a hash subsample; scale by the denominator captured with that tally
        const auto &counts = hopScalingModule->getLastPerHopCounts();
        directActiveNodes =
            static_cast<uint16_t>(std::min<uint32_t>(static_cast<uint32_t>(counts.perHop[0]) * counts.denominator, UINT16_MAX));
    }
#else
    const uint16_t directActiveNodes = 0;
#endif
    return meshTooBusy(channelUtilPercent, airUtilTxPercent, directActiveNodes);
}

uint8_t RepeatScalingModule::getDupeCancelThreshold(const meshtastic_MeshPacket *p)
{
    const int32_t portnum = resolvePortnum(p);

    const uint8_t threshold = portnum >= 0 ? portnumThreshold(portnum) : headerThreshold(p);

    if (threshold > 1 && meshTooBusyForExtraRepeats()) {
        return 1;
    }

    return threshold;
}

uint8_t RepeatScalingModule::portnumThreshold(int32_t portnum)
{
    switch (portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
    case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
        return 2; // Chat has no broadcast ACK/retry
    default:
        return 1;
    }
}

uint8_t RepeatScalingModule::headerThreshold(const meshtastic_MeshPacket *p)
{
    // Portnum unknown: only a flooded DM gets the extra tolerance
    return (!isBroadcast(p->to) && p->next_hop == NO_NEXT_HOP_PREFERENCE) ? 2 : 1;
}

const RepeatScalingModule::DupeCountEntry *RepeatScalingModule::findEntry(NodeNum sender, PacketId id) const
{
    for (const auto &entry : dupeCounts) {
        if (entry.id == id && entry.sender == sender)
            return &entry;
    }
    return nullptr;
}

RepeatScalingModule::DupeCountEntry *RepeatScalingModule::findEntry(NodeNum sender, PacketId id)
{
    return const_cast<DupeCountEntry *>(static_cast<const RepeatScalingModule *>(this)->findEntry(sender, id));
}

RepeatScalingModule::DupeCountEntry &RepeatScalingModule::claimEntry(NodeNum sender, PacketId id)
{
    DupeCountEntry *slot = findEntry(0, 0); // a received packet's from is never 0, so (0, 0) only marks a free slot
    if (!slot) {
        slot = &dupeCounts[0];
        for (auto &entry : dupeCounts) {
            if (entry.claimedSeq < slot->claimedSeq)
                slot = &entry;
        }
    }
    *slot = DupeCountEntry{sender, id, 0, -1, ++claimSeq};
    return *slot;
}

uint8_t RepeatScalingModule::registerDupeHeard(NodeNum sender, PacketId id)
{
    DupeCountEntry *entry = findEntry(sender, id);
    if (!entry)
        entry = &claimEntry(sender, id);
    if (entry->count < UINT8_MAX)
        entry->count++;
    return entry->count;
}

void RepeatScalingModule::clearDupeCount(NodeNum sender, PacketId id)
{
    if (DupeCountEntry *entry = findEntry(sender, id))
        *entry = DupeCountEntry{};
}

void RepeatScalingModule::noteScheduled(const meshtastic_MeshPacket *p)
{
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return; // nothing to cache: a miss gives the same header guess
    const int32_t portnum = p->decoded.portnum;
    // A threshold-1 broadcast's miss is already right; caching such bulk relays would evict the rest
    if (isBroadcast(p->to) && portnumThreshold(portnum) == 1)
        return;
    noteScheduled(p->from, p->id, portnum);
}

void RepeatScalingModule::noteScheduled(NodeNum sender, PacketId id, int32_t portnum)
{
    DupeCountEntry *entry = findEntry(sender, id);
    if (entry)
        entry->claimedSeq = ++claimSeq; // now queued, so no longer the oldest claim to evict
    else
        entry = &claimEntry(sender, id);
    entry->portnum = portnum;
}

int32_t RepeatScalingModule::lookupNotedPortnum(NodeNum sender, PacketId id) const
{
    const DupeCountEntry *entry = findEntry(sender, id);
    return entry ? entry->portnum : -1;
}

int32_t RepeatScalingModule::resolvePortnum(const meshtastic_MeshPacket *p) const
{
    return (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) ? p->decoded.portnum
                                                                           : lookupNotedPortnum(p->from, p->id);
}

bool RepeatScalingModule::shouldCancelDupe(const meshtastic_MeshPacket *p)
{
    const uint8_t threshold = getDupeCancelThreshold(p);
    // Threshold 1 needs no slot
    const uint8_t dupesHeard = threshold <= 1 ? 1 : registerDupeHeard(p->from, p->id);
    const int32_t portnum = resolvePortnum(p);

    if (dupesHeard >= threshold) {
        LOG_INFO("[REPEATSCALE] drop own rebroadcast of 0x%08x port=%d: %u/%u dupes", p->id, portnum, dupesHeard, threshold);
        clearDupeCount(p->from, p->id);
        return true;
    }

    return false;
}
