#include "Router.h"
#include "Channels.h"
#include "CryptoEngine.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "detect/LoRaRadioType.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/RoutingModule.h"
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif
#include "Default.h"
#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif
#if ENABLE_JSON_LOGGING || ARCH_PORTDUINO
#include "serialization/MeshPacketSerializer.h"
#endif

#define MAX_RX_FROMRADIO                                                                                                         \
    4 // max number of packets destined to our queue, we dispatch packets quickly so it doesn't need to be big

// I think this is right, one packet for each of the three fifos + one packet being currently assembled for TX or RX
// And every TX packet might have a retransmission packet or an ack alive at any moment
#define MAX_PACKETS                                                                                                              \
    (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + 2 * MAX_TX_QUEUE +                                                                      \
     2) // max number of packets which can be in flight (either queued from reception or queued for sending)

// static MemoryPool<MeshPacket> staticPool(MAX_PACKETS);
static MemoryDynamic<meshtastic_MeshPacket> staticPool;

Allocator<meshtastic_MeshPacket> &packetPool = staticPool;

static uint8_t bytes[MAX_LORA_PAYLOAD_LEN + 1] __attribute__((__aligned__));

/**
 * Constructor
 *
 * Currently we only allow one interface, that may change in the future
 */
Router::Router() : concurrency::OSThread("Router"), fromRadioQueue(MAX_RX_FROMRADIO)
{
    // This is called pre main(), don't touch anything here, the following code is not safe

    /* LOG_DEBUG("Size of NodeInfo %d", sizeof(NodeInfo));
    LOG_DEBUG("Size of SubPacket %d", sizeof(SubPacket));
    LOG_DEBUG("Size of MeshPacket %d", sizeof(MeshPacket)); */

    fromRadioQueue.setReader(this);

    // init Lockguard for crypt operations
    assert(!cryptLock);
    cryptLock = new concurrency::Lock();
}

/**
 * do idle processing
 * Mostly looking in our incoming rxPacket queue and calling handleReceived.
 */
int32_t Router::runOnce()
{
    meshtastic_MeshPacket *mp;
    while ((mp = fromRadioQueue.dequeuePtr(0)) != NULL) {
        // printPacket("handle fromRadioQ", mp);
        perhapsHandleReceived(mp);
    }

    // LOG_DEBUG("Sleep forever!");
    return INT32_MAX; // Wait a long time - until we get woken for the message queue
}

/**
 * RadioInterface calls this to queue up packets that have been received from the radio.  The router is now responsible for
 * freeing the packet
 */
void Router::enqueueReceivedMessage(meshtastic_MeshPacket *p)
{
    // Try enqueue until successful
    while (!fromRadioQueue.enqueue(p, 0)) {
        meshtastic_MeshPacket *old_p;
        old_p = fromRadioQueue.dequeuePtr(0); // Dequeue and discard the oldest packet
        if (old_p) {
            printPacket("fromRadioQ full, drop oldest!", old_p);
            packetPool.release(old_p);
        }
    }
    // Nasty hack because our threading is primitive.  interfaces shouldn't need to know about routers FIXME
    setReceivedMessage();
}

/// Generate a unique packet id
// FIXME, move this someplace better
PacketId generatePacketId()
{
    static uint32_t rollingPacketId; // Note: trying to keep this in noinit didn't help for working across reboots
    static bool didInit = false;

    if (!didInit) {
        didInit = true;

        // pick a random initial sequence number at boot (to prevent repeated reboots always starting at 0)
        // Note: we mask the high order bit to ensure that we never pass a 'negative' number to random
        rollingPacketId = random(UINT32_MAX & 0x7fffffff);
        LOG_DEBUG("Initial packet id %u", rollingPacketId);
    }

    rollingPacketId++;

    rollingPacketId &= ID_COUNTER_MASK;                                    // Mask out the top 22 bits
    PacketId id = rollingPacketId | random(UINT32_MAX & 0x7fffffff) << 10; // top 22 bits
    LOG_DEBUG("Partially randomized packet id %u", id);
    return id;
}

meshtastic_MeshPacket *Router::allocForSending()
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();

    p->which_payload_variant = meshtastic_MeshPacket_decoded_tag; // Assume payload is decoded at start.
    p->from = nodeDB->getNodeNum();
    p->to = NODENUM_BROADCAST;
    p->hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
    p->id = generatePacketId();
    p->rx_time =
        getValidTime(RTCQualityFromNet); // Just in case we process the packet locally - make sure it has a valid timestamp

    return p;
}

