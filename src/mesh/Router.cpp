#include "Router.h"
#include "CryptoEngine.h"
#include "RTC.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include <NodeDB.h>

/**
 * Router todo
 *
 * DONE: Implement basic interface and use it elsewhere in app
 * Add naive flooding mixin (& drop duplicate rx broadcasts), add tools for sending broadcasts with incrementing sequence #s
 * Add an optional adjacent node only 'send with ack' mixin.  If we timeout waiting for the ack, call handleAckTimeout(packet)
 * Add DSR mixin
 *
 **/

#define MAX_RX_FROMRADIO                                                                                                         \
    4 // max number of packets destined to our queue, we dispatch packets quickly so it doesn't need to be big

// I think this is right, one packet for each of the three fifos + one packet being currently assembled for TX or RX
// And every TX packet might have a retransmission packet or an ack alive at any moment
#define MAX_PACKETS                                                                                                              \
    (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + 2 * MAX_TX_QUEUE +                                                                      \
     2) // max number of packets which can be in flight (either queued from reception or queued for sending)

// static MemoryPool<MeshPacket> staticPool(MAX_PACKETS);
static MemoryDynamic<MeshPacket> staticPool;

Allocator<MeshPacket> &packetPool = staticPool;

/**
 * Constructor
 *
 * Currently we only allow one interface, that may change in the future
 */
Router::Router() : concurrency::OSThread("Router"), fromRadioQueue(MAX_RX_FROMRADIO)
{
    // This is called pre main(), don't touch anything here, the following code is not safe

    /* DEBUG_MSG("Size of NodeInfo %d\n", sizeof(NodeInfo));
    DEBUG_MSG("Size of SubPacket %d\n", sizeof(SubPacket));
    DEBUG_MSG("Size of MeshPacket %d\n", sizeof(MeshPacket)); */

    fromRadioQueue.setReader(this);
}

/**
 * do idle processing
 * Mostly looking in our incoming rxPacket queue and calling handleReceived.
 */
int32_t Router::runOnce()
{
    MeshPacket *mp;
    while ((mp = fromRadioQueue.dequeuePtr(0)) != NULL) {
        perhapsHandleReceived(mp);
    }

    return INT32_MAX; // Wait a long time - until we get woken for the message queue
}

/// Generate a unique packet id
// FIXME, move this someplace better
PacketId generatePacketId()
{
    static uint32_t i; // Note: trying to keep this in noinit didn't help for working across reboots
    static bool didInit = false;

    assert(sizeof(PacketId) == 4 || sizeof(PacketId) == 1);                // only supported values
    uint32_t numPacketId = sizeof(PacketId) == 1 ? UINT8_MAX : UINT32_MAX; // 0 is consider invalid

    if (!didInit) {
        didInit = true;

        // pick a random initial sequence number at boot (to prevent repeated reboots always starting at 0)
        // Note: we mask the high order bit to ensure that we never pass a 'negative' number to random
        i = random(numPacketId & 0x7fffffff);
        DEBUG_MSG("Initial packet id %u, numPacketId %u\n", i, numPacketId);
    }

    i++;
    PacketId id = (i % numPacketId) + 1; // return number between 1 and numPacketId (ie - never zero)
    myNodeInfo.current_packet_id = id;   // Kinda crufty - we keep updating this so the phone can see a current value
    return id;
}

MeshPacket *Router::allocForSending()
{
    MeshPacket *p = packetPool.allocZeroed();

    p->which_payloadVariant = MeshPacket_decoded_tag; // Assume payload is decoded at start.
    p->from = nodeDB.getNodeNum();
    p->to = NODENUM_BROADCAST;
    p->hop_limit = HOP_RELIABLE;
    p->id = generatePacketId();
    p->rx_time =
        getValidTime(RTCQualityFromNet); // Just in case we process the packet locally - make sure it has a valid timestamp

    return p;
}

/**
 * Send an ack or a nak packet back towards whoever sent idFrom
 */
void Router::sendAckNak(ErrorReason err, NodeNum to, PacketId idFrom)
{
    auto p = allocForSending();
    p->hop_limit = 0; // Assume just immediate neighbors for now
    p->to = to;
    DEBUG_MSG("Sending an err=%d,to=0x%x,idFrom=0x%x,id=0x%x\n", err, to, idFrom, p->id);

    if (!err) {
        p->decoded.ackVariant.success_id = idFrom;
        p->decoded.which_ackVariant = SubPacket_success_id_tag;
    } else {
        p->decoded.ackVariant.fail_id = idFrom;
        p->decoded.which_ackVariant = SubPacket_fail_id_tag;

        // Also send back the error reason
        p->decoded.which_payloadVariant = SubPacket_error_reason_tag;
        p->decoded.error_reason = err;
    }

    sendLocal(p); // we sometimes send directly to the local node
}



ErrorCode Router::sendLocal(MeshPacket *p)
{
    // No need to deliver externally if the destination is the local node
    if (p->to == nodeDB.getNodeNum()) {
        printPacket("Enqueuing local", p);
        fromRadioQueue.enqueue(p);
        return ERRNO_OK;
    } else if (!iface) {
        // We must be sending to remote nodes also, fail if no interface found

        // ERROR! no radio found, report failure back to the client and drop the packet
        DEBUG_MSG("Error: No interface, returning NAK and dropping packet.\n");
        sendAckNak(ErrorReason_NO_INTERFACE, p->from, p->id);
        packetPool.release(p);

        return ERRNO_NO_INTERFACES;
    } else {
        // If we are sending a broadcast, we also treat it as if we just received it ourself
        // this allows local apps (and PCs) to see broadcasts sourced locally
        if (p->to == NODENUM_BROADCAST) {
            handleReceived(p);
        }

        return send(p);
    }
}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error.
 */
