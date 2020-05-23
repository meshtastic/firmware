#include "DSRRouter.h"
#include "configuration.h"

/* when we receive any packet

- sniff and update tables (especially useful to find adjacent nodes). Update user, network and position info.
- if we need to route() that packet, resend it to the next_hop based on our nodedb.
- if it is broadcast or destined for our node, deliver locally
- handle routereply/routeerror/routediscovery messages as described below
- then free it

routeDiscovery

- if we've already passed through us (or is from us), then it ignore it
- use the nodes already mentioned in the request to update our routing table
- if they were looking for us, send back a routereply
- if max_hops is zero and they weren't looking for us, drop (FIXME, send back error - I think not though?)
- if we receive a discovery packet, we use it to populate next_hop (if needed) towards the requester (after decrementing max_hops)
- if we receive a discovery packet, and we have a next_hop in our nodedb for that destination we send a (reliable) we send a route
reply towards the requester

when sending any reliable packet

- if timeout doing retries, send a routeError (nak) message back towards the original requester. all nodes eavesdrop on that
packet and update their route caches.

when we receive a routereply packet

- update next_hop on the node, if the new reply needs fewer hops than the existing one (we prefer shorter paths). fixme, someday
use a better heuristic

when we receive a routeError packet

- delete the route for that failed recipient, restartRouteDiscovery()
- if we receive routeerror in response to a discovery,
- fixme, eventually keep caches of possible other routes.
*/

void DSRRouter::sniffReceived(const MeshPacket *p)
{

    // FIXME, update nodedb

    // Handle route discovery packets (will be a broadcast message)
    if (p->decoded.which_payload == SubPacket_request_tag) {
        // FIXME - always start request with the senders nodenum

        if (weAreInRoute(p->decoded.request)) {
            DEBUG_MSG("Ignoring a route request that contains us\n");
        } else {
            updateRoutes(p->decoded.request,
                         false); // Update our routing tables based on the route that came in so far on this request

            if (p->decoded.dest == getNodeNum()) {
                // They were looking for us, send back a route reply (the sender address will be first in the list)
                sendRouteReply(p->decoded.request);
            } else {
                // They were looking for someone else, forward it along (as a zero hop broadcast)
                NodeNum nextHop = getNextHop(p->decoded.dest);
                if (nextHop) {
                    // in our route cache, reply to the requester (the sender address will be first in the list)
                    sendRouteReply(p->decoded.request, nextHop);
                } else {
                    // Not in our route cache, rebroadcast on their behalf (after adding ourselves to the request route)
                    resendRouteRequest(p);
                }
            }
        }
    }

    // Handle route reply packets
    if (p->decoded.which_payload == SubPacket_reply_tag) {
        updateRoutes(p->decoded.reply, true);
    }

    // Learn 0 hop routes by just hearing any adjacent nodes
    // But treat broadcasts carefully, because when flood broadcasts go out they keep the same original "from".  So we want to
    // ignore rebroadcasts.
    if (p->to != NODENUM_BROADCAST || p->hop_limit != HOP_RELIABLE) {
        setRoute(p->from, p->from, 0); // We are adjacent with zero hops
    }

    // FIXME - handle any naks we receive (either because they are passing by us or someone naked a message we sent)

    // Handle regular packets
    if (p->to == getNodeNum()) { // Destined for us (at least for this hop)

        // We need to route this packet to some other node
        if (p->decoded.dest && p->decoded.dest != p->to) {
            // FIXME if we have a route out, resend the packet to the next hop, otherwise return a nak with no-route available
        }

        // FIXME - handle naks from our adjacent nodes - convert them to route error packets
    }

    return ReliableRouter::sniffReceived(p);
}