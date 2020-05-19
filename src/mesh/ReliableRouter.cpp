#include "ReliableRouter.h"
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
        auto copy = packetPool.allocCopy(*p);
        startRetransmission(copy);
    }

    return FloodingRouter::send(p);
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
void ReliableRouter::handleReceived(MeshPacket *p)
{
    if (p->to == getNodeNum()) { // ignore ack/nak/want_ack packets that are not address to us (for now)
        if (p->want_ack) {
            sendAckNak(true, p->from, p->id);
        }

        if (perhapsDecode(p)) {
            // If the payload is valid, look for ack/nak

            PacketId ackId = p->decoded.which_ack == SubPacket_success_id_tag ? p->decoded.ack.success_id : 0;
            PacketId nakId = p->decoded.which_ack == SubPacket_fail_id_tag ? p->decoded.ack.fail_id : 0;

            // we are careful to only read/update wasSeenRecently _after_ confirming this is an ack (to not mess
            // up broadcasts)
            if ((ackId || nakId) && !wasSeenRecently(p)) {
                if (ackId) {
                    DEBUG_MSG("Received a ack=%d, stopping retransmissions\n", ackId);
                    stopRetransmission(p->to, ackId);
                } else {
                    DEBUG_MSG("Received a nak=%d, stopping retransmissions\n", nakId);
                    stopRetransmission(p->to, nakId);
                }
            }
        }
    }

    // handle the packet as normal
    FloodingRouter::handleReceived(p);
}

/**
 * Send an ack or a nak packet back towards whoever sent idFrom
 */
void ReliableRouter::sendAckNak(bool isAck, NodeNum to, PacketId idFrom)
{
    DEBUG_MSG("Sending an ack=%d,to=%d,idFrom=%d", isAck, to, idFrom);
    auto p = allocForSending();
    p->hop_limit = 0; // Assume just immediate neighbors for now
    p->to = to;

    if (isAck) {
        p->decoded.ack.success_id = idFrom;
        p->decoded.which_ack = SubPacket_success_id_tag;
    } else {
        p->decoded.ack.fail_id = idFrom;
        p->decoded.which_ack = SubPacket_fail_id_tag;
    }

    send(p);
}

/**
 * Stop any retransmissions we are doing of the specified node/packet ID pair
 */
void ReliableRouter::stopRetransmission(NodeNum from, PacketId id) {}

/**
 * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
 */
void ReliableRouter::startRetransmission(MeshPacket *p) {}

/**
 * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
 */
void ReliableRouter::doRetransmissions() {}