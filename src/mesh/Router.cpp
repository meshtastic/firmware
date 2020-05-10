#include "Router.h"
#include "CryptoEngine.h"
#include "GPS.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

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
#define MAX_PACKETS                                                                                                              \
    (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + MAX_TX_QUEUE +                                                                          \
     2) // max number of packets which can be in flight (either queued from reception or queued for sending)

MemoryPool<MeshPacket> packetPool(MAX_PACKETS);

/**
 * Constructor
 *
 * Currently we only allow one interface, that may change in the future
 */
Router::Router() : fromRadioQueue(MAX_RX_FROMRADIO) {}

/**
 * do idle processing
 * Mostly looking in our incoming rxPacket queue and calling handleReceived.
 */
void Router::loop()
{
    MeshPacket *mp;
    while ((mp = fromRadioQueue.dequeuePtr(0)) != NULL) {
        handleReceived(mp);
    }
}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error.
 */
ErrorCode Router::send(MeshPacket *p)
{
    // If the packet hasn't yet been encrypted, do so now (it might already be encrypted if we are just forwarding it)

    assert(p->which_payload == MeshPacket_encrypted_tag ||
           p->which_payload == MeshPacket_decoded_tag); // I _think_ all packets should have a payload by now

    // First convert from protobufs to raw bytes
    if (p->which_payload == MeshPacket_decoded_tag) {
        static uint8_t bytes[MAX_RHPACKETLEN]; // we have to use a scratch buffer because a union

        size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), SubPacket_fields, &p->decoded);

        assert(numbytes <= MAX_RHPACKETLEN);
        crypto->encrypt(p->from, p->id, numbytes, bytes);

        // Copy back into the packet and set the variant type
        memcpy(p->encrypted.bytes, bytes, numbytes);
        p->encrypted.size = numbytes;
        p->which_payload = MeshPacket_encrypted_tag;
    }

    if (iface) {
        // DEBUG_MSG("Sending packet via interface fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);
        return iface->send(p);
    } else {
        DEBUG_MSG("Dropping packet - no interfaces - fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);
        packetPool.release(p);
        return ERRNO_NO_INTERFACES;
    }
}

/**
 * Handle any packet that is received by an interface on this node.
 * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
 */
void Router::handleReceived(MeshPacket *p)
{
    // FIXME, this class shouldn't EVER need to know about the GPS, move getValidTime() into a non gps dependent function
    // Also, we should set the time from the ISR and it should have msec level resolution
    p->rx_time = getValidTime(); // store the arrival timestamp for the phone

    assert(p->which_payload ==
           MeshPacket_encrypted_tag); // I _think_ the only thing that pushes to us is raw devices that just received packets

    // Try to decrypt the packet if we can
    static uint8_t bytes[MAX_RHPACKETLEN];
    memcpy(bytes, p->encrypted.bytes,
           p->encrypted.size); // we have to copy into a scratch buffer, because these bytes are a union with the decoded protobuf
    crypto->decrypt(p->from, p->id, p->encrypted.size, bytes);

    // Take those raw bytes and convert them back into a well structured protobuf we can understand
    if (!pb_decode_from_bytes(bytes, p->encrypted.size, SubPacket_fields, &p->decoded)) {
        DEBUG_MSG("Invalid protobufs in received mesh packet, discarding.\n");
    } else {
        // parsing was successful, queue for our recipient
        p->which_payload = MeshPacket_decoded_tag;

        DEBUG_MSG("Notifying observers of received packet fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);
        notifyPacketReceived.notifyObservers(p);
    }

    packetPool.release(p);
}