#include "FloodingRouter.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "airtime.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/HopScalingModule.h"
#include "modules/TextMessageModule.h"
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
#include "modules/TraceRouteModule.h"
#endif

namespace
{
// Above any of these, the mesh is busy/dense enough that extra-repeat tolerance (see
// getDupeCancelThreshold) would just be adding airtime to an already-congested channel for
// little extra delivery benefit - so we fall back to the historical "cancel on first duplicate"
// behavior regardless of what the portnum table says.
constexpr float BUSY_CHANNEL_UTIL_PERCENT = 10.0f;
constexpr float BUSY_AIR_UTIL_TX_PERCENT = 4.0f;
constexpr uint16_t BUSY_DIRECT_ACTIVE_NODES = 10;

// True if channel/air utilization or estimated direct-neighbor density indicates the mesh is
// too busy to spend extra airtime on repeat-tolerant rebroadcasts.
bool meshTooBusyForExtraRepeats()
{
    if (airTime && airTime->channelUtilizationPercent() > BUSY_CHANNEL_UTIL_PERCENT)
        return true;
    if (airTime && airTime->utilizationTXPercent() > BUSY_AIR_UTIL_TX_PERCENT)
        return true;
#if HAS_VARIABLE_HOPS
    // getLastPerHopCounts().perHop[0] is HopScalingModule's estimate of active (heard within its
    // rolling window) direct (hop_away == 0) neighbors - the same "active and direct" notion used
    // for its own hop-limit recommendation.
    if (hopScalingModule && hopScalingModule->getLastPerHopCounts().perHop[0] > BUSY_DIRECT_ACTIVE_NODES)
        return true;
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
// calls this same (inherited, non-overridden) getDupeCancelThreshold() when it hears a duplicate
// it wasn't explicitly asked to relay - both in the fallback-to-flooding case and the general
// "duplicate heard, we're not the assigned next hop" case - so a DM of a listed portnum gets the
// same extra tolerance as a broadcast of that portnum, with no separate to/from-based gating needed.
//
// This is a compile-time table (not a runtime/protobuf config) because the desired threshold is
// expected to vary per packet type rather than being a single device-wide knob; edit it to tune
// for a given deployment.
//
// Regardless of the table below, meshTooBusyForExtraRepeats() (channel/air utilization or direct-
// neighbor density too high) always forces the threshold back down to 1 - see its definition above.
uint8_t FloodingRouter::getDupeCancelThreshold(const meshtastic_MeshPacket *p)
{
    // Portnum is only visible once a packet is decoded (i.e. we hold the channel key); packets we
    // can't decrypt always get the default of 1, since we have no per-portnum signal to act on.
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return 1;

    uint8_t threshold;
    switch (p->decoded.portnum) {
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
    if (threshold > 1 && meshTooBusyForExtraRepeats())
        return 1;

    return threshold;
}

uint8_t FloodingRouter::registerDupeHeard(NodeNum sender, PacketId id)
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
    return slot.count;
}

void FloodingRouter::clearDupeCount(NodeNum sender, PacketId id)
{
    for (auto &entry : dupeCounts) {
        if (entry.id == id && entry.sender == sender) {
            entry.sender = 0;
            entry.id = 0;
            entry.count = 0;
            return;
        }
    }
}

FloodingRouter::FloodingRouter() {}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // First set the relayer to us
    wasSeenRecently(p);                                         // FIXME, move this to a sniffSent method

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    bool wasUpgraded = false;
    bool seenRecently =
        wasSeenRecently(p, true, nullptr, nullptr, &wasUpgraded); // Updates history; returns false when an upgrade is detected

    // Handle hop_limit upgrade scenario for rebroadcasters
    if (wasUpgraded && perhapsHandleUpgradedPacket(p)) {
        return true; // we handled it, so stop processing
    }

    if (!seenRecently && !wasUpgraded && textMessageModule) {
        seenRecently = textMessageModule->recentlySeen(p->id);
    }

    if (seenRecently) {
        printPacket("Ignore dupe incoming msg", p);
        rxDupe++;

        /* If the original transmitter is doing retransmissions (hopStart equals hopLimit) for a reliable transmission, e.g., when
        the ACK got lost, we will handle the packet again to make sure it gets an implicit ACK. */
        bool isRepeated = p->hop_start > 0 && p->hop_start == p->hop_limit;
        if (isRepeated) {
            LOG_DEBUG("Repeated reliable tx");
            // Check if it's still in the Tx queue, if not, we have to relay it again
            if (!findInTxQueue(p->from, p->id)) {
                reprocessPacket(p);
                perhapsRebroadcast(p);
            }
        } else {
            perhapsCancelDupe(p);
        }

        return true;
    }

    return Router::shouldFilterReceived(p);
}

bool FloodingRouter::perhapsHandleUpgradedPacket(const meshtastic_MeshPacket *p)
{
    // isRebroadcaster() is duplicated in perhapsRebroadcast(), but this avoids confusing log messages
    if (isRebroadcaster() && iface && p->hop_limit > 0) {
        // If we overhear a duplicate copy of the packet with more hops left than the one we are waiting to
        // rebroadcast, then remove the packet currently sitting in the TX queue and use this one instead.
        uint8_t dropThreshold = p->hop_limit; // remove queued packets that have fewer hops remaining
        if (iface->removePendingTXPacket(getFrom(p), p->id, dropThreshold)) {
            LOG_DEBUG("Processing upgraded packet 0x%08x for rebroadcast with hop limit %d (dropping queued < %d)", p->id,
                      p->hop_limit, dropThreshold);

            reprocessPacket(p);
            perhapsRebroadcast(p);

            rxDupe++;
            // We already enqueued the improved copy, so make sure the incoming packet stops here.
            return true;
        }
    }

    return false;
}

void FloodingRouter::reprocessPacket(const meshtastic_MeshPacket *p)
{
    if (nodeDB)
        nodeDB->updateFrom(*p);

#if !MESHTASTIC_EXCLUDE_TRACEROUTE
    if (traceRouteModule && p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        // If we got a packet that is not decoded, try to decode it so we can check for traceroute.
        auto decodedState = perhapsDecode(const_cast<meshtastic_MeshPacket *>(p));
        if (decodedState == DecodeState::DECODE_SUCCESS) {
            // parsing was successful, print for debugging
            printPacket("reprocessPacket(DUP)", p);
        } else {
            // Fatal decoding error, we can't do anything with this packet
            LOG_WARN(
                "FloodingRouter::reprocessPacket: Fatal decode error (state=%d, id=0x%08x, from=%u), can't check for traceroute",
                static_cast<int>(decodedState), p->id, getFrom(p));
            return;
        }
    }

    if (traceRouteModule && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        p->decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP) {
        traceRouteModule->processUpgradedPacket(*p);
    }
#endif
}

bool FloodingRouter::roleAllowsCancelingDupe(const meshtastic_MeshPacket *p)
{
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE) {
        // ROUTER, ROUTER_LATE should never cancel relaying a packet (i.e. we should always rebroadcast),
        // even if we've heard another station rebroadcast it already.
        return false;
    }

