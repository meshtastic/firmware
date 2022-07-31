#include "Router.h"
#include "Channels.h"
#include "CryptoEngine.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "modules/RoutingModule.h"
extern "C" {
#include "mesh/compression/unishox2.h"
}

#if HAS_WIFI
#include "mqtt/MQTT.h"
#endif

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
        // printPacket("handle fromRadioQ", mp);
        perhapsHandleReceived(mp);
    }

    // DEBUG_MSG("sleeping forever!\n");
    return INT32_MAX; // Wait a long time - until we get woken for the message queue
}

/**
 * RadioInterface calls this to queue up packets that have been received from the radio.  The router is now responsible for
 * freeing the packet
 */
void Router::enqueueReceivedMessage(MeshPacket *p)
{
    if (fromRadioQueue.enqueue(p, 0)) { // NOWAIT - fixme, if queue is full, delete older messages

        // Nasty hack because our threading is primitive.  interfaces shouldn't need to know about routers FIXME
        setReceivedMessage();
    } else {
        printPacket("BUG! fromRadioQueue is full! Discarding!", p);
        packetPool.release(p);
    }
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
    return id;
}

MeshPacket *Router::allocForSending()
{
    MeshPacket *p = packetPool.allocZeroed();

    p->which_payloadVariant = MeshPacket_decoded_tag; // Assume payload is decoded at start.
    p->from = nodeDB.getNodeNum();
    p->to = NODENUM_BROADCAST;
    if (config.lora.hop_limit && config.lora.hop_limit <= HOP_MAX) {
        p->hop_limit = (config.lora.hop_limit >= HOP_MAX) ? HOP_MAX : config.lora.hop_limit;
    } else {
        p->hop_limit = HOP_RELIABLE;
    }
    p->id = generatePacketId();
    p->rx_time =
        getValidTime(RTCQualityFromNet); // Just in case we process the packet locally - make sure it has a valid timestamp

    return p;
}

/**
 * Send an ack or a nak packet back towards whoever sent idFrom
 */
void Router::sendAckNak(Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex)
{
    routingModule->sendAckNak(err, to, idFrom, chIndex);
}

void Router::abortSendAndNak(Routing_Error err, MeshPacket *p)
{
    DEBUG_MSG("Error=%d, returning NAK and dropping packet.\n", err);
    sendAckNak(Routing_Error_NO_INTERFACE, getFrom(p), p->id, p->channel);
    packetPool.release(p);
}

void Router::setReceivedMessage()
{
    // DEBUG_MSG("set interval to ASAP\n");
    setInterval(0); // Run ASAP, so we can figure out our correct sleep time
    runASAP = true;
}

ErrorCode Router::sendLocal(MeshPacket *p, RxSource src)
{
    // No need to deliver externally if the destination is the local node
    if (p->to == nodeDB.getNodeNum()) {
        printPacket("Enqueued local", p);
        enqueueReceivedMessage(p);
        return ERRNO_OK;
    } else if (!iface) {
        // We must be sending to remote nodes also, fail if no interface found
        abortSendAndNak(Routing_Error_NO_INTERFACE, p);

        return ERRNO_NO_INTERFACES;
    } else {
        // If we are sending a broadcast, we also treat it as if we just received it ourself
        // this allows local apps (and PCs) to see broadcasts sourced locally
        if (p->to == NODENUM_BROADCAST) {
            handleReceived(p, src);
        }

        return send(p);
    }
}

void printBytes(const char *label, const uint8_t *p, size_t numbytes)
{
    DEBUG_MSG("%s: ", label);
    for (size_t i = 0; i < numbytes; i++)
        DEBUG_MSG("%02x ", p[i]);
    DEBUG_MSG("\n");
}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error.
 */
