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

bool FloodingRouter::shouldFilterReceived(const MeshPacket *p)
{
    if (wasSeenRecently(p)) {
        DEBUG_MSG("Ignoring incoming msg, because we've already seen it\n");
        return true;
    }

    return Router::shouldFilterReceived(p);
}

void FloodingRouter::sniffReceived(const MeshPacket *p)
{
    // If a broadcast, possibly _also_ send copies out into the mesh.
    // (FIXME, do something smarter than naive flooding here)
    if (p->to == NODENUM_BROADCAST && p->hop_limit > 0) {
        if (p->id != 0) {
            MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

            tosend->hop_limit--; // bump down the hop count

            DEBUG_MSG("Rebroadcasting received floodmsg to neighbors, fr=0x%x,to=0x%x,id=%d,hop_limit=%d\n", p->from, p->to,
                      p->id, tosend->hop_limit);
            // Note: we are careful to resend using the original senders node id
            // We are careful not to call our hooked version of send() - because we don't want to check this again
            Router::send(tosend);

        } else {
            DEBUG_MSG("Ignoring a simple (0 id) broadcast\n");
        }
    }

    // handle the packet as normal
    Router::sniffReceived(p);
}
