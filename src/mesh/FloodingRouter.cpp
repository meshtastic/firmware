#include "FloodingRouter.h"
#include "../userPrefs.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

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
    /* Resend implicit ACKs for repeated packets (hopStart equals hopLimit);
     * this way if an implicit ACK is dropped and a packet is resent we'll rebroadcast again.
     * Resending real ACKs is omitted, as you might receive a packet multiple times due to flooding and
     * flooding this ACK back to the original sender already adds redundancy. */
    bool isRepeated = p->hop_start > 0 && (p->hop_start == p->hop_limit);
    bool didRebroadcast = false;
    if (wasSeenRecently(p, false) && isRepeated) {
        LOG_DEBUG("Repeated floodmsg");
        didRebroadcast = perhapsRebroadcast(p); // perhaps rebroadcast the packet
    }

    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        printPacket("Ignore dupe incoming msg", p);
        rxDupe++;
        if (!didRebroadcast) { // We shouldn't cancel a rebroadcast that we just did
            if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
                config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER) {
                // cancel rebroadcast of this message *if* there was already one, unless we're a router/repeater!
                if (Router::cancelSending(p->from, p->id))
                    txRelayCanceled++;
            }
        }
        return true;
    }

    return Router::shouldFilterReceived(p);
}

bool FloodingRouter::isRebroadcaster()
{
    return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
           config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
}

void FloodingRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) && (p->decoded.request_id != 0);
    if (isAckorReply && !isToUs(p) && !isBroadcast(p->to)) {
        // do not flood direct message that is ACKed or replied to
        LOG_DEBUG("Rxd an ACK/reply not for me, cancel rebroadcast");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
    }
    perhapsRebroadcast(p);

    // handle the packet as normal
    Router::sniffReceived(p, c);
}

bool FloodingRouter::perhapsRebroadcast(const meshtastic_MeshPacket *p)
{
    if (!isToUs(p) && (p->hop_limit > 0) && !isFromUs(p)) {
        if (p->id != 0) {
            if (isRebroadcaster()) {
                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

                tosend->hop_limit--; // bump down the hop count
#if USERPREFS_EVENT_MODE
                if (tosend->hop_limit > 2) {
                    // if we are "correcting" the hop_limit, "correct" the hop_start by the same amount to preserve hops away.
                    tosend->hop_start -= (tosend->hop_limit - 2);
                    tosend->hop_limit = 2;
                }
#endif

                LOG_INFO("Rebroadcast received floodmsg");
                // Note: we are careful to resend using the original senders node id
                // We are careful not to call our hooked version of send() - because we don't want to check this again
                Router::send(tosend);

                return true;
            } else {
                LOG_DEBUG("No rebroadcast: Role = CLIENT_MUTE or Rebroadcast Mode = NONE");
            }
        } else {
            LOG_DEBUG("Ignore 0 id broadcast");
        }
    }

    return false;
}