#include "ReliableRouter.h"
#include "Default.h"
#include "MeshTypes.h"
#include "configuration.h"
#include "memGet.h"
#include "mesh-pb-constants.h"
#include "modules/NodeInfoModule.h"
#include "modules/RoutingModule.h"

// ReliableRouter::ReliableRouter() {}

/**
 * If the message is want_ack, then add it to a list of packets to retransmit.
 * If we run out of retransmissions, send a nak packet towards the original client to indicate failure.
 */
ErrorCode ReliableRouter::send(meshtastic_MeshPacket *p)
{
    if (p->want_ack) {
        // If someone asks for acks on broadcast, we need the hop limit to be at least one, so that first node that receives our
        // message will rebroadcast.  But asking for hop_limit 0 in that context means the client app has no preference on hop
        // counts and we want this message to get through the whole mesh, so use the default.
        if (p->hop_limit == 0) {
            p->hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
        }
        DEBUG_HEAP_BEFORE;
        auto copy = packetPool.allocCopy(*p);
        DEBUG_HEAP_AFTER("ReliableRouter::send", copy);

        startRetransmission(copy, NUM_RELIABLE_RETX);
    }

    /* If we have pending retransmissions, add the airtime of this packet to it, because during that time we cannot receive an
       (implicit) ACK. Otherwise, we might retransmit too early.
     */
    for (auto i = pending.begin(); i != pending.end(); i++) {
        if (i->first.id != p->id) {
            i->second.nextTxMsec += iface->getPacketTime(p);
        }
    }

    return isBroadcast(p->to) ? FloodingRouter::send(p) : NextHopRouter::send(p);
}

bool ReliableRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    // Note: do not use getFrom() here, because we want to ignore messages sent from phone
    if (p->from == getNodeNum()) {
        printPacket("Rx someone rebroadcasting for us", p);

        // We are seeing someone rebroadcast one of our broadcast attempts.
        // If this is the first time we saw this, cancel any retransmissions we have queued up and generate an internal ack for
        // the original sending process.

        // This "optimization", does save lots of airtime. For DMs, you also get a real ACK back
        // from the intended recipient.
        auto key = GlobalPacketId(getFrom(p), p->id);
        auto old = findPendingPacket(key);
        if (old) {
            LOG_DEBUG("Generate implicit ack");
            // NOTE: we do NOT check p->wantAck here because p is the INCOMING rebroadcast and that packet is not expected to be
            // marked as wantAck
            sendAckNak(meshtastic_Routing_Error_NONE, getFrom(p), p->id, old->packet->channel);

            // Only stop retransmissions if the rebroadcast came via LoRa
            if (p->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA) {
                stopRetransmission(key);
            }
        } else {
            LOG_DEBUG("Didn't find pending packet");
        }
    }

    /* At this point we have already deleted the pending retransmission if this packet was an (implicit) ACK to it.
       Now for all other pending retransmissions, we have to add the airtime of this received packet to the retransmission timer,
       because while receiving this packet, we could not have received an (implicit) ACK for it.
       If we don't add this, we will likely retransmit too early.
    */
    for (auto i = pending.begin(); i != pending.end(); i++) {
        i->second.nextTxMsec += iface->getPacketTime(p, true);
    }

    return isBroadcast(p->to) ? FloodingRouter::shouldFilterReceived(p) : NextHopRouter::shouldFilterReceived(p);
}

/**
 * If we receive a want_ack packet (do not check for wasSeenRecently), send back an ack (this might generate multiple ack sends in
 * case the our first ack gets lost)
 *
 * If we receive an ack packet (do check wasSeenRecently), clear out any retransmissions and
 * forward the ack to the application layer.
 *
 * If we receive a nak packet (do check wasSeenRecently), clear out any retransmissions
 * and forward the nak to the application layer.
 *
 * Otherwise, let superclass handle it.
 */
void ReliableRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    if (isToUs(p)) { // ignore ack/nak/want_ack packets that are not address to us (we only handle 0 hop reliability)
        if (!MeshModule::currentReply) {
            if (p->want_ack) {
                if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                    /* A response may be set to want_ack for retransmissions, but we don't need to ACK a response if it received
                      an implicit ACK already. If we received it directly or via NextHopRouter, only ACK with a hop limit of 0 to
                      make sure the other side stops retransmitting. */

                    if (shouldSuccessAckWithWantAck(p)) {
                        // If this packet should always be ACKed reliably with want_ack back to the original sender, make sure we
                        // do that unconditionally.
                        sendAckNak(meshtastic_Routing_Error_NONE, getFrom(p), p->id, p->channel,
                                   routingModule->getHopLimitForResponse(p->hop_start, p->hop_limit), true);
                    } else if (!p->decoded.request_id && !p->decoded.reply_id) {
                        // If it's not an ACK or a reply, send an ACK.
                        sendAckNak(meshtastic_Routing_Error_NONE, getFrom(p), p->id, p->channel,
                                   routingModule->getHopLimitForResponse(p->hop_start, p->hop_limit));
                    } else if ((p->hop_start > 0 && p->hop_start == p->hop_limit) || p->next_hop != NO_NEXT_HOP_PREFERENCE) {
                        // If we received the packet directly from the original sender, send a 0-hop ACK since the original sender
                        // won't overhear any implicit ACKs. If we received the packet via NextHopRouter, also send a 0-hop ACK to
                        // stop the immediate relayer's retransmissions.
                        sendAckNak(meshtastic_Routing_Error_NONE, getFrom(p), p->id, p->channel, 0);
                    }
                } else if (p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag && p->channel == 0 &&
                           (nodeDB->getMeshNode(p->from) == nullptr || nodeDB->getMeshNode(p->from)->user.public_key.size == 0)) {
                    LOG_INFO("PKI packet from unknown node, send PKI_UNKNOWN_PUBKEY");
                    sendAckNak(meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, getFrom(p), p->id, channels.getPrimaryIndex(),
                               routingModule->getHopLimitForResponse(p->hop_start, p->hop_limit));
                } else {
                    // Send a 'NO_CHANNEL' error on the primary channel if want_ack packet destined for us cannot be decoded
                    sendAckNak(meshtastic_Routing_Error_NO_CHANNEL, getFrom(p), p->id, channels.getPrimaryIndex(),
                               routingModule->getHopLimitForResponse(p->hop_start, p->hop_limit));
                }
            } else if (p->next_hop == nodeDB->getLastByteOfNodeNum(getNodeNum()) && p->hop_limit > 0) {
                // No wantAck, but we need to ACK with hop limit of 0 if we were the next hop to stop their retransmissions
                sendAckNak(meshtastic_Routing_Error_NONE, getFrom(p), p->id, p->channel, 0);
            }
        } else {
            LOG_DEBUG("Another module replied to this message, no need for 2nd ack");
        }
        if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag && c &&
            c->error_reason == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY) {
            if (owner.public_key.size == 32) {
                LOG_INFO("PKI decrypt failure, send a NodeInfo");
                nodeInfoModule->sendOurNodeInfo(p->from, false, p->channel, true);
            }
        }
        // We consider an ack to be either a !routing packet with a request ID or a routing packet with !error
        PacketId ackId = ((c && c->error_reason == meshtastic_Routing_Error_NONE) || !c) ? p->decoded.request_id : 0;

        // A nak is a routing packt that has an  error code
        PacketId nakId = (c && c->error_reason != meshtastic_Routing_Error_NONE) ? p->decoded.request_id : 0;

        // We intentionally don't check wasSeenRecently, because it is harmless to delete non existent retransmission records
        if (ackId || nakId) {
            LOG_DEBUG("Received a %s for 0x%x, stopping retransmissions", ackId ? "ACK" : "NAK", ackId);
            if (ackId) {
                stopRetransmission(p->to, ackId);
            } else {
                stopRetransmission(p->to, nakId);
            }
        }
    }

    // handle the packet as normal
    isBroadcast(p->to) ? FloodingRouter::sniffReceived(p, c) : NextHopRouter::sniffReceived(p, c);
}

/**
 * If we ACK this packet, should we set want_ack=true on the ACK for reliable delivery of the ACK packet?
 */
bool ReliableRouter::shouldSuccessAckWithWantAck(const meshtastic_MeshPacket *p)
{
    // Don't ACK-with-want-ACK outgoing packets
    if (isFromUs(p))
        return false;

    // Only ACK-with-want-ACK if the original packet asked for want_ack
    if (!p->want_ack)
        return false;

    // Only ACK-with-want-ACK packets to us (not broadcast)
    if (!isToUs(p))
        return false;

    // Special case for text message DMs:
    bool isTextMessage =
        (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) &&
        IS_ONE_OF(p->decoded.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP);

    if (isTextMessage) {
        // If it's a non-broadcast text message, and the original asked for want_ack,
        // let's send an ACK that is itself want_ack to improve reliability of confirming delivery back to the sender.
        // This should include all DMs regardless of whether or not reply_id is set.
        return true;
    }

    return false;
}