/**
 * Send an ack or a nak packet back towards whoever sent idFrom
 */
void Router::sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit)
{
    routingModule->sendAckNak(err, to, idFrom, chIndex, hopLimit);
}

void Router::abortSendAndNak(meshtastic_Routing_Error err, meshtastic_MeshPacket *p)
{
    LOG_ERROR("Error=%d, return NAK and drop packet", err);
    sendAckNak(err, getFrom(p), p->id, p->channel);
    packetPool.release(p);
}

void Router::setReceivedMessage()
{
    // LOG_DEBUG("set interval to ASAP");
    setInterval(0); // Run ASAP, so we can figure out our correct sleep time
    runASAP = true;
}

meshtastic_QueueStatus Router::getQueueStatus()
{
    if (!iface) {
        meshtastic_QueueStatus qs;
        qs.res = qs.mesh_packet_id = qs.free = qs.maxlen = 0;
        return qs;
    } else
        return iface->getQueueStatus();
}

ErrorCode Router::sendLocal(meshtastic_MeshPacket *p, RxSource src)
{
    if (p->to == 0) {
        LOG_ERROR("Packet received with to: of 0!");
    }
    // No need to deliver externally if the destination is the local node
    if (isToUs(p)) {
        printPacket("Enqueued local", p);
        enqueueReceivedMessage(p);
        return ERRNO_OK;
    } else if (!iface) {
        // We must be sending to remote nodes also, fail if no interface found
        abortSendAndNak(meshtastic_Routing_Error_NO_INTERFACE, p);

        return ERRNO_NO_INTERFACES;
    } else {
        // If we are sending a broadcast, we also treat it as if we just received it ourself
        // this allows local apps (and PCs) to see broadcasts sourced locally
        if (isBroadcast(p->to)) {
            handleReceived(p, src);
        }

        // don't override if a channel was requested and no need to set it when PKI is enforced
        if (!p->channel && !p->pki_encrypted && !isBroadcast(p->to)) {
            meshtastic_NodeInfoLite const *node = nodeDB->getMeshNode(p->to);
            if (node) {
                p->channel = node->channel;
                LOG_DEBUG("localSend to channel %d", p->channel);
            }
        }

        return send(p);
    }
}
/**
 * Send a packet on a suitable interface.
 */
ErrorCode Router::rawSend(meshtastic_MeshPacket *p)
{
    assert(iface); // This should have been detected already in sendLocal (or we just received a packet from outside)
    return iface->send(p);
}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error.
 */
