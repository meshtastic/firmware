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
    if (p->decoded.which_payload == SubPacket_route_request_tag) {
        // FIXME - always start request with the senders nodenum

        if (weAreInRoute(p->decoded.route_request)) {
            DEBUG_MSG("Ignoring a route request that contains us\n");
        } else {
            updateRoutes(p->decoded.route_request,
                         false); // Update our routing tables based on the route that came in so far on this request

            if (p->decoded.dest == getNodeNum()) {
                // They were looking for us, send back a route reply (the sender address will be first in the list)
                sendRouteReply(p->decoded.route_request);
            } else {
                // They were looking for someone else, forward it along (as a zero hop broadcast)
                NodeNum nextHop = getNextHop(p->decoded.dest);
                if (nextHop) {
                    // in our route cache, reply to the requester (the sender address will be first in the list)
                    sendRouteReply(p->decoded.route_request, nextHop);
                } else {
                    // Not in our route cache, rebroadcast on their behalf (after adding ourselves to the request route)
                    resendRouteRequest(p);
                }
            }
        }
    }

    // Handle route reply packets
    if (p->decoded.which_payload == SubPacket_route_reply_tag) {
        updateRoutes(p->decoded.route_reply, true);
    }

    // Handle route error packets
    if (p->decoded.which_payload == SubPacket_route_error_tag) {
        // FIXME
    }

    // Learn 0 hop routes by just hearing any adjacent nodes
    // But treat broadcasts carefully, because when flood broadcasts go out they keep the same original "from".  So we want to
    // ignore rebroadcasts.
    if (p->to != NODENUM_BROADCAST || p->hop_limit != HOP_RELIABLE) {
        addRoute(p->from, p->from, 0); // We are adjacent with zero hops
    }

    // We simply ignore ACKs - because ReliableRouter will delete the pending packet for us

    // Handle regular packets
    if (p->to == getNodeNum()) { // Destined for us (at least for this hop)

        // We need to route this packet to some other node
        if (p->decoded.dest && p->decoded.dest != p->to) {
            // FIXME if we have a route out, resend the packet to the next hop, otherwise return RouteError no-route available

            NodeNum nextHop = getNextHop(p->decoded.dest);
            if (nextHop) {
                sendNextHop(nextHop, p); // start a reliable single hop send
            } else {
                // We don't have a route out
                assert(p->decoded.source); // I think this is guaranteed by now

                sendRouteError(p, RouteError_NO_ROUTE);
            }

            // FIXME, stop local processing of this packet
        }

        // handle naks - convert them to route error packets
        // All naks are generated locally, because we failed resending the packet too many times
        PacketId nakId = p->decoded.which_ack == SubPacket_fail_id_tag ? p->decoded.ack.fail_id : 0;
        if (nakId) {
            auto pending = findPendingPacket(p->to, nakId);
            if (pending && pending->packet->decoded.source) { // if source not set, this was not a multihop packet, just ignore
                removeRoute(pending->packet->decoded.dest, p->to); // We no longer have a route to the specified node

                sendRouteError(p, RouteError_GOT_NAK);
            }
        }
    }

    return ReliableRouter::sniffReceived(p);
}