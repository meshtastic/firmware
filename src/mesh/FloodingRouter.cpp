#include "FloodingRouter.h"
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
    wasSeenRecently(p); // FIXME, move this to a sniffSent method

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        printPacket("Ignoring incoming msg we've already seen", p);
        if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
            config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER) {
            // cancel rebroadcast of this message *if* there was already one, unless we're a router/repeater!
            Router::cancelSending(p->from, p->id);
        }
        return true;
    }

    return Router::shouldFilterReceived(p);
}

void FloodingRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) && (p->decoded.request_id != 0);
    if (isAckorReply && p->to != getNodeNum() && p->to != NODENUM_BROADCAST) {
        // do not flood direct message that is ACKed or replied to
        LOG_DEBUG("Receiving an ACK or reply not for me, but don't need to rebroadcast this direct message anymore.\n");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
    }
    if ((p->to != getNodeNum()) && (p->hop_limit > 0) && (getFrom(p) != getNodeNum())) {
        if (p->id != 0) {
            if (config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE) {
                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

                tosend->hop_limit--; // bump down the hop count

                LOG_INFO("Rebroadcasting received floodmsg to neighbors\n");
                // Note: we are careful to resend using the original senders node id
                // We are careful not to call our hooked version of send() - because we don't want to check this again
                Router::send(tosend);
            } else {
                LOG_DEBUG("Not rebroadcasting. Role = Role_ClientMute\n");
            }
        } else {
            LOG_DEBUG("Ignoring a simple (0 id) broadcast\n");
        }
    }
    // handle the packet as normal
    Router::sniffReceived(p, c);
}