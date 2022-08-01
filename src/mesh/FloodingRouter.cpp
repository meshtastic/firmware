#include "FloodingRouter.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

FloodingRouter::FloodingRouter() {}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    wasSeenRecently(p); // FIXME, move this to a sniffSent method

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        printPacket("Ignoring incoming msg, because we've already seen it", p);
        return true;
    }

    return Router::shouldFilterReceived(p);
}

void FloodingRouter::sniffReceived(const MeshPacket *p, const Routing *c)
{
    PacketId ackId = ((c && c->error_reason == Routing_Error_NONE) || !c) ? p->decoded.request_id : 0;
    if (ackId && p->to != getNodeNum()) {
        // do not flood direct message that is ACKed 
        DEBUG_MSG("Receiving an ACK not for me, but don't need to rebroadcast this direct message anymore.\n");
        Router::cancelSending(p->to, p->decoded.request_id);   // cancel rebroadcast for this DM
    } else if ((p->to != getNodeNum()) && (p->hop_limit > 0) && (getFrom(p) != getNodeNum())) {
        if (p->id != 0) {
            if (config.device.role != Config_DeviceConfig_Role_ClientMute) {
                MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

                tosend->hop_limit--; // bump down the hop count

                printPacket("Rebroadcasting received floodmsg to neighbors", p);
                // Note: we are careful to resend using the original senders node id
                // We are careful not to call our hooked version of send() - because we don't want to check this again
                Router::send(tosend);

            } else {
                DEBUG_MSG("Not rebroadcasting. Role = Role_ClientMute\n");
            }

        } else {
            DEBUG_MSG("Ignoring a simple (0 id) broadcast\n");
        }
    }

    // handle the packet as normal
    Router::sniffReceived(p, c);
}
