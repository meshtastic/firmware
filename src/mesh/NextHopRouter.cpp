#include "NextHopRouter.h"

NextHopRouter::NextHopRouter() {}

PendingPacket::PendingPacket(meshtastic_MeshPacket *p, uint8_t numRetransmissions)
{
    packet = p;
    this->numRetransmissions = numRetransmissions - 1; // We subtract one, because we assume the user just did the first send
}

/**
 * Send a packet
 */
ErrorCode NextHopRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // First set the relayer to us
    wasSeenRecently(p);                                         // FIXME, move this to a sniffSent method

    p->next_hop = getNextHop(p->to, p->relay_node); // set the next hop
    LOG_DEBUG("Setting next hop for packet with dest %x to %x\n", p->to, p->next_hop);

    if (p->next_hop != NO_NEXT_HOP_PREFERENCE && (p->want_ack || (p->to != p->next_hop && p->hop_limit > 0)))
        startRetransmission(packetPool.allocCopy(*p)); // start retransmission for relayed packet

    return Router::send(p);
}

bool NextHopRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        if (p->next_hop == nodeDB->getLastByteOfNodeNum(getNodeNum())) {
            LOG_DEBUG("Ignoring incoming msg, because we've already seen it.\n");
        } else {
            LOG_DEBUG("Ignoring incoming msg, because we've already seen it and cancel any outgoing packets.\n");
            stopRetransmission(p->from, p->id);
            Router::cancelSending(p->from, p->id);
        }
        return true;
    }

    return Router::shouldFilterReceived(p);
}

void NextHopRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    NodeNum ourNodeNum = getNodeNum();
    // TODO DMs are now usually not decoded!
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) && (p->decoded.request_id != 0);
    if (isAckorReply) {
        // Update next-hop for the original transmitter of this successful transmission to the relay node, but ONLY if "from" is
        // not 0 (means implicit ACK) and original packet was also relayed by this node, or we sent it directly to the destination
        if (p->to == ourNodeNum && p->from != 0) {
            if (p->relay_node) {
                // Check who was the original relayer of this packet
                uint8_t original_relayer = PacketHistory::getRelayerFromHistory(p->decoded.request_id, p->to);
                uint8_t ourRelayID = nodeDB->getLastByteOfNodeNum(ourNodeNum);
                meshtastic_NodeInfoLite *origTx = nodeDB->getMeshNode(p->from);
                // Either original relayer and relayer of ACK are the same, or we were the relayer and the ACK came directly from
                // the destination
                if (origTx && original_relayer == p->relay_node ||
                    original_relayer == ourRelayID && p->relay_node == nodeDB->getLastByteOfNodeNum(p->from)) {
                    LOG_DEBUG("Update next hop of 0x%x to 0x%x based on received ACK or reply.\n", p->from, p->relay_node);
                    origTx->next_hop = p->relay_node;
                }
            }
        }

        LOG_DEBUG("Receiving an ACK or reply, don't need to relay this packet anymore.\n");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
        stopRetransmission(p->from, p->decoded.request_id);
    }

    if (config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE) {
        if ((p->to != ourNodeNum) && (getFrom(p) != ourNodeNum)) {
            if (p->next_hop == NO_NEXT_HOP_PREFERENCE || p->next_hop == nodeDB->getLastByteOfNodeNum(ourNodeNum)) {
                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it
                LOG_INFO("Relaying received message coming from %x\n", p->relay_node);

                tosend->hop_limit--; // bump down the hop count
                NextHopRouter::send(tosend);
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
        if (node->next_hop && node->next_hop != relay_node) {
            LOG_DEBUG("Next hop for 0x%x is 0x%x\n", to, node->next_hop);
            return node->next_hop;
        } else {
            if (node->next_hop)
                LOG_WARN("Next hop for 0x%x is 0x%x, same as relayer; setting as no preference\n", to, node->next_hop);
        }
    }
    return NO_NEXT_HOP_PREFERENCE;
}

PendingPacket *NextHopRouter::findPendingPacket(GlobalPacketId key)
{
    auto old = pending.find(key); // If we have an old record, someone messed up because id got reused
    if (old != pending.end()) {
        return &old->second;
    } else
        return NULL;
}
/**
 * Stop any retransmissions we are doing of the specified node/packet ID pair
 */
bool NextHopRouter::stopRetransmission(NodeNum from, PacketId id)
{
    auto key = GlobalPacketId(from, id);
    return stopRetransmission(key);
}

bool NextHopRouter::stopRetransmission(GlobalPacketId key)
{
    auto old = findPendingPacket(key);
    if (old) {
        auto p = old->packet;
        /* Only when we already transmitted a packet via LoRa, we will cancel the packet in the Tx queue
          to avoid canceling a transmission if it was ACKed super fast via MQTT */
        if (old->numRetransmissions < NUM_RETRANSMISSIONS - 1) {
            // remove the 'original' (identified by originator and packet->id) from the txqueue and free it
            cancelSending(getFrom(p), p->id);
            // now free the pooled copy for retransmission too
            packetPool.release(p);
        }
        auto numErased = pending.erase(key);
        assert(numErased == 1);
        return true;
    } else
        return false;
}

/**
 * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
 */
PendingPacket *NextHopRouter::startRetransmission(meshtastic_MeshPacket *p)
{
    auto id = GlobalPacketId(p);
    auto rec = PendingPacket(p, this->NUM_RETRANSMISSIONS);

    stopRetransmission(getFrom(p), p->id);

    setNextTx(&rec);
    pending[id] = rec;

    return &pending[id];
}

/**
 * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
 */
int32_t NextHopRouter::doRetransmissions()
{
    uint32_t now = millis();
    int32_t d = INT32_MAX;

    // FIXME, we should use a better datastructure rather than walking through this map.
    // for(auto el: pending) {
    for (auto it = pending.begin(), nextIt = it; it != pending.end(); it = nextIt) {
        ++nextIt; // we use this odd pattern because we might be deleting it...
        auto &p = it->second;

        bool stillValid = true; // assume we'll keep this record around

        // FIXME, handle 51 day rolloever here!!!
        if (p.nextTxMsec <= now) {
            if (p.numRetransmissions == 0) {
                if (p.packet->from == getNodeNum()) {
                    LOG_DEBUG("Reliable send failed, returning a nak for fr=0x%x,to=0x%x,id=0x%x\n", p.packet->from, p.packet->to,
                              p.packet->id);
                    sendAckNak(meshtastic_Routing_Error_MAX_RETRANSMIT, getFrom(p.packet), p.packet->id, p.packet->channel);
                }
                // Note: we don't stop retransmission here, instead the Nak packet gets processed in sniffReceived
                stopRetransmission(it->first);
                stillValid = false; // just deleted it
            } else {
                LOG_DEBUG("Sending retransmission fr=0x%x,to=0x%x,id=0x%x, tries left=%d\n", p.packet->from, p.packet->to,
                          p.packet->id, p.numRetransmissions);

                if (p.packet->to != NODENUM_BROADCAST) {
                    if (p.numRetransmissions == 1) {
                        // Last retransmission, reset next_hop (fallback to FloodingRouter)
                        p.packet->next_hop = NO_NEXT_HOP_PREFERENCE;
                        // Also reset it in the nodeDB
                        meshtastic_NodeInfoLite *sentTo = nodeDB->getMeshNode(p.packet->to);
                        if (sentTo) {
                            LOG_WARN("Resetting next hop for packet with dest 0x%x\n", p.packet->to);
                            sentTo->next_hop = NO_NEXT_HOP_PREFERENCE;
                        }
                        FloodingRouter::send(packetPool.allocCopy(*p.packet));
                    } else {
                        NextHopRouter::send(packetPool.allocCopy(*p.packet));
                    }
                } else {
                    // Note: we call the superclass version because we don't want to have our version of send() add a new
                    // retransmission record
                    FloodingRouter::send(packetPool.allocCopy(*p.packet));
                }

                // Queue again
                --p.numRetransmissions;
                setNextTx(&p);
            }
        }

        if (stillValid) {
            // Update our desired sleep delay
            int32_t t = p.nextTxMsec - now;

            d = min(t, d);
        }
    }

    return d;
}

void NextHopRouter::setNextTx(PendingPacket *pending)
{
    assert(iface);
    auto d = iface->getRetransmissionMsec(pending->packet);
    pending->nextTxMsec = millis() + d;
    LOG_DEBUG("Setting next retransmission in %u msecs: ", d);
    printPacket("", pending->packet);
    setReceivedMessage(); // Run ASAP, so we can figure out our correct sleep time
}