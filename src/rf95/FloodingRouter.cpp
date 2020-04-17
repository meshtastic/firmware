#include "FloodingRouter.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

/// We clear our old flood record five minute after we see the last of it
#define FLOOD_EXPIRE_TIME (5 * 60 * 1000L)

FloodingRouter::FloodingRouter()
{
    recentBroadcasts.reserve(MAX_NUM_NODES); // Prealloc the worst case # of records - to prevent heap fragmentation
}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(MeshPacket *p)
{
    // We update our table of recent broadcasts, even for messages we send
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
        DEBUG_MSG("Ignoring incoming floodmsg, because we've already seen it\n");
        packetPool.release(p);
    } else {
        if (p->to == NODENUM_BROADCAST) {
            if (p->id != 0) {
                DEBUG_MSG("Rebroadcasting received floodmsg to neighbors fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);
                // FIXME, wait a random delay

                MeshPacket *tosend = packetPool.allocCopy(*p);
                // Note: we are careful to resend using the original senders node id
                Router::send(tosend); // We are careful not to call our hooked version of send()
            } else {
                DEBUG_MSG("Ignoring a simple (0 hop) broadcast\n");
            }
        }

        // handle the packet as normal
        Router::handleReceived(p);
    }
}

/**
 * Update recentBroadcasts and return true if we have already seen this packet
 */
bool FloodingRouter::wasSeenRecently(const MeshPacket *p)
{
    if (p->to != NODENUM_BROADCAST)
        return false; // Not a broadcast, so we don't care

    if (p->id == 0) {
        DEBUG_MSG("Ignoring message with zero id\n");
        return false; // Not a floodable message ID, so we don't care
    }

    uint32_t now = millis();
    for (int i = 0; i < recentBroadcasts.size();) {
        BroadcastRecord &r = recentBroadcasts[i];

        if ((now - r.rxTimeMsec) >= FLOOD_EXPIRE_TIME) {
            DEBUG_MSG("Deleting old broadcast record %d\n", i);
            recentBroadcasts.erase(recentBroadcasts.begin() + i); // delete old record
        } else {
            if (r.id == p->id && r.sender == p->from) {
                DEBUG_MSG("Found existing broadcast record for fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);

                // Update the time on this record to now
                r.rxTimeMsec = now;
                return true;
            }

            i++;
        }
    }

    // Didn't find an existing record, make one
    BroadcastRecord r;
    r.id = p->id;
    r.sender = p->from;
    r.rxTimeMsec = now;
    recentBroadcasts.push_back(r);
    DEBUG_MSG("Adding broadcast record for fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);

    return false;
}