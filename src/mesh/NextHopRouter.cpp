#include "NextHopRouter.h"

NextHopRouter::NextHopRouter() {}

/**
 * Send a packet
 */
ErrorCode NextHopRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    wasSeenRecently(p); // FIXME, move this to a sniffSent method

    p->next_hop = getNextHop(p->to, p->relay_node); // set the next hop
    LOG_DEBUG("Setting next hop for packet with dest %x to %x\n", p->to, p->next_hop);

    return Router::send(p);
}

bool NextHopRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        if (p->next_hop == nodeDB->getLastByteOfNodeNum(getNodeNum())) {
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
    NodeNum ourNodeNum = getNodeNum();
    bool isAck =
        ((c && c->error_reason == meshtastic_Routing_Error_NONE)); // consider only ROUTING_APP message without error as ACK
    if (isAck || (p->to == ourNodeNum)) {
        // Update next-hop for the original transmitter of this successful transmission to the relay node, but ONLY if "from" is
        // not 0 or ourselves (means implicit ACK or someone is relaying our ACK)
        if (p->from != 0 && p->from != ourNodeNum) {
            if (p->relay_node) {
                meshtastic_NodeInfoLite *origTx = nodeDB->getMeshNode(p->from);
                if (origTx) {
                    LOG_DEBUG("Update next hop of 0x%x to 0x%x based on received DM or ACK.\n", p->from, p->relay_node);
                    origTx->next_hop = p->relay_node;
                }
            }
        }
    }

    if (config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE) {
        if ((p->to != ourNodeNum) && (getFrom(p) != ourNodeNum)) {
            if (p->next_hop == nodeDB->getLastByteOfNodeNum(ourNodeNum)) {
                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it
                LOG_INFO("Relaying received next-hop message coming from %x\n", p->relay_node);

                tosend->hop_limit--; // bump down the hop count
                NextHopRouter::send(tosend);
            } else if (p->next_hop == NO_NEXT_HOP_PREFERENCE) {
                // No preference for next hop, use FloodingRouter
                LOG_DEBUG("No preference for next hop, using FloodingRouter\n");
                FloodingRouter::sniffReceived(p, c);
            } // else don't relay
        }
    } else {
        LOG_DEBUG("Not rebroadcasting. Role = Role_ClientMute\n");
    }
    // handle the packet as normal
    Router::sniffReceived(p, c);
}

/**
 * Get the next hop for a destination, given the relay node
 * @return the node number of the next hop, 0 if no preference (fallback to FloodingRouter)
 */
uint8_t NextHopRouter::getNextHop(NodeNum to, uint8_t relay_node)
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(to);
    if (node) {
        // We are careful not to return the relay node as the next hop
        if (node->next_hop != relay_node) {
            LOG_DEBUG("Next hop for 0x%x is 0x%x\n", to, node->next_hop);
            return node->next_hop;
        } else {
            LOG_WARN("Next hop for 0x%x is 0x%x, same as relayer; setting as no preference\n", to, node->next_hop);
            return NO_NEXT_HOP_PREFERENCE;
        }
    } else {
        return NO_NEXT_HOP_PREFERENCE;
    }
}