ErrorCode Router::send(MeshPacket *p)
{
    assert(p->to != nodeDB.getNodeNum()); // should have already been handled by sendLocal

    // PacketId nakId = p->decoded.which_ackVariant == SubPacket_fail_id_tag ? p->decoded.ackVariant.fail_id : 0;
    // assert(!nakId); // I don't think we ever send 0hop naks over the wire (other than to the phone), test that assumption with
    // assert

    // Never set the want_ack flag on broadcast packets sent over the air.
    if (p->to == NODENUM_BROADCAST)
        p->want_ack = false;

    // Up until this point we might have been using 0 for the from address (if it started with the phone), but when we send over
    // the lora we need to make sure we have replaced it with our local address
    p->from = getFrom(p);

    // If the packet hasn't yet been encrypted, do so now (it might already be encrypted if we are just forwarding it)

    assert(p->which_payloadVariant == MeshPacket_encrypted_tag ||
           p->which_payloadVariant == MeshPacket_decoded_tag); // I _think_ all packets should have a payload by now

    // If the packet is not yet encrypted, do so now
    if (p->which_payloadVariant == MeshPacket_decoded_tag) {
        ChannelIndex chIndex = p->channel; // keep as a local because we are about to change it

#if HAS_WIFI
        // check if we should send decrypted packets to mqtt

        // truth table:
        /* mqtt_server  mqtt_encryption_enabled should_encrypt
         *    not set                        0              1
         *    not set                        1              1
         *        set                        0              0
         *        set                        1              1
         *
         * => so we only decrypt mqtt if they have a custom mqtt server AND mqtt_encryption_enabled is FALSE
         */

        bool shouldActuallyEncrypt = true;
        if (*moduleConfig.mqtt.address && !moduleConfig.mqtt.encryption_enabled) {
            shouldActuallyEncrypt = false;
        }

        DEBUG_MSG("Should encrypt MQTT?: %d\n", shouldActuallyEncrypt);

        // the packet is currently in a decrypted state.  send it now if they want decrypted packets
        if (mqtt && !shouldActuallyEncrypt)
            mqtt->onSend(*p, chIndex);
#endif

        auto encodeResult = perhapsEncode(p);
        if (encodeResult != Routing_Error_NONE) {
            abortSendAndNak(encodeResult, p);
            return encodeResult; // FIXME - this isn't a valid ErrorCode
        }

#if HAS_WIFI
        // the packet is now encrypted.
        // check if we should send encrypted packets to mqtt
        if (mqtt && shouldActuallyEncrypt)
            mqtt->onSend(*p, chIndex);
#endif
    }

    assert(iface); // This should have been detected already in sendLocal (or we just received a packet from outside)
    return iface->send(p);
}

/** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
bool Router::cancelSending(NodeNum from, PacketId id)
{
    return iface ? iface->cancelSending(from, id) : false;
}

/**
 * Every (non duplicate) packet this node receives will be passed through this method.  This allows subclasses to
 * update routing tables etc... based on what we overhear (even for messages not destined to our node)
 */
void Router::sniffReceived(const MeshPacket *p, const Routing *c)
{
    DEBUG_MSG("FIXME-update-db Sniffing packet\n");
    // FIXME, update nodedb here for any packet that passes through us
}

bool perhapsDecode(MeshPacket *p)
{

    // DEBUG_MSG("\n\n** perhapsDecode payloadVariant - %d\n\n", p->which_payloadVariant);

    if (p->which_payloadVariant == MeshPacket_decoded_tag)
        return true; // If packet was already decoded just return

    // assert(p->which_payloadVariant == MeshPacket_encrypted_tag);

    // Try to find a channel that works with this hash
    for (ChannelIndex chIndex = 0; chIndex < channels.getNumChannels(); chIndex++) {
        // Try to use this hash/channel pair
        if (channels.decryptForHash(chIndex, p->channel)) {
            // Try to decrypt the packet if we can
            static uint8_t bytes[MAX_RHPACKETLEN];
            size_t rawSize = p->encrypted.size;
            assert(rawSize <= sizeof(bytes));
            memcpy(bytes, p->encrypted.bytes,
                   rawSize); // we have to copy into a scratch buffer, because these bytes are a union with the decoded protobuf
            crypto->decrypt(p->from, p->id, rawSize, bytes);

            // printBytes("plaintext", bytes, p->encrypted.size);

            // Take those raw bytes and convert them back into a well structured protobuf we can understand
            memset(&p->decoded, 0, sizeof(p->decoded));
            if (!pb_decode_from_bytes(bytes, rawSize, Data_fields, &p->decoded)) {
                DEBUG_MSG("Invalid protobufs in received mesh packet (bad psk?)!\n");
            } else if (p->decoded.portnum == PortNum_UNKNOWN_APP) {
                DEBUG_MSG("Invalid portnum (bad psk?)!\n");
            } else {
                // parsing was successful
                p->which_payloadVariant = MeshPacket_decoded_tag; // change type to decoded
                p->channel = chIndex;                             // change to store the index instead of the hash

                /*
                if (p->decoded.portnum == PortNum_TEXT_MESSAGE_APP) {
                    DEBUG_MSG("\n\n** TEXT_MESSAGE_APP\n");
                } else if (p->decoded.portnum == PortNum_TEXT_MESSAGE_COMPRESSED_APP) {
                    DEBUG_MSG("\n\n** PortNum_TEXT_MESSAGE_COMPRESSED_APP\n");
                }
                */

                // Decompress if needed. jm
                if (p->decoded.portnum == PortNum_TEXT_MESSAGE_COMPRESSED_APP) {
                    // Decompress the payload
                    char compressed_in[Constants_DATA_PAYLOAD_LEN] = {};
                    char decompressed_out[Constants_DATA_PAYLOAD_LEN] = {};
                    int decompressed_len;

                    memcpy(compressed_in, p->decoded.payload.bytes, p->decoded.payload.size);

                    decompressed_len = unishox2_decompress_simple(compressed_in, p->decoded.payload.size, decompressed_out);

                    // DEBUG_MSG("\n\n**\n\nDecompressed length - %d \n", decompressed_len);

                    memcpy(p->decoded.payload.bytes, decompressed_out, decompressed_len);

                    // Switch the port from PortNum_TEXT_MESSAGE_COMPRESSED_APP to PortNum_TEXT_MESSAGE_APP
                    p->decoded.portnum = PortNum_TEXT_MESSAGE_APP;
                }

                printPacket("decoded message", p);
                return true;
            }
        }
    }

    DEBUG_MSG("No suitable channel found for decoding, hash was 0x%x!\n", p->channel);
    return false;
}