ErrorCode Router::send(meshtastic_MeshPacket *p)
{
    if (isToUs(p)) {
        LOG_ERROR("BUG! send() called with packet destined for local node!");
        packetPool.release(p);
        return meshtastic_Routing_Error_BAD_REQUEST;
    } // should have already been handled by sendLocal

    // Abort sending if we are violating the duty cycle
    if (!config.lora.override_duty_cycle && myRegion->dutyCycle < 100) {
        float hourlyTxPercent = airTime->utilizationTXPercent();
        if (hourlyTxPercent > myRegion->dutyCycle) {
#ifdef DEBUG_PORT
            uint8_t silentMinutes = airTime->getSilentMinutes(hourlyTxPercent, myRegion->dutyCycle);
            LOG_WARN("Duty cycle limit exceeded. Aborting send for now, you can send again in %d mins", silentMinutes);
            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            cn->has_reply_id = true;
            cn->reply_id = p->id;
            cn->level = meshtastic_LogRecord_Level_WARNING;
            cn->time = getValidTime(RTCQualityFromNet);
            sprintf(cn->message, "Duty cycle limit exceeded. You can send again in %d mins", silentMinutes);
            service->sendClientNotification(cn);
#endif
            meshtastic_Routing_Error err = meshtastic_Routing_Error_DUTY_CYCLE_LIMIT;
            if (isFromUs(p)) { // only send NAK to API, not to the mesh
                abortSendAndNak(err, p);
            } else {
                packetPool.release(p);
            }
            return err;
        }
    }

    // PacketId nakId = p->decoded.which_ackVariant == SubPacket_fail_id_tag ? p->decoded.ackVariant.fail_id : 0;
    // assert(!nakId); // I don't think we ever send 0hop naks over the wire (other than to the phone), test that assumption with
    // assert

    // Never set the want_ack flag on broadcast packets sent over the air.
    if (isBroadcast(p->to))
        p->want_ack = false;

    // Up until this point we might have been using 0 for the from address (if it started with the phone), but when we send over
    // the lora we need to make sure we have replaced it with our local address
    p->from = getFrom(p);

    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // set the relayer to us
    // If we are the original transmitter, set the hop limit with which we start
    if (isFromUs(p))
        p->hop_start = p->hop_limit;

    // If the packet hasn't yet been encrypted, do so now (it might already be encrypted if we are just forwarding it)

    if (!(p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag ||
          p->which_payload_variant == meshtastic_MeshPacket_decoded_tag)) {
        return meshtastic_Routing_Error_BAD_REQUEST;
    }

    fixPriority(p); // Before encryption, fix the priority if it's unset

    // If the packet is not yet encrypted, do so now
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        ChannelIndex chIndex = p->channel; // keep as a local because we are about to change it
        meshtastic_MeshPacket *p_decoded = packetPool.allocCopy(*p);

        auto encodeResult = perhapsEncode(p);
        if (encodeResult != meshtastic_Routing_Error_NONE) {
            packetPool.release(p_decoded);
            p->channel = 0; // Reset the channel to 0, so we don't use the failing hash again
            abortSendAndNak(encodeResult, p);
            return encodeResult; // FIXME - this isn't a valid ErrorCode
        }
#if !MESHTASTIC_EXCLUDE_MQTT
        // Only publish to MQTT if we're the original transmitter of the packet
        if (moduleConfig.mqtt.enabled && isFromUs(p) && mqtt) {
            mqtt->onSend(*p, *p_decoded, chIndex);
        }
#endif
        packetPool.release(p_decoded);
    }

#if HAS_UDP_MULTICAST
    if (udpHandler && config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST) {
        udpHandler->onSend(const_cast<meshtastic_MeshPacket *>(p));
    }
#endif

    assert(iface); // This should have been detected already in sendLocal (or we just received a packet from outside)
    return iface->send(p);
}

/** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
bool Router::cancelSending(NodeNum from, PacketId id)
{
    if (iface && iface->cancelSending(from, id)) {
        // We are not a relayer of this packet anymore
        removeRelayer(nodeDB->getLastByteOfNodeNum(nodeDB->getNodeNum()), id, from);
        return true;
    }
    return false;
}

/** Attempt to find a packet in the TxQueue. Returns true if the packet was found. */
bool Router::findInTxQueue(NodeNum from, PacketId id)
{
    return iface->findInTxQueue(from, id);
}

/**
 * Every (non duplicate) packet this node receives will be passed through this method.  This allows subclasses to
 * update routing tables etc... based on what we overhear (even for messages not destined to our node)
 */
void Router::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    // FIXME, update nodedb here for any packet that passes through us
}

DecodeState perhapsDecode(meshtastic_MeshPacket *p)
{
    concurrency::LockGuard g(cryptLock);

    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER &&
        config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING)
        return DecodeState::DECODE_FAILURE;

    if (config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY &&
        (nodeDB->getMeshNode(p->from) == NULL || !nodeDB->getMeshNode(p->from)->has_user)) {
        LOG_DEBUG("Node 0x%x not in nodeDB-> Rebroadcast mode KNOWN_ONLY will ignore packet", p->from);
        return DecodeState::DECODE_FAILURE;
    }

    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag)
        return DecodeState::DECODE_SUCCESS; // If packet was already decoded just return

    size_t rawSize = p->encrypted.size;
    if (rawSize > sizeof(bytes)) {
        LOG_ERROR("Packet too large to attempt decryption! (rawSize=%d > 256)", rawSize);
        return DecodeState::DECODE_FATAL;
    }
    bool decrypted = false;
    ChannelIndex chIndex = 0;
