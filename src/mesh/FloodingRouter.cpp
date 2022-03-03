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

bool FloodingRouter::inRangeOfRouter()
{

    uint32_t maximum_router_sec = 300;

    // FIXME : Scale minimum_snr to accomodate different modem configurations.
    float minimum_snr = 2;

    for (int i = 0; i < myNodeInfo.router_count; i++) {
        // A router has been seen and the heartbeat was heard within the last 300 seconds
        if (
            ((myNodeInfo.router_sec[i] > 0) && (myNodeInfo.router_sec[i] < maximum_router_sec)) &&
            (myNodeInfo.router_snr[i] > minimum_snr)
            ) {
            return true;
        }
    }

    return false;
}

void FloodingRouter::sniffReceived(const MeshPacket *p, const Routing *c)
{
    bool rebroadcastPacket = true;

    if (radioConfig.preferences.role == Role_Repeater || radioConfig.preferences.role == Role_Router) {
        rebroadcastPacket = true;

    } else if ((radioConfig.preferences.role == Role_Default) && inRangeOfRouter()) {
        DEBUG_MSG("Role_Default - rx_snr > 13\n");

        rebroadcastPacket = false;
    }

    if ((p->to == NODENUM_BROADCAST) && (p->hop_limit > 0) && (getFrom(p) != getNodeNum() && rebroadcastPacket)) {
        if (p->id != 0) {
            MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

            tosend->hop_limit--; // bump down the hop count

            printPacket("Rebroadcasting received floodmsg to neighbors", p);
            // Note: we are careful to resend using the original senders node id
            // We are careful not to call our hooked version of send() - because we don't want to check this again
            Router::send(tosend);

        } else {
            DEBUG_MSG("Ignoring a simple (0 id) broadcast\n");
        }
    }

    // handle the packet as normal
    Router::sniffReceived(p, c);
}
