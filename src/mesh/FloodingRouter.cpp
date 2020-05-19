#include "FloodingRouter.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

FloodingRouter::FloodingRouter() : toResend(MAX_NUM_NODES) {}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(MeshPacket *p)
{
    wasSeenRecently(p);

    return Router::send(p);
}

/**
 * Called from loop()
 * Handle any packet that is received by an interface on this node.
 * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
 *
 * Note: this method will free the provided packet
 */
void FloodingRouter::handleReceived(MeshPacket *p)
{
    if (wasSeenRecently(p)) {
        DEBUG_MSG("Ignoring incoming msg, because we've already seen it\n");
        packetPool.release(p);
    } else {
        // If a broadcast, possibly _also_ send copies out into the mesh. (FIXME, do something smarter than naive flooding here)
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
        Router::handleReceived(p);
    }
}