#if !(MESHTASTIC_EXCLUDE_PKI)
    // Attempt PKI decryption first
    if (p->channel == 0 && isToUs(p) && p->to > 0 && !isBroadcast(p->to) && nodeDB->getMeshNode(p->from) != nullptr &&
        nodeDB->getMeshNode(p->from)->user.public_key.size > 0 && nodeDB->getMeshNode(p->to)->user.public_key.size > 0 &&
        rawSize > MESHTASTIC_PKC_OVERHEAD) {
        LOG_DEBUG("Attempt PKI decryption");

        if (crypto->decryptCurve25519(p->from, nodeDB->getMeshNode(p->from)->user.public_key, p->id, rawSize, p->encrypted.bytes,
                                      bytes)) {
            LOG_INFO("PKI Decryption worked!");

            meshtastic_Data decodedtmp;
            memset(&decodedtmp, 0, sizeof(decodedtmp));
            rawSize -= MESHTASTIC_PKC_OVERHEAD;
            if (pb_decode_from_bytes(bytes, rawSize, &meshtastic_Data_msg, &decodedtmp) &&
                decodedtmp.portnum != meshtastic_PortNum_UNKNOWN_APP) {
                decrypted = true;
                LOG_INFO("Packet decrypted using PKI!");
                p->pki_encrypted = true;
                memcpy(&p->public_key.bytes, nodeDB->getMeshNode(p->from)->user.public_key.bytes, 32);
                p->public_key.size = 32;
                p->decoded = decodedtmp;
                p->which_payload_variant = meshtastic_MeshPacket_decoded_tag; // change type to decoded
            } else {
                LOG_ERROR("PKC Decrypted, but pb_decode failed!");
                return DecodeState::DECODE_FAILURE;
            }
        } else {
            LOG_WARN("PKC decrypt attempted but failed!");
        }
    }
#endif

    // assert(p->which_payloadVariant == MeshPacket_encrypted_tag);
    if (!decrypted) {
        // Try to find a channel that works with this hash
        for (chIndex = 0; chIndex < channels.getNumChannels(); chIndex++) {
            // Try to use this hash/channel pair
            if (channels.decryptForHash(chIndex, p->channel)) {
                // we have to copy into a scratch buffer, because these bytes are a union with the decoded protobuf. Create a
                // fresh copy for each decrypt attempt.
                memcpy(bytes, p->encrypted.bytes, rawSize);
                // Try to decrypt the packet if we can
                crypto->decrypt(p->from, p->id, rawSize, bytes);

                // printBytes("plaintext", bytes, p->encrypted.size);

                // Take those raw bytes and convert them back into a well structured protobuf we can understand
                meshtastic_Data decodedtmp;
                memset(&decodedtmp, 0, sizeof(decodedtmp));
                if (!pb_decode_from_bytes(bytes, rawSize, &meshtastic_Data_msg, &decodedtmp)) {
                    LOG_ERROR("Invalid protobufs in received mesh packet id=0x%08x (bad psk?)!", p->id);
                } else if (decodedtmp.portnum == meshtastic_PortNum_UNKNOWN_APP) {
                    LOG_ERROR("Invalid portnum (bad psk?)!");
                } else {
                    p->decoded = decodedtmp;
                    p->which_payload_variant = meshtastic_MeshPacket_decoded_tag; // change type to decoded
                    decrypted = true;
                    break;
                }
            }
        }
    }
    if (decrypted) {
        // parsing was successful
        p->channel = chIndex; // change to store the index instead of the hash
        if (p->decoded.has_bitfield)
            p->decoded.want_response |= p->decoded.bitfield & BITFIELD_WANT_RESPONSE_MASK;

        /* Not actually ever used.
        // Decompress if needed. jm
        if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP) {
            // Decompress the payload
            char compressed_in[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
            char decompressed_out[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
            int decompressed_len;

            memcpy(compressed_in, p->decoded.payload.bytes, p->decoded.payload.size);

            decompressed_len = unishox2_decompress_simple(compressed_in, p->decoded.payload.size, decompressed_out);

            // LOG_DEBUG("**Decompressed length - %d ", decompressed_len);

            memcpy(p->decoded.payload.bytes, decompressed_out, decompressed_len);

            // Switch the port from PortNum_TEXT_MESSAGE_COMPRESSED_APP to PortNum_TEXT_MESSAGE_APP
            p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        } */

        printPacket("decoded message", p);
#if ENABLE_JSON_LOGGING
        LOG_TRACE("%s", MeshPacketSerializer::JsonSerialize(p, false).c_str());
#elif ARCH_PORTDUINO
        if (settingsStrings[traceFilename] != "" || settingsMap[logoutputlevel] == level_trace) {
            LOG_TRACE("%s", MeshPacketSerializer::JsonSerialize(p, false).c_str());
        }
#endif
        return DecodeState::DECODE_SUCCESS;
    } else {
        LOG_WARN("No suitable channel found for decoding, hash was 0x%x!", p->channel);
        return DecodeState::DECODE_FAILURE;
    }
}