ErrorCode Router::send(MeshPacket *p)
{
    assert(p->to != nodeDB.getNodeNum()); // should have already been handled by sendLocal

    PacketId nakId = p->decoded.which_ackVariant == SubPacket_fail_id_tag ? p->decoded.ackVariant.fail_id : 0;
    assert(
        !nakId); // I don't think we ever send 0hop naks over the wire (other than to the phone), test that assumption with assert

    // Never set the want_ack flag on broadcast packets sent over the air.
    if (p->to == NODENUM_BROADCAST)
        p->want_ack = false;

    // If the packet hasn't yet been encrypted, do so now (it might already be encrypted if we are just forwarding it)

    assert(p->which_payloadVariant == MeshPacket_encrypted_tag ||
           p->which_payloadVariant == MeshPacket_decoded_tag); // I _think_ all packets should have a payload by now

    // First convert from protobufs to raw bytes
    if (p->which_payloadVariant == MeshPacket_decoded_tag) {
        static uint8_t bytes[MAX_RHPACKETLEN]; // we have to use a scratch buffer because a union

        size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), SubPacket_fields, &p->decoded);

        assert(numbytes <= MAX_RHPACKETLEN);
        crypto->encrypt(p->from, p->id, numbytes, bytes);

        // Copy back into the packet and set the variant type
        memcpy(p->encrypted.bytes, bytes, numbytes);
        p->encrypted.size = numbytes;
        p->which_payloadVariant = MeshPacket_encrypted_tag;
    }

    assert(iface); // This should have been detected already in sendLocal (or we just received a packet from outside)
    // if (iface) {
    // DEBUG_MSG("Sending packet via interface fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);
    return iface->send(p);
    /* } else {
        DEBUG_MSG("Dropping packet - no interfaces - fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);
        packetPool.release(p);
        return ERRNO_NO_INTERFACES;
    } */
}

/** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
bool Router::cancelSending(NodeNum from, PacketId id) {
    return iface ? iface->cancelSending(from, id) : false;
}



/**
 * Every (non duplicate) packet this node receives will be passed through this method.  This allows subclasses to
 * update routing tables etc... based on what we overhear (even for messages not destined to our node)
 */
void Router::sniffReceived(const MeshPacket *p)
{
    DEBUG_MSG("FIXME-update-db Sniffing packet\n");
    // FIXME, update nodedb here for any packet that passes through us
}

bool Router::perhapsDecode(MeshPacket *p)
{
    if (p->which_payloadVariant == MeshPacket_decoded_tag)
        return true; // If packet was already decoded just return

    assert(p->which_payloadVariant == MeshPacket_encrypted_tag);

    // FIXME - someday don't send routing packets encrypted.  That would allow us to route for other channels without
    // being able to decrypt their data.
    // Try to decrypt the packet if we can
    static uint8_t bytes[MAX_RHPACKETLEN];
    memcpy(bytes, p->encrypted.bytes,
           p->encrypted.size); // we have to copy into a scratch buffer, because these bytes are a union with the decoded protobuf
    crypto->decrypt(p->from, p->id, p->encrypted.size, bytes);

    // Take those raw bytes and convert them back into a well structured protobuf we can understand
    if (!pb_decode_from_bytes(bytes, p->encrypted.size, SubPacket_fields, &p->decoded)) {
        DEBUG_MSG("Invalid protobufs in received mesh packet!\n");
        return false;
    } else {
        // parsing was successful
        p->which_payloadVariant = MeshPacket_decoded_tag;
        return true;
    }
}

NodeNum Router::getNodeNum()
{
    return nodeDB.getNodeNum();
}

/**
 * Handle any packet that is received by an interface on this node.
 * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
 */
void Router::handleReceived(MeshPacket *p)
{
    // Also, we should set the time from the ISR and it should have msec level resolution
    p->rx_time = getValidTime(RTCQualityFromNet); // store the arrival timestamp for the phone

    // Take those raw bytes and convert them back into a well structured protobuf we can understand
    if (perhapsDecode(p)) {
        // parsing was successful, queue for our recipient

        sniffReceived(p);

        if (p->to == NODENUM_BROADCAST || p->to == getNodeNum()) {
            printPacket("Delivering rx packet", p);
            notifyPacketReceived.notifyObservers(p);
        }
    }
}

void Router::perhapsHandleReceived(MeshPacket *p)
{
    assert(radioConfig.has_preferences);
    bool ignore = is_in_repeated(radioConfig.preferences.ignore_incoming, p->from);

    if (ignore)
        DEBUG_MSG("Ignoring incoming message, 0x%x is in our ignore list\n", p->from);
    else if (ignore |= shouldFilterReceived(p)) {
        // DEBUG_MSG("Incoming message was filtered 0x%x\n", p->from);
    }

    // Note: we avoid calling shouldFilterReceived if we are supposed to ignore certain nodes - because some overrides might
    // cache/learn of the existence of nodes (i.e. FloodRouter) that they should not
    if (!ignore)
        handleReceived(p);

    packetPool.release(p);
}