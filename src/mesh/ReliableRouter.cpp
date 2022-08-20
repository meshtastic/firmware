#include "ReliableRouter.h"
#include "MeshModule.h"
#include "MeshTypes.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

// ReliableRouter::ReliableRouter() {}

/**
 * If the message is want_ack, then add it to a list of packets to retransmit.
 * If we run out of retransmissions, send a nak packet towards the original client to indicate failure.
 */
ErrorCode ReliableRouter::send(MeshPacket *p)
{
    if (p->want_ack) {
        // If someone asks for acks on broadcast, we need the hop limit to be at least one, so that first node that receives our
        // message will rebroadcast.  But asking for hop_limit 0 in that context means the client app has no preference on hop
        // counts and we want this message to get through the whole mesh, so use the default.
        if (p->hop_limit == 0) {
            if (config.lora.hop_limit && config.lora.hop_limit <= HOP_MAX) {
                p->hop_limit = (config.lora.hop_limit >= HOP_MAX) ? HOP_MAX : config.lora.hop_limit;
            } else {
                p->hop_limit = HOP_RELIABLE;
            }
        }

        auto copy = packetPool.allocCopy(*p);
        startRetransmission(copy);
    }

    return FloodingRouter::send(p);
}

bool ReliableRouter::shouldFilterReceived(MeshPacket *p)
{
    // Note: do not use getFrom() here, because we want to ignore messages sent from phone
    if (p->from == getNodeNum()) {
        printPacket("Rx someone rebroadcasting for us", p);

        // We are seeing someone rebroadcast one of our broadcast attempts.
        // If this is the first time we saw this, cancel any retransmissions we have queued up and generate an internal ack for
        // the original sending process.

        // FIXME - we might want to turn off this "optimization", it does save lots of airtime but it assumes that once we've
        // heard one one adjacent node hear our packet that a) probably other adjacent nodes heard it and b) we can trust those
        // nodes to reach our destination.  Both of which might be incorrect.
        auto key = GlobalPacketId(getFrom(p), p->id);
        auto old = findPendingPacket(key);
        if (old) {
            DEBUG_MSG("generating implicit ack\n");
            // NOTE: we do NOT check p->wantAck here because p is the INCOMING rebroadcast and that packet is not expected to be
            // marked as wantAck
            sendAckNak(Routing_Error_NONE, getFrom(p), p->id, old->packet->channel);

            stopRetransmission(key);
        } else {
            DEBUG_MSG("didn't find pending packet\n");
        }
    }

    /* send acks for repeated packets that want acks and are destined for us
     * this way if an ACK is dropped and a packet is resent we'll ACK the resent packet
     * make sure wasSeenRecently _doesn't_ update
     * finding the channel requires decoding the packet. */
    if (p->want_ack && (p->to == getNodeNum()) && wasSeenRecently(p, false)) {
        if (perhapsDecode(p)) {
            sendAckNak(Routing_Error_NONE, getFrom(p), p->id, p->channel);
            DEBUG_MSG("acking a repeated want_ack packet\n");
        }
    } else if (wasSeenRecently(p, false) && p->hop_limit == HOP_RELIABLE) {
        // retransmission on broadcast has hop_limit still equal to HOP_RELIABLE
        DEBUG_MSG("Resending implicit ack for a repeated floodmsg\n");
        MeshPacket *tosend = packetPool.allocCopy(*p);
        tosend->hop_limit--; // bump down the hop count
        Router::send(tosend);
    }

    return FloodingRouter::shouldFilterReceived(p);
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
void ReliableRouter::sniffReceived(const MeshPacket *p, const Routing *c)
{
    NodeNum ourNode = getNodeNum();

    if (p->to == ourNode) { // ignore ack/nak/want_ack packets that are not address to us (we only handle 0 hop reliability
                            // - not DSR routing)
        if (p->want_ack) {
            if (MeshModule::currentReply)
                DEBUG_MSG("Someone else has replied to this message, no need for a 2nd ack\n");
            else
                sendAckNak(Routing_Error_NONE, getFrom(p), p->id, p->channel);
        }

        // We consider an ack to be either a !routing packet with a request ID or a routing packet with !error
        PacketId ackId = ((c && c->error_reason == Routing_Error_NONE) || !c) ? p->decoded.request_id : 0;

        // A nak is a routing packt that has an  error code
        PacketId nakId = (c && c->error_reason != Routing_Error_NONE) ? p->decoded.request_id : 0;

        // We intentionally don't check wasSeenRecently, because it is harmless to delete non existent retransmission records
        if (ackId || nakId) {
            if (ackId) {
                DEBUG_MSG("Received an ack for 0x%x, stopping retransmissions\n", ackId);
                stopRetransmission(p->to, ackId);
            } else {
                DEBUG_MSG("Received a nak for 0x%x, stopping retransmissions\n", nakId);
                stopRetransmission(p->to, nakId);
            }
        }
    }

    // handle the packet as normal
    FloodingRouter::sniffReceived(p, c);
}

#define NUM_RETRANSMISSIONS 3

PendingPacket::PendingPacket(MeshPacket *p)
{
    packet = p;
    numRetransmissions = NUM_RETRANSMISSIONS - 1; // We subtract one, because we assume the user just did the first send
}

PendingPacket *ReliableRouter::findPendingPacket(GlobalPacketId key)
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
bool ReliableRouter::stopRetransmission(NodeNum from, PacketId id)
{
    auto key = GlobalPacketId(from, id);
    return stopRetransmission(key);
}

bool ReliableRouter::stopRetransmission(GlobalPacketId key)
{
    auto old = findPendingPacket(key);
    if (old) {
        auto numErased = pending.erase(key);
        assert(numErased == 1);
        cancelSending(getFrom(old->packet), old->packet->id);
        return true;
    } else
        return false;
}

/**
 * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
 */
PendingPacket *ReliableRouter::startRetransmission(MeshPacket *p)
{
    auto id = GlobalPacketId(p);
    auto rec = PendingPacket(p);

    stopRetransmission(getFrom(p), p->id);

    setNextTx(&rec);
    pending[id] = rec;

    return &pending[id];
}

/**
 * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
 */
int32_t ReliableRouter::doRetransmissions()
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
                DEBUG_MSG("Reliable send failed, returning a nak for fr=0x%x,to=0x%x,id=0x%x\n", p.packet->from, p.packet->to,
                          p.packet->id);
                sendAckNak(Routing_Error_MAX_RETRANSMIT, getFrom(p.packet), p.packet->id, p.packet->channel);
                // Note: we don't stop retransmission here, instead the Nak packet gets processed in sniffReceived - which
                // allows the DSR version to still be able to look at the PendingPacket
                stopRetransmission(it->first);
                stillValid = false; // just deleted it
            } else {
                DEBUG_MSG("Sending reliable retransmission fr=0x%x,to=0x%x,id=0x%x, tries left=%d\n", p.packet->from,
                          p.packet->to, p.packet->id, p.numRetransmissions);

                // Note: we call the superclass version because we don't want to have our version of send() add a new
                // retransmission record
                FloodingRouter::send(packetPool.allocCopy(*p.packet));

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

void ReliableRouter::setNextTx(PendingPacket *pending)
{
    assert(iface);
    auto d = iface->getRetransmissionMsec(pending->packet);
    pending->nextTxMsec = millis() + d;
    DEBUG_MSG("Setting next retransmission in %u msecs: ", d);
    printPacket("", pending->packet);
    setReceivedMessage(); // Run ASAP, so we can figure out our correct sleep time
}