/** Return 0 for success or a Routing_Error code for failure
 */
meshtastic_Routing_Error perhapsEncode(meshtastic_MeshPacket *p)
{
    concurrency::LockGuard g(cryptLock);

    int16_t hash;

    // If the packet is not yet encrypted, do so now
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        if (isFromUs(p)) {
            p->decoded.has_bitfield = true;
            p->decoded.bitfield |= (config.lora.config_ok_to_mqtt << BITFIELD_OK_TO_MQTT_SHIFT);
            p->decoded.bitfield |= (p->decoded.want_response << BITFIELD_WANT_RESPONSE_SHIFT);
        }

        size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_Data_msg, &p->decoded);

        /* Not actually used, so save the cycles
        //  TODO: Allow modules to opt into compression.
        if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {

            char original_payload[meshtastic_Constants_DATA_PAYLOAD_LEN];
            memcpy(original_payload, p->decoded.payload.bytes, p->decoded.payload.size);

            char compressed_out[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};

            int compressed_len;
            compressed_len = unishox2_compress_simple(original_payload, p->decoded.payload.size, compressed_out);

            LOG_DEBUG("Original length - %d ", p->decoded.payload.size);
            LOG_DEBUG("Compressed length - %d ", compressed_len);
            LOG_DEBUG("Original message - %s ", p->decoded.payload.bytes);

            // If the compressed length is greater than or equal to the original size, don't use the compressed form
            if (compressed_len >= p->decoded.payload.size) {

                LOG_DEBUG("Not using compressing message");
                // Set the uncompressed payload variant anyway. Shouldn't hurt?
                // p->decoded.which_payloadVariant = Data_payload_tag;

                // Otherwise we use the compressor
            } else {
                LOG_DEBUG("Use compressed message");
                // Copy the compressed data into the meshpacket

                p->decoded.payload.size = compressed_len;
                memcpy(p->decoded.payload.bytes, compressed_out, compressed_len);

                p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP;
            }
        } */

        if (numbytes + MESHTASTIC_HEADER_LENGTH > MAX_LORA_PAYLOAD_LEN)
            return meshtastic_Routing_Error_TOO_LARGE;

        // printBytes("plaintext", bytes, numbytes);

        ChannelIndex chIndex = p->channel; // keep as a local because we are about to change it

#if !(MESHTASTIC_EXCLUDE_PKI)
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(p->to);
        // We may want to retool things so we can send a PKC packet when the client specifies a key and nodenum, even if the node
        // is not in the local nodedb
        // First, only PKC encrypt packets we are originating
        if (isFromUs(p) &&
            // Don't use PKC with simulator
            radioType != SIM_RADIO &&
            // Don't use PKC with Ham mode
            !owner.is_licensed &&
            // Don't use PKC if it's not explicitly requested and a non-primary channel is requested
            !(p->pki_encrypted != true && p->channel > 0) &&
            // Check for valid keys and single node destination
            config.security.private_key.size == 32 && !isBroadcast(p->to) && node != nullptr &&
            // Check for a known public key for the destination
            (node->user.public_key.size == 32) &&
            // Some portnums either make no sense to send with PKC
            p->decoded.portnum != meshtastic_PortNum_TRACEROUTE_APP && p->decoded.portnum != meshtastic_PortNum_NODEINFO_APP &&
            p->decoded.portnum != meshtastic_PortNum_ROUTING_APP && p->decoded.portnum != meshtastic_PortNum_POSITION_APP) {
            LOG_DEBUG("Use PKI!");
            if (numbytes + MESHTASTIC_HEADER_LENGTH + MESHTASTIC_PKC_OVERHEAD > MAX_LORA_PAYLOAD_LEN)
                return meshtastic_Routing_Error_TOO_LARGE;
            if (p->pki_encrypted && !memfll(p->public_key.bytes, 0, 32) &&
                memcmp(p->public_key.bytes, node->user.public_key.bytes, 32) != 0) {
                LOG_WARN("Client public key differs from requested: 0x%02x, stored key begins 0x%02x", *p->public_key.bytes,
                         *node->user.public_key.bytes);
                return meshtastic_Routing_Error_PKI_FAILED;
            }
            crypto->encryptCurve25519(p->to, getFrom(p), node->user.public_key, p->id, numbytes, bytes, p->encrypted.bytes);
            numbytes += MESHTASTIC_PKC_OVERHEAD;
            p->channel = 0;
            p->pki_encrypted = true;

            // warn the user about a low entropy key
            if (nodeDB->keyIsLowEntropy) {
                LOG_WARN(LOW_ENTROPY_WARNING);
                if (!nodeDB->hasWarned) {
                    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
                    cn->level = meshtastic_LogRecord_Level_WARNING;
                    cn->time = getValidTime(RTCQualityFromNet);
                    sprintf(cn->message, LOW_ENTROPY_WARNING);
                    service->sendClientNotification(cn);
                    nodeDB->hasWarned = true;
                }
            }
        } else {
            if (p->pki_encrypted == true) {
                // Client specifically requested PKI encryption
                return meshtastic_Routing_Error_PKI_FAILED;
            }
            hash = channels.setActiveByIndex(chIndex);

            // Now that we are encrypting the packet channel should be the hash (no longer the index)
            p->channel = hash;
            if (hash < 0) {
                // No suitable channel could be found for sending
                return meshtastic_Routing_Error_NO_CHANNEL;
            }
            crypto->encryptPacket(getFrom(p), p->id, numbytes, bytes);
            memcpy(p->encrypted.bytes, bytes, numbytes);
        }
#else
        if (p->pki_encrypted == true) {
            // Client specifically requested PKI encryption
            return meshtastic_Routing_Error_PKI_FAILED;
        }
        hash = channels.setActiveByIndex(chIndex);

        // Now that we are encrypting the packet channel should be the hash (no longer the index)
        p->channel = hash;
        if (hash < 0) {
            // No suitable channel could be found for sending
            return meshtastic_Routing_Error_NO_CHANNEL;
        }
        crypto->encryptPacket(getFrom(p), p->id, numbytes, bytes);
        memcpy(p->encrypted.bytes, bytes, numbytes);
#endif

        // Copy back into the packet and set the variant type
        p->encrypted.size = numbytes;
        p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    }

    return meshtastic_Routing_Error_NONE;
}

NodeNum Router::getNodeNum()
{
    return nodeDB->getNodeNum();
}

/**
 * Handle any packet that is received by an interface on this node.
 * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
 */
