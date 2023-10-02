#include "NextHopRouter.h"

NextHopRouter::NextHopRouter() {}

/**
 * Send a packet
 */
ErrorCode NextHopRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    wasSeenRecently(p); // FIXME, move this to a sniffSent method

    p->next_hop = getNextHop(p->to, p->current_relayer); // set the next hop
    LOG_DEBUG("Setting next hop for packet with dest %x to %x\n", p->to, p->next_hop);

    return Router::send(p);
}

bool NextHopRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        if (p->next_hop == getNodeNum()) {
            LOG_DEBUG("Ignoring incoming msg, because we've already seen it.\n");
        } else {
            LOG_DEBUG("Ignoring incoming msg, because we've already seen it and cancel any outgoing packets.\n");
            Router::cancelSending(p->from, p->id);
        }
        return true;
    }

    return Router::shouldFilterReceived(p);
}

void NextHopRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAck =
        ((c && c->error_reason == meshtastic_Routing_Error_NONE)); // consider only ROUTING_APP message without error as ACK
    if (isAck) {
        // Update next-hop of this successful transmission to current relayer, but ONLY if "from" is not 0 or ourselves (means
        // implicit ACK or someone is relaying our ACK)
        if (p->from != 0 && p->from != getNodeNum()) {
            if (p->current_relayer) {
                meshtastic_NodeInfoLite *sentTo = nodeDB.getMeshNode(p->from);
                if (sentTo) {
                    LOG_DEBUG("Update next hop of %x to %x based on received ACK.\n", p->from, p->current_relayer);
                    sentTo->next_hop = p->current_relayer;
                }
            }
        }
    }

    if (config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE) {
        if ((p->to != getNodeNum()) && (getFrom(p) != getNodeNum())) {
            if (p->next_hop == getNodeNum()) {
                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it
                LOG_INFO("Relaying received next-hop message coming from %x\n", p->current_relayer);

                if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                    // If it is a traceRoute request, update the route that it went via me
                    if (traceRouteModule && traceRouteModule->wantPacket(tosend))
                        traceRouteModule->updateRoute(tosend);
                    // If it is a neighborInfo packet, update last_sent_by_id
                    if (neighborInfoModule && neighborInfoModule->wantPacket(tosend))
                        neighborInfoModule->updateLastSentById(tosend);
                }

                tosend->hop_limit--; // bump down the hop count
                NextHopRouter::send(tosend);
            } else if (p->next_hop == 0) {
                // No preference for next hop, use FloodingRouter
                LOG_DEBUG("No preference for next hop, using FloodingRouter\n");
                FloodingRouter::sniffReceived(p, c);
            } else if (p->to == NODENUM_BROADCAST) {
                // TODO how to handle broadcast messages?
                LOG_DEBUG("TODO: Broadcast next-hop message\n");
                FloodingRouter::sniffReceived(p, c);
            }
        }
    } else {
        LOG_DEBUG("Not rebroadcasting. Role = Role_ClientMute\n");
    }
    // handle the packet as normal
    Router::sniffReceived(p, c);
}

/**
 * Get the next hop for a destination, given the current relayer
 * @return the node number of the next hop, 0 if no preference (fallback to FloodingRouter)
 */
uint32_t NextHopRouter::getNextHop(NodeNum to, NodeNum current_relayer)
{
    meshtastic_NodeInfoLite *node = nodeDB.getMeshNode(to);
    if (node) {
        // We are careful not to return the current relayer as the next hop
        if (node->next_hop != current_relayer) {
            LOG_DEBUG("Next hop for %x is %x\n", to, node->next_hop);
            return node->next_hop;
        } else {
            LOG_WARN("Next hop for %x is %x, which is the same as current relayer; setting as no preference\n", to,
                     node->next_hop);
            return 0;
        }
    } else {
        return 0;
    }
}