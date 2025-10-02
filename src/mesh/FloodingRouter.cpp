#include "FloodingRouter.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
#include "modules/TraceRouteModule.h"
#endif

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
    // isRebroadcaster() is duplicated in perhapsRebroadcast(), but this avoids confusing log messages
    if (wasUpgraded && isRebroadcaster() && iface && p->hop_limit > 0) {
        // wasSeenRecently() reports false in upgrade cases so we handle replacement before the duplicate short-circuit
        // If we overhear a duplicate copy of the packet with more hops left than the one we are waiting to
        // rebroadcast, then remove the packet currently sitting in the TX queue and use this one instead.
        uint8_t dropThreshold = p->hop_limit; // remove queued packets that have fewer hops remaining
        if (iface->removePendingTXPacket(getFrom(p), p->id, dropThreshold)) {
            LOG_DEBUG("Processing upgraded packet 0x%08x for rebroadcast with hop limit %d (dropping queued < %d)", p->id,
                      p->hop_limit, dropThreshold);

            if (nodeDB)
                nodeDB->updateFrom(*p);
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
            if (traceRouteModule && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
                p->decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP)
                traceRouteModule->processUpgradedPacket(*p);
#endif

            perhapsRebroadcast(p);

            // We already enqueued the improved copy, so make sure the incoming packet stops here.
            return true;
        }

        // No queue entry was replaced by this upgraded copy, so treat it as a duplicate to avoid
        // delivering the same packet to applications/phone twice with different hop limits.
        seenRecently = true;
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
            if (!findInTxQueue(p->from, p->id))
                perhapsRebroadcast(p);
        } else {
            perhapsCancelDupe(p);
        }

        return true;
    }

    return Router::shouldFilterReceived(p);
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
        // cancel rebroadcast of this message *if* there was already one, unless we're a router!
        // But only LoRa packets should be able to trigger this.
        if (Router::cancelSending(p->from, p->id))
            txRelayCanceled++;
    }
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE && iface) {
        iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
    }
}

bool FloodingRouter::isRebroadcaster()
{
    return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
           config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
}

void FloodingRouter::perhapsRebroadcast(const meshtastic_MeshPacket *p)
{
    if (!isToUs(p) && (p->hop_limit > 0) && !isFromUs(p)) {
        if (p->id != 0) {
            if (isRebroadcaster()) {
                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

                // Use shared logic to determine if hop_limit should be decremented
                if (shouldDecrementHopLimit(p)) {
                    tosend->hop_limit--; // bump down the hop count
                } else {
                    LOG_INFO("favorite-ROUTER/CLIENT_BASE-to-ROUTER/CLIENT_BASE flood: preserving hop_limit");
                }
#if USERPREFS_EVENT_MODE
                if (tosend->hop_limit > 2) {
                    // if we are "correcting" the hop_limit, "correct" the hop_start by the same amount to preserve hops away.
                    tosend->hop_start -= (tosend->hop_limit - 2);
                    tosend->hop_limit = 2;
                }
#endif

                tosend->next_hop = NO_NEXT_HOP_PREFERENCE; // this should already be the case, but just in case

                LOG_INFO("Rebroadcast received floodmsg");
                // Note: we are careful to resend using the original senders node id
                send(tosend);
            } else {
                LOG_DEBUG("No rebroadcast: Role = CLIENT_MUTE or Rebroadcast Mode = NONE");
            }
        } else {
            LOG_DEBUG("Ignore 0 id broadcast");
        }
    }
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