void Router::handleReceived(meshtastic_MeshPacket *p, RxSource src)
{
    bool skipHandle = false;
    // Also, we should set the time from the ISR and it should have msec level resolution
    p->rx_time = getValidTime(RTCQualityFromNet); // store the arrival timestamp for the phone
    // Store a copy of encrypted packet for MQTT
    meshtastic_MeshPacket *p_encrypted = packetPool.allocCopy(*p);

    // Take those raw bytes and convert them back into a well structured protobuf we can understand
    auto decodedState = perhapsDecode(p);
    if (decodedState == DecodeState::DECODE_FATAL) {
        // Fatal decoding error, we can't do anything with this packet
        LOG_WARN("Fatal decode error, dropping packet");
        cancelSending(p->from, p->id);
        skipHandle = true;
    } else if (decodedState == DecodeState::DECODE_SUCCESS) {
        // parsing was successful, queue for our recipient
        if (src == RX_SRC_LOCAL)
            printPacket("handleReceived(LOCAL)", p);
        else if (src == RX_SRC_USER)
            printPacket("handleReceived(USER)", p);
        else
            printPacket("handleReceived(REMOTE)", p);

        // Neighbor info module is disabled, ignore expensive neighbor info packets
        if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            p->decoded.portnum == meshtastic_PortNum_NEIGHBORINFO_APP &&
            (!moduleConfig.has_neighbor_info || !moduleConfig.neighbor_info.enabled)) {
            LOG_DEBUG("Neighbor info module is disabled, ignore neighbor packet");
            cancelSending(p->from, p->id);
            skipHandle = true;
        }

        bool shouldIgnoreNonstandardPorts =
            config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY;
#if USERPREFS_EVENT_MODE
        shouldIgnoreNonstandardPorts = true;
#endif
        if (shouldIgnoreNonstandardPorts && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            IS_ONE_OF(p->decoded.portnum, meshtastic_PortNum_ATAK_FORWARDER, meshtastic_PortNum_ATAK_PLUGIN,
                      meshtastic_PortNum_PAXCOUNTER_APP, meshtastic_PortNum_IP_TUNNEL_APP, meshtastic_PortNum_AUDIO_APP,
                      meshtastic_PortNum_PRIVATE_APP, meshtastic_PortNum_DETECTION_SENSOR_APP, meshtastic_PortNum_RANGE_TEST_APP,
                      meshtastic_PortNum_REMOTE_HARDWARE_APP)) {
            LOG_DEBUG("Ignore packet on blacklisted portnum for CORE_PORTNUMS_ONLY");
            cancelSending(p->from, p->id);
            skipHandle = true;
        }
    } else {
        printPacket("packet decoding failed or skipped (no PSK?)", p);
    }

    // call modules here
    if (!skipHandle) {
        MeshModule::callModules(*p, src);

#if !MESHTASTIC_EXCLUDE_MQTT
        // Mark as pki_encrypted if it is not yet decoded and MQTT encryption is also enabled, hash matches and it's a DM not to
        // us (because we would be able to decrypt it)
        if (decodedState == DecodeState::DECODE_FAILURE && moduleConfig.mqtt.encryption_enabled && p->channel == 0x00 &&
            !isBroadcast(p->to) && !isToUs(p))
            p_encrypted->pki_encrypted = true;
        // After potentially altering it, publish received message to MQTT if we're not the original transmitter of the packet
        if ((decodedState == DecodeState::DECODE_SUCCESS || p_encrypted->pki_encrypted) && moduleConfig.mqtt.enabled &&
            !isFromUs(p) && mqtt)
            mqtt->onSend(*p_encrypted, *p, p->channel);
#endif
    }

    packetPool.release(p_encrypted); // Release the encrypted packet
}

void Router::perhapsHandleReceived(meshtastic_MeshPacket *p)
{
#if ENABLE_JSON_LOGGING
    // Even ignored packets get logged in the trace
    p->rx_time = getValidTime(RTCQualityFromNet); // store the arrival timestamp for the phone
    LOG_TRACE("%s", MeshPacketSerializer::JsonSerializeEncrypted(p).c_str());
#elif ARCH_PORTDUINO
    // Even ignored packets get logged in the trace
    if (settingsStrings[traceFilename] != "" || settingsMap[logoutputlevel] == level_trace) {
        p->rx_time = getValidTime(RTCQualityFromNet); // store the arrival timestamp for the phone
        LOG_TRACE("%s", MeshPacketSerializer::JsonSerializeEncrypted(p).c_str());
    }
#endif
    // assert(radioConfig.has_preferences);
    if (is_in_repeated(config.lora.ignore_incoming, p->from)) {
        LOG_DEBUG("Ignore msg, 0x%x is in our ignore list", p->from);
        packetPool.release(p);
        return;
    }

    meshtastic_NodeInfoLite const *node = nodeDB->getMeshNode(p->from);
    if (node != NULL && node->is_ignored) {
        LOG_DEBUG("Ignore msg, 0x%x is ignored", p->from);
        packetPool.release(p);
        return;
    }

    if (p->from == NODENUM_BROADCAST) {
        LOG_DEBUG("Ignore msg from broadcast address");
        packetPool.release(p);
        return;
    }

    if (config.lora.ignore_mqtt && p->via_mqtt) {
        LOG_DEBUG("Msg came in via MQTT from 0x%x", p->from);
        packetPool.release(p);
        return;
    }

    if (shouldFilterReceived(p)) {
        LOG_DEBUG("Incoming msg was filtered from 0x%x", p->from);
        packetPool.release(p);
        return;
    }

    // Note: we avoid calling shouldFilterReceived if we are supposed to ignore certain nodes - because some overrides might
    // cache/learn of the existence of nodes (i.e. FloodRouter) that they should not
    handleReceived(p);
    packetPool.release(p);
}
