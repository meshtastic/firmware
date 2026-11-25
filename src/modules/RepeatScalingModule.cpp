#include "RepeatScalingModule.h"
#include "DebugConfiguration.h"
#include "airtime.h"
#include "configuration.h"
#include "modules/HopScalingModule.h"

RepeatScalingModule *repeatScalingModule;
static bool busy(const char *which)
{
    LOG_DEBUG("[REPEATSCALE] mesh busy: %s", which);
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
    // perHop[0]: active direct neighbours
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
    const uint16_t directActiveNodes = hopScalingModule ? hopScalingModule->getLastPerHopCounts().perHop[0] : 0;
#else
    const uint16_t directActiveNodes = 0;
#endif
    return meshTooBusy(channelUtilPercent, airUtilTxPercent, directActiveNodes);
}

uint8_t RepeatScalingModule::getDupeCancelThreshold(const meshtastic_MeshPacket *p)
{
    const int32_t portnum = resolvePortnum(p);

    uint8_t threshold;
    if (portnum >= 0) {
        switch (portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
            // Chat has no broadcast ACK/retry
            threshold = 2;
            break;
        default:
            threshold = 1;
            break;
        }
    } else {
        // Undecodable: only a flooded DM, judged from the plaintext header
        threshold = (!isBroadcast(p->to) && p->next_hop == NO_NEXT_HOP_PREFERENCE) ? 2 : 1;
    }

    if (threshold > 1 && meshTooBusyForExtraRepeats()) {
        return 1;
    }

    return threshold;
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
    DupeCountEntry *slot = findEntry(0, 0);
    if (!slot) {
        slot = &dupeCounts[dupeCountsNextSlot];
        dupeCountsNextSlot = (dupeCountsNextSlot + 1) % DUPE_COUNT_TRACKER_SIZE;
    }
    *slot = DupeCountEntry{sender, id, 0, -1};
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

void RepeatScalingModule::noteScheduled(NodeNum sender, PacketId id, int32_t portnum)
{
    DupeCountEntry *entry = findEntry(sender, id);
    if (!entry)
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
