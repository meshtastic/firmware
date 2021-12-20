#include "configuration.h"
#include "DSRRouter.h"

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

ErrorCode DSRRouter::send(MeshPacket *p)
{
    // We only consider multihop routing packets (i.e. those with dest set)
    if (p->decoded.dest) {
        // add an entry for this pending message
        auto pending = startRetransmission(p);
        // FIXME - when acks come in for this packet, we should _not_ delete the record unless the ack was from
        // the final dest.  We need to keep that record around until FIXME
        // Also we should not retransmit multihop entries in that table at all

        // If we have an entry in our routing tables, just send it, otherwise start a route discovery
        NodeNum nextHop = getNextHop(p->decoded.dest);
        if (nextHop) {
            sendNextHop(nextHop, p); // start a reliable single hop send
        } else {
            pending->wantRoute = true;

            // start discovery, but only if we don't already a discovery in progress for that node number
            startDiscovery(p->decoded.dest);
        }

        return ERRNO_OK;
    } else
        return ReliableRouter::send(p);
}

void DSRRouter::sniffReceived(const MeshPacket *p, const Routing *c)
{
    // Learn 0 hop routes by just hearing any adjacent nodes
    // But treat broadcasts carefully, because when flood broadcasts go out they keep the same original "from".  So we want to
    // ignore rebroadcasts.
    // this will also add records for any ACKs we receive for our messages
    if (p->to != NODENUM_BROADCAST || p->hop_limit != HOP_RELIABLE) {
        addRoute(getFrom(p), getFrom(p), 0); // We are adjacent with zero hops
    }

    if (c)
        switch (c->which_variant) {
        case Routing_route_request_tag:
            // Handle route discovery packets (will be a broadcast message)
            // FIXME - always start request with the senders nodenum
            if (weAreInRoute(c->route_request)) {
                DEBUG_MSG("Ignoring a route request that contains us\n");
            } else {
                updateRoutes(c->route_request,
                             true); // Update our routing tables based on the route that came in so far on this request

                if (p->decoded.dest == getNodeNum()) {
                    // They were looking for us, send back a route reply (the sender address will be first in the list)
                    sendRouteReply(c->route_request);
                } else {
                    // They were looking for someone else, forward it along (as a zero hop broadcast)
                    NodeNum nextHop = getNextHop(p->decoded.dest);
                    if (nextHop) {
                        // in our route cache, reply to the requester (the sender address will be first in the list)
                        sendRouteReply(c->route_request, nextHop);
                    } else {
                        // Not in our route cache, rebroadcast on their behalf (after adding ourselves to the request route)
                        resendRouteRequest(p);
                    }
                }
            }
            break;
        case Routing_route_reply_tag:
            updateRoutes(c->route_reply, false);

            // FIXME, if any of our current pending packets were waiting for this route, send them (and leave them as regular
            // pending packets until ack arrives)
            // FIXME, if we don't get a route reply at all (or a route error), timeout and generate a routeerror TIMEOUT on our
            // own...
            break;
        case Routing_error_reason_tag:
            removeRoute(p->decoded.dest);

            // FIXME: if any pending packets were waiting on this route, delete them
            break;
        default:
            break;
        }

    // We simply ignore ACKs - because ReliableRouter will delete the pending packet for us

    // Handle regular packets
    if (p->to == getNodeNum()) { // Destined for us (at least for this hop)

        // We need to route this packet to some other node
        if (p->decoded.dest && p->decoded.dest != p->to) {
            // if we have a route out, resend the packet to the next hop, otherwise return RouteError no-route available

            NodeNum nextHop = getNextHop(p->decoded.dest);
            if (nextHop) {
                sendNextHop(nextHop, p); // start a reliable single hop send
            } else {
                // We don't have a route out
                assert(p->decoded.source); // I think this is guaranteed by now

                // FIXME - what if the current packet _is_ a route error packet?
                sendRouteError(p, Routing_Error_NO_ROUTE);
            }

            // FIXME, stop local processing of this packet
        }

        if (c) {
            // handle naks - convert them to route error packets
            // All naks are generated locally, because we failed resending the packet too many times
            PacketId nakId = c->error_reason ? p->decoded.request_id : 0;
            if (nakId) {
                auto pending = findPendingPacket(p->to, nakId);
                if (pending &&
                    pending->packet->decoded.source) {          // if source not set, this was not a multihop packet, just ignore
                    removeRoute(pending->packet->decoded.dest); // We no longer have a route to the specified node

                    sendRouteError(p, Routing_Error_GOT_NAK);
                }
            }
        }
    }

    ReliableRouter::sniffReceived(p, c);
}

/**
 * Does our node appear in the specified route
 */
bool DSRRouter::weAreInRoute(const RouteDiscovery &route)
{
    return true; // FIXME
}

/**
 * Given a DSR route, use that route to update our DB of possible routes
 *
 * Note: routes are always listed in the same order - from sender to receipient (i.e. route_replies also use this some order)
 *
 * @param isRequest is true if we are looking at a route request, else we are looking at a reply
 **/
void DSRRouter::updateRoutes(const RouteDiscovery &route, bool isRequest)
{
    DEBUG_MSG("FIXME not implemented updateRoutes\n");
}

/**
 * send back a route reply (the sender address will be first in the list)
 */
void DSRRouter::sendRouteReply(const RouteDiscovery &route, NodeNum toAppend)
{
    DEBUG_MSG("FIXME not implemented sendRoute\n");
}

/**
 * Given a nodenum return the next node we should forward to if we want to reach that node.
 *
 * @return 0 if no route found
 */
NodeNum DSRRouter::getNextHop(NodeNum dest)
{
    DEBUG_MSG("FIXME not implemented getNextHop\n");
    return 0;
}

/** Not in our route cache, rebroadcast on their behalf (after adding ourselves to the request route)
 *
 * We will bump down hop_limit in this call.
 */
void DSRRouter::resendRouteRequest(const MeshPacket *p)
{
    DEBUG_MSG("FIXME not implemented resendRoute\n");
}

/**
 * Record that forwarder can reach dest for us, but they will need numHops to get there.
 * If our routing tables already have something that can reach that node in fewer hops we will keep the existing route
 * instead.
 */
void DSRRouter::addRoute(NodeNum dest, NodeNum forwarder, uint8_t numHops)
{
    DEBUG_MSG("FIXME not implemented addRoute\n");
}

/**
 * Record that we no longer have a route to the dest
 */
void DSRRouter::removeRoute(NodeNum dest)
{
    DEBUG_MSG("FIXME not implemented removeRoute\n");
}

/**
 * Forward the specified packet to the specified node
 */
void DSRRouter::sendNextHop(NodeNum n, const MeshPacket *p)
{
    DEBUG_MSG("FIXME not implemented sendNextHop\n");
}

/**
 * Send a route error packet towards whoever originally sent this message
 */
void DSRRouter::sendRouteError(const MeshPacket *p, Routing_Error err)
{
    DEBUG_MSG("FIXME not implemented sendRouteError\n");
}

/** make a copy of p, start discovery, but only if we don't
 *  already a discovery in progress for that node number.  Caller has already scheduled this message for retransmission
 *  when the discovery is complete.
 */
void DSRRouter::startDiscovery(NodeNum dest)
{
    DEBUG_MSG("FIXME not implemented startDiscovery\n");
}