/** Return 0 for success or a Routing_Errror code for failure
 */
Routing_Error perhapsEncode(MeshPacket *p)
{
    // If the packet is not yet encrypted, do so now
    if (p->which_payloadVariant == MeshPacket_decoded_tag) {
        static uint8_t bytes[MAX_RHPACKETLEN]; // we have to use a scratch buffer because a union

        size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), Data_fields, &p->decoded);

        // Only allow encryption on the text message app.
        //  TODO: Allow modules to opt into compression.
        if (p->decoded.portnum == PortNum_TEXT_MESSAGE_APP) {

            char original_payload[Constants_DATA_PAYLOAD_LEN];
            memcpy(original_payload, p->decoded.payload.bytes, p->decoded.payload.size);

            char compressed_out[Constants_DATA_PAYLOAD_LEN] = {0};

            int compressed_len;
            compressed_len = unishox2_compress_simple(original_payload, p->decoded.payload.size, compressed_out);

            DEBUG_MSG("Original length - %d \n", p->decoded.payload.size);
            DEBUG_MSG("Compressed length - %d \n", compressed_len);
            DEBUG_MSG("Original message - %s \n", p->decoded.payload.bytes);

            // If the compressed length is greater than or equal to the original size, don't use the compressed form
            if (compressed_len >= p->decoded.payload.size) {

                DEBUG_MSG("Not using compressing message.\n");
                // Set the uncompressed payload varient anyway. Shouldn't hurt?
                // p->decoded.which_payloadVariant = Data_payload_tag;

                // Otherwise we use the compressor
            } else {
                DEBUG_MSG("Using compressed message.\n");
                // Copy the compressed data into the meshpacket

                p->decoded.payload.size = compressed_len;
                memcpy(p->decoded.payload.bytes, compressed_out, compressed_len);

                p->decoded.portnum = PortNum_TEXT_MESSAGE_COMPRESSED_APP;
            }
        }

        if (numbytes > MAX_RHPACKETLEN)
            return Routing_Error_TOO_LARGE;

        // printBytes("plaintext", bytes, numbytes);

        ChannelIndex chIndex = p->channel; // keep as a local because we are about to change it
        auto hash = channels.setActiveByIndex(chIndex);
        if (hash < 0)
            // No suitable channel could be found for sending
            return Routing_Error_NO_CHANNEL;

        // Now that we are encrypting the packet channel should be the hash (no longer the index)
        p->channel = hash;
        crypto->encrypt(getFrom(p), p->id, numbytes, bytes);

        // Copy back into the packet and set the variant type
        memcpy(p->encrypted.bytes, bytes, numbytes);
        p->encrypted.size = numbytes;
        p->which_payloadVariant = MeshPacket_encrypted_tag;
    }

    return Routing_Error_NONE;
}

NodeNum Router::getNodeNum()
{
    return nodeDB.getNodeNum();
}

/**
 * Handle any packet that is received by an interface on this node.
 * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
 */
void Router::handleReceived(MeshPacket *p, RxSource src)
{
    // Also, we should set the time from the ISR and it should have msec level resolution
    p->rx_time = getValidTime(RTCQualityFromNet); // store the arrival timestamp for the phone

    // Take those raw bytes and convert them back into a well structured protobuf we can understand
    bool decoded = perhapsDecode(p);
    if (decoded) {
        // parsing was successful, queue for our recipient
        if (src == RX_SRC_LOCAL)
            printPacket("handleReceived(LOCAL)", p);
        else if (src == RX_SRC_USER)
            printPacket("handleReceived(USER)", p);
        else
            printPacket("handleReceived(REMOTE)", p);
    } else {
        printPacket("packet decoding failed (no PSK?)", p);
    }

    // call modules here
    MeshModule::callPlugins(*p, src);
}

void Router::perhapsHandleReceived(MeshPacket *p)
{
    // assert(radioConfig.has_preferences);
    bool ignore = is_in_repeated(config.lora.ignore_incoming, p->from);

    if (ignore)
        DEBUG_MSG("Ignoring incoming message, 0x%x is in our ignore list\n", p->from);
    else if (ignore |= shouldFilterReceived(p)) {
        DEBUG_MSG("Incoming message was filtered 0x%x\n", p->from);
    }

    // Note: we avoid calling shouldFilterReceived if we are supposed to ignore certain nodes - because some overrides might
    // cache/learn of the existence of nodes (i.e. FloodRouter) that they should not
    if (!ignore)
        handleReceived(p);

    packetPool.release(p);
}