    if (config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE) {
        // CLIENT_BASE: if the packet is from or to a favorited node,
        // we should act like a ROUTER and should never cancel a rebroadcast (i.e. we should always rebroadcast),
        // even if we've heard another station rebroadcast it already.
        return !nodeDB->isFromOrToFavoritedNode(*p);
    }

    // All other roles (such as CLIENT) should cancel a rebroadcast if they hear another station's rebroadcast.
    return true;
}

void FloodingRouter::perhapsCancelDupe(const meshtastic_MeshPacket *p)
{
    if (p->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA && roleAllowsCancelingDupe(p)) {
        // Wait for the configured number of duplicate rebroadcasts (see dupeCancelThresholds) before
        // giving up on our own rebroadcast; below that, note the duplicate and keep waiting.
        uint8_t dupesHeard = registerDupeHeard(p->from, p->id);
        if (dupesHeard >= getDupeCancelThreshold(p)) {
            // cancel rebroadcast of this message *if* there was already one, unless we're a router!
            // But only LoRa packets should be able to trigger this.
            if (Router::cancelSending(p->from, p->id))
                txRelayCanceled++;
            clearDupeCount(p->from, p->id);
        }
    }
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE && iface) {
        iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
    }
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE && iface && nodeDB &&
        nodeDB->isFromOrToFavoritedNode(*p)) {
        iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
    }
}

bool FloodingRouter::isRebroadcaster()
{
    return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
           config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
}

void FloodingRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) &&
                        (p->decoded.request_id != 0 || p->decoded.reply_id != 0);
    if (isAckorReply && !isToUs(p) && !isBroadcast(p->to)) {
        // do not flood direct message that is ACKed or replied to
        LOG_DEBUG("Rxd an ACK/reply not for me, cancel rebroadcast");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
    }

    perhapsRebroadcast(p);

    // handle the packet as normal
    Router::sniffReceived(p, c);
}
