#include "ReliableRouter.h"
#include "MeshTypes.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "timing.h"

// ReliableRouter::ReliableRouter() {}

/**
 * If the message is want_ack, then add it to a list of packets to retransmit.
 * If we run out of retransmissions, send a nak packet towards the original client to indicate failure.
 */
ErrorCode ReliableRouter::send(MeshPacket *p)
{
    if (p->want_ack) {
        // If someone asks for acks on broadcast, we need the hop limit to be at least one, so that first node that receives our
        // message will rebroadcast
        if (p->to == NODENUM_BROADCAST && p->hop_limit == 0)
            p->hop_limit = 1;

        auto copy = packetPool.allocCopy(*p);
        startRetransmission(copy);
    }

    return FloodingRouter::send(p);
}

bool ReliableRouter::shouldFilterReceived(const MeshPacket *p)
{
    if (p->to == NODENUM_BROADCAST && p->from == getNodeNum()) {
        printPacket("Rx someone rebroadcasting for us", p);

        // We are seeing someone rebroadcast one of our broadcast attempts.
        // If this is the first time we saw this, cancel any retransmissions we have queued up and generate an internal ack for
        // the original sending process.
        if (stopRetransmission(p->from, p->id)) {
            DEBUG_MSG("Someone is retransmitting for us, generate implicit ack\n");
            sendAckNak(true, p->from, p->id);
        }
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
void ReliableRouter::sniffReceived(const MeshPacket *p)
{
    NodeNum ourNode = getNodeNum();

    if (p->to == ourNode) { // ignore ack/nak/want_ack packets that are not address to us (we only handle 0 hop reliability
                            // - not DSR routing)
        if (p->want_ack) {
            sendAckNak(true, p->from, p->id);
        }

        // If the payload is valid, look for ack/nak

        PacketId ackId = p->decoded.which_ack == SubPacket_success_id_tag ? p->decoded.ack.success_id : 0;
        PacketId nakId = p->decoded.which_ack == SubPacket_fail_id_tag ? p->decoded.ack.fail_id : 0;

        // We intentionally don't check wasSeenRecently, because it is harmless to delete non existent retransmission records
        if (ackId || nakId) {
            if (ackId) {
                DEBUG_MSG("Received a ack=%d, stopping retransmissions\n", ackId);
                stopRetransmission(p->to, ackId);
            } else {
                DEBUG_MSG("Received a nak=%d, stopping retransmissions\n", nakId);
                stopRetransmission(p->to, nakId);
            }
        }
    }

    // handle the packet as normal
    FloodingRouter::sniffReceived(p);
}

/**
 * Send an ack or a nak packet back towards whoever sent idFrom
 */
void ReliableRouter::sendAckNak(bool isAck, NodeNum to, PacketId idFrom)
{
    auto p = allocForSending();
    p->hop_limit = 0; // Assume just immediate neighbors for now
    p->to = to;
    DEBUG_MSG("Sending an ack=0x%x,to=0x%x,idFrom=%d,id=%d\n", isAck, to, idFrom, p->id);

    if (isAck) {
        p->decoded.ack.success_id = idFrom;
        p->decoded.which_ack = SubPacket_success_id_tag;
    } else {
        p->decoded.ack.fail_id = idFrom;
        p->decoded.which_ack = SubPacket_fail_id_tag;
    }

    sendLocal(p); // we sometimes send directly to the local node
}

#define NUM_RETRANSMISSIONS 3

PendingPacket::PendingPacket(MeshPacket *p)
{
    packet = p;
    numRetransmissions = NUM_RETRANSMISSIONS - 1; // We subtract one, because we assume the user just did the first send
    setNextTx();
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
        packetPool.release(old->packet);
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

    stopRetransmission(p->from, p->id);
    pending[id] = rec;

    return &pending[id];
}

/**
 * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
 */
void ReliableRouter::doRetransmissions()
{
    uint32_t now = timing::millis();

    // FIXME, we should use a better datastructure rather than walking through this map.
    // for(auto el: pending) {
    for (auto it = pending.begin(), nextIt = it; it != pending.end(); it = nextIt) {
        ++nextIt; // we use this odd pattern because we might be deleting it...
        auto &p = it->second;

        // FIXME, handle 51 day rolloever here!!!
        if (p.nextTxMsec <= now) {
            if (p.numRetransmissions == 0) {
                DEBUG_MSG("Reliable send failed, returning a nak fr=0x%x,to=0x%x,id=%d\n", p.packet->from, p.packet->to,
                          p.packet->id);
                sendAckNak(false, p.packet->from, p.packet->id);
                // Note: we don't stop retransmission here, instead the Nak packet gets processed in sniffReceived - which
                // allows the DSR version to still be able to look at the PendingPacket
                stopRetransmission(it->first);
            } else {
                DEBUG_MSG("Sending reliable retransmission fr=0x%x,to=0x%x,id=%d, tries left=%d\n", p.packet->from, p.packet->to,
                          p.packet->id, p.numRetransmissions);

                // Note: we call the superclass version because we don't want to have our version of send() add a new
                // retransmission record
                FloodingRouter::send(packetPool.allocCopy(*p.packet));

                // Queue again
                --p.numRetransmissions;
                p.setNextTx();
            }
        }
    }
}