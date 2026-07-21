#include "Router.h"
#include "Channels.h"
#include "CryptoEngine.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PositionPrecision.h"
#include "gps/RTC.h"

#include "configuration.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/RoutingModule.h"
#include <ErriezCRC32.h>
#include <pb_decode.h>
#include <pb_encode.h>
#if HAS_TRAFFIC_MANAGEMENT
#endif
#if HAS_VARIABLE_HOPS
#include "modules/HopScalingModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif
#include "Default.h"
#if ARCH_PORTDUINO
#include "Throttle.h"
#include "platform/portduino/PortduinoGlue.h"
#include "serialization/MeshPacketSerializer.h"
#endif

#define MAX_RX_FROMRADIO                                                                                                         \
    4 // max number of packets destined to our queue, we dispatch packets quickly so it doesn't need to be big

// I think this is right, one packet for each of the three fifos + one packet being currently assembled for TX or RX
// And every TX packet might have a retransmission packet or an ack alive at any moment

#ifdef ARCH_PORTDUINO
// Portduino (native) targets can use dynamic memory pools with runtime-configurable sizes
#define MAX_PACKETS                                                                                                              \
    (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + 2 * MAX_TX_QUEUE +                                                                      \
     2) // max number of packets which can be in flight (either queued from reception or queued for sending)

// Live in-flight packet bytes are tracked under "pktpool(live)" in the MemAudit breakdown
static MemoryDynamic<meshtastic_MeshPacket> dynamicPool("pktpool(live)");
Allocator<meshtastic_MeshPacket> &packetPool = dynamicPool;
#elif defined(ARCH_STM32WL) || defined(BOARD_HAS_PSRAM)
// On STM32 and boards with PSRAM, there isn't enough heap left over for the rest of the firmware if we allocate this statically.
// For now, make it dynamic again.
#define MAX_PACKETS                                                                                                              \
    (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + 2 * MAX_TX_QUEUE +                                                                      \
     2) // max number of packets which can be in flight (either queued from reception or queued for sending)

// Live in-flight packet bytes are tracked under "pktpool(live)" in the MemAudit breakdown
static MemoryDynamic<meshtastic_MeshPacket> dynamicPool("pktpool(live)");
Allocator<meshtastic_MeshPacket> &packetPool = dynamicPool;
#else
// Embedded targets use static memory pools with compile-time constants
#define MAX_PACKETS_STATIC                                                                                                       \
    (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + 2 * MAX_TX_QUEUE +                                                                      \
     2) // max number of packets which can be in flight (either queued from reception or queued for sending)

// Static pool RAM is BSS, not heap; "pktpool(live)" still shows in-flight packet bytes
static MemoryPool<meshtastic_MeshPacket, MAX_PACKETS_STATIC> staticPool("pktpool(live)");
Allocator<meshtastic_MeshPacket> &packetPool = staticPool;
#endif

static uint8_t bytes[MAX_LORA_PAYLOAD_LEN + 1] __attribute__((__aligned__));

struct RoutingAuthCache {
    bool valid = false;
    meshtastic_Config_SecurityConfig_PacketSignaturePolicy policy =
        meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED;
    meshtastic_MeshPacket wire = meshtastic_MeshPacket_init_zero;
    meshtastic_MeshPacket authenticated = meshtastic_MeshPacket_init_zero;
};
static RoutingAuthCache routingAuthCache;
static concurrency::Lock *routingAuthCacheLock;
static uint32_t routingAuthEvaluations;

static bool routingAuthCacheMatches(const meshtastic_MeshPacket &packet)
{
    if (!routingAuthCacheLock)
        return false;
    concurrency::LockGuard guard(routingAuthCacheLock);
    if (!routingAuthCache.valid)
        return false;
    if (routingAuthCache.policy != config.security.packet_signature_policy ||
        memcmp(&routingAuthCache.wire, &packet, sizeof(packet)) != 0) {
        routingAuthCache.valid = false;
        return false;
    }
    return true;
}

static void storeRoutingAuthCache(const meshtastic_MeshPacket &wire, const meshtastic_MeshPacket &authenticated)
{
    concurrency::LockGuard guard(routingAuthCacheLock);
    routingAuthCache.wire = wire;
    routingAuthCache.authenticated = authenticated;
    routingAuthCache.policy = config.security.packet_signature_policy;
    routingAuthCache.valid = true;
}

static bool applyRoutingAuthCache(meshtastic_MeshPacket *packet)
{
    if (!routingAuthCacheLock)
        return false;
    concurrency::LockGuard guard(routingAuthCacheLock);
    if (!routingAuthCache.valid || routingAuthCache.policy != config.security.packet_signature_policy ||
        memcmp(&routingAuthCache.wire, packet, sizeof(*packet)) != 0) {
        routingAuthCache.valid = false;
        return false;
    }
    *packet = routingAuthCache.authenticated;
    routingAuthCache.valid = false;
    return true;
}

static void clearRoutingAuthCache()
{
    if (!routingAuthCacheLock)
        return;
    concurrency::LockGuard guard(routingAuthCacheLock);
    routingAuthCache.valid = false;
}

#ifdef PIO_UNIT_TESTING
uint32_t routingAuthEvaluationCount()
{
    return routingAuthEvaluations;
}
void resetRoutingAuthEvaluationCount()
{
    routingAuthEvaluations = 0;
    if (routingAuthCacheLock) {
        concurrency::LockGuard guard(routingAuthCacheLock);
        routingAuthCache.valid = false;
    }
}
#endif

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
    if (!routingAuthCacheLock)
        routingAuthCacheLock = new concurrency::Lock();
}

bool Router::shouldDecrementHopLimit(const meshtastic_MeshPacket *p)
{
    // First hop MUST always decrement to prevent retry issues
    if (getHopsAway(*p) == 0) {
        return true; // Always decrement on first hop
    }

    // Check if both local device and previous relay are routers (including CLIENT_BASE)
    bool localIsRouter =
        IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER, meshtastic_Config_DeviceConfig_Role_ROUTER_LATE,
                  meshtastic_Config_DeviceConfig_Role_CLIENT_BASE);

    // If local device isn't a router, always decrement
    if (!localIsRouter) {
        return true;
    }

    // router_preserve_hops: not suitable right now - removed from config until
    // the right heuristics for when to preserve vs. exhaust hops are established.
    // #if HAS_TRAFFIC_MANAGEMENT
    //     if (moduleConfig.has_traffic_management &&
    //         moduleConfig.traffic_management.router_preserve_hops && ...) { ... }
    // #endif

    // For subsequent hops, preserve hop_limit only when the previous relay is UNAMBIGUOUSLY a favorite
    // router. The relay_node byte is just the last byte of a 32-bit node number, so on a dense mesh it
    // collides; the old "first matching node wins" scan could preserve hops for the wrong node
    // (non-deterministic, depends on NodeDB order). resolveLastByte() reports a collision instead, and
    // we re-check the favorite/router predicate on the single resolved node. On ambiguity/none we
    // decrement (the safe default).
    NodeNum resolved = 0;
    if (nodeDB->resolveUniqueLastByte(p->relay_node, /*requireDirectNeighbor=*/false, &resolved)) {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(resolved);
        if (node && nodeInfoLiteIsFavorite(node) && nodeInfoLiteHasUser(node) &&
            IS_ONE_OF(node->role, meshtastic_Config_DeviceConfig_Role_ROUTER, meshtastic_Config_DeviceConfig_Role_ROUTER_LATE,
                      meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)) {
            LOG_DEBUG("Identified unique favorite relay router 0x%08x from last byte 0x%x", resolved, p->relay_node);
            return false; // Don't decrement hop_limit
        }
    }

    // No unambiguous favorite router match found, decrement hop_limit
    return true;
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
    if (!p)
        return nullptr;

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
void Router::sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit,
                        bool ackWantsAck)
{
    routingModule->sendAckNak(err, to, idFrom, chIndex, hopLimit, ackWantsAck);
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
        // Preserve the trusted origin explicitly. Queueing used to erase src and make a local
        // phone/module packet indistinguishable from remote already-decoded ingress.
        handleReceived(p, src);
        return ERRNO_SHOULD_RELEASE;
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

        // If someone asks for acks on broadcast, we need the hop limit to be at least one, so that first node that receives our
        // message will rebroadcast.  But asking for hop_limit 0 in that context means the client app has no preference on hop
        // counts and we want this message to get through the whole mesh, so use the default.
        if (src == RX_SRC_USER && p->want_ack && p->hop_limit == 0) {
            p->hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
        }

        return send(p);
    }
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
    float effectiveDutyCycle = getEffectiveDutyCycle();
    if (!config.lora.override_duty_cycle && effectiveDutyCycle < 100) {
        float hourlyTxPercent = airTime->utilizationTXPercent();
        if (hourlyTxPercent > effectiveDutyCycle) {
            uint8_t silentMinutes = airTime->getSilentMinutes(hourlyTxPercent, effectiveDutyCycle);

            LOG_WARN("Duty cycle limit exceeded. Aborting send for now, you can send again in %d mins", silentMinutes);

            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            if (cn) {
                cn->has_reply_id = true;
                cn->reply_id = p->id;
                cn->level = meshtastic_LogRecord_Level_WARNING;
                cn->time = getValidTime(RTCQualityFromNet);
                snprintf(cn->message, sizeof(cn->message), "Duty cycle limit exceeded. You can send again in %d mins",
                         silentMinutes);
                service->sendClientNotification(cn);
            }

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

#if HAS_VARIABLE_HOPS
    // Apply HopScaling hop recommendation to routine outgoing broadcasts
    if (isFromUs(p) && isBroadcast(p->to) && hopScalingModule && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        switch (p->decoded.portnum) {
        case meshtastic_PortNum_POSITION_APP:
        case meshtastic_PortNum_TELEMETRY_APP:
        case meshtastic_PortNum_NODEINFO_APP:
        case meshtastic_PortNum_NEIGHBORINFO_APP: {
            uint8_t variableHopLimit = hopScalingModule->getLastRequiredHop();

            // Never exceed user-configured hop_limit
            if (variableHopLimit < p->hop_limit) {
                LOG_DEBUG("[HOPSCALE] hop_limit %u -> %u for portnum %u", p->hop_limit, variableHopLimit, p->decoded.portnum);
                p->hop_limit = variableHopLimit;
            }
            break;
        }
        default:
            break;
        }
    }
#endif

    // If we are the original transmitter, set the hop limit with which we start
    if (isFromUs(p))
        p->hop_start = p->hop_limit;

    // If the packet hasn't yet been encrypted, do so now (it might already be encrypted if we are just forwarding it)

    if (!(p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag ||
          p->which_payload_variant == meshtastic_MeshPacket_decoded_tag)) {
        return meshtastic_Routing_Error_BAD_REQUEST;
    }

    fixPriority(p); // Before encryption, fix the priority if it's unset
    // Position precision is an originator-only privacy policy. Relays keep
    // p->from as the original sender, so do not rewrite their POSITION_APP payload.
    if (isFromUs(p)) {
        if (!applyPositionPrecisionForChannel(*p, p->channel)) {
            LOG_ERROR("Dropping malformed position packet before send");
            packetPool.release(p);
            return meshtastic_Routing_Error_BAD_REQUEST;
        }
    }

    // If the packet is not yet encrypted, do so now
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        ChannelIndex chIndex = p->channel; // keep as a local because we are about to change it

        DEBUG_HEAP_BEFORE;
        meshtastic_MeshPacket *p_decoded = packetPool.allocCopy(*p);
        DEBUG_HEAP_AFTER("Router::send", p_decoded);

        auto encodeResult = perhapsEncode(p);
        if (encodeResult != meshtastic_Routing_Error_NONE) {
            packetPool.release(p_decoded);
            p->channel = 0; // Reset the channel to 0, so we don't use the failing hash again
            abortSendAndNak(encodeResult, p);
            return encodeResult; // FIXME - this isn't a valid ErrorCode
        }
#if !MESHTASTIC_EXCLUDE_MQTT
        // Only publish to MQTT if we're the original transmitter of the packet
        if (moduleConfig.mqtt.enabled && isFromUs(p) && mqtt && p_decoded) {
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

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
/** Size a decoded Data as the sender's signedDataFits() gate would have, with padding stripped:
 * unknown fields inside Data.payload survive in payload.size and would otherwise let a forger
 * inflate an unsigned broadcast past the signable budget. Returns false only if sizing failed.
 * Sizing only what this build's schema decodes, so a signable type that later grows needs its
 * legitimate maximum re-checked against the budget or honest unsigned broadcasts get dropped. */
static bool canonicalSignableSize(meshtastic_Data *d, size_t *size)
{
    const pb_msgdesc_t *fields = nullptr;
    switch (d->portnum) {
    case meshtastic_PortNum_POSITION_APP:
        fields = &meshtastic_Position_msg;
        break;
    case meshtastic_PortNum_TELEMETRY_APP:
        fields = &meshtastic_Telemetry_msg;
        break;
    case meshtastic_PortNum_WAYPOINT_APP:
        fields = &meshtastic_Waypoint_msg;
        break;
    case meshtastic_PortNum_NODEINFO_APP:
        fields = &meshtastic_User_msg;
        break;
    default:
        break;
    }

    if (fields) {
        // Scratch kept off the stack: these decoded structs are large for the smaller MCU targets.
        // Safe as file-static state because both callers of checkXeddsaReceivePolicy hold cryptLock.
        static union {
            // cppcheck-suppress unusedStructMember ; written by pb_decode through &inner
            meshtastic_Position position;
            // cppcheck-suppress unusedStructMember ; written by pb_decode through &inner
            meshtastic_Telemetry telemetry;
            // cppcheck-suppress unusedStructMember ; written by pb_decode through &inner
            meshtastic_Waypoint waypoint;
            // cppcheck-suppress unusedStructMember ; written by pb_decode through &inner
            meshtastic_User user;
        } inner;

        memset(&inner, 0, sizeof(inner));
        size_t canonicalPayload;
        if (pb_decode_from_bytes(d->payload.bytes, d->payload.size, fields, &inner) &&
            pb_get_encoded_size(&canonicalPayload, fields, &inner) && canonicalPayload <= d->payload.size) {
            // Only the length matters when sizing a bytes field, so swap it in place instead of
            // copying the whole Data; restored below because modules still need the real payload.
            const pb_size_t prevSize = d->payload.size;
            d->payload.size = (pb_size_t)canonicalPayload;
            const bool sized = pb_get_encoded_size(size, &meshtastic_Data_msg, d);
            d->payload.size = prevSize;
            return sized;
        }
    }

    return pb_get_encoded_size(size, &meshtastic_Data_msg, d);
}

enum class NodeInfoBootstrapResult { NOT_APPLICABLE, VERIFIED, INVALID };

static NodeInfoBootstrapResult verifyFirstContactNodeInfo(meshtastic_MeshPacket *p)
{
    if (p->decoded.portnum != meshtastic_PortNum_NODEINFO_APP)
        return NodeInfoBootstrapResult::NOT_APPLICABLE;

    meshtastic_User user = meshtastic_User_init_zero;
    if (!pb_decode_from_bytes(p->decoded.payload.bytes, p->decoded.payload.size, &meshtastic_User_msg, &user) ||
        user.public_key.size != 32 || crc32Buffer(user.public_key.bytes, user.public_key.size) != p->from ||
        !crypto->xeddsa_verify(user.public_key.bytes, p->from, p->id, p->decoded.portnum, p->decoded.payload.bytes,
                               p->decoded.payload.size, p->decoded.xeddsa_signature.bytes)) {
        return NodeInfoBootstrapResult::INVALID;
    }

    meshtastic_NodeInfoLite *node = nodeDB->getOrCreateMeshNode(p->from);
    if (!node)
        return NodeInfoBootstrapResult::INVALID;
    node->public_key.size = user.public_key.size;
    memcpy(node->public_key.bytes, user.public_key.bytes, user.public_key.size);
    nodeInfoLiteSetBit(node, NODEINFO_BITFIELD_HAS_XEDDSA_SIGNED_MASK, true);
    p->xeddsa_signed = true;
    LOG_DEBUG("Verified first-contact XEdDSA NodeInfo from 0x%08x", p->from);
    return NodeInfoBootstrapResult::VERIFIED;
}

bool checkXeddsaReceivePolicy(meshtastic_MeshPacket *p)
{
    const auto policy = config.security.packet_signature_policy;
    const bool strict = policy == meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT;
    const bool compatible = policy == meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE;

    // Only a signature we verify below may mark this packet signed; never trust an inbound flag.
    p->xeddsa_signed = false;
    if (p->decoded.xeddsa_signature.size == XEDDSA_SIGNATURE_SIZE) {
        meshtastic_NodeInfoLite_public_key_t senderKey = {0, {0}};
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(p->from);
        if (nodeDB->copyPublicKey(p->from, senderKey)) {
            p->xeddsa_signed =
                crypto->xeddsa_verify(senderKey.bytes, p->from, p->id, p->decoded.portnum, p->decoded.payload.bytes,
                                      p->decoded.payload.size, p->decoded.xeddsa_signature.bytes);
            if (p->xeddsa_signed) {
                // Learn this node as a signer, so a later unsigned signable broadcast from it is dropped
                // A warm-tier key must be re-admitted before setting the signer bit; otherwise Balanced
                // forgets downgrade protection as soon as the node is evicted from the hot store.
                if (!node)
                    node = nodeDB->getOrCreateMeshNode(p->from);
                if (!node)
                    return false;
                nodeInfoLiteSetBit(node, NODEINFO_BITFIELD_HAS_XEDDSA_SIGNED_MASK, true);
                LOG_DEBUG("Verified XEdDSA signature from 0x%08x", p->from);
            } else {
                LOG_WARN("XEdDSA signature verification failed from 0x%08x, dropping", p->from);
                return false;
            }
        } else {
            const auto bootstrap = verifyFirstContactNodeInfo(p);
            if (bootstrap == NodeInfoBootstrapResult::INVALID) {
                LOG_WARN("Invalid first-contact XEdDSA NodeInfo from 0x%08x, dropping", p->from);
                return false;
            }
            if (bootstrap == NodeInfoBootstrapResult::VERIFIED)
                return true;
            LOG_DEBUG("No public key for 0x%08x, cannot verify XEdDSA signature", p->from);
            if (strict)
                return false;
        }
    } else if (p->decoded.xeddsa_signature.size != 0) {
        // A signature field that is neither empty nor a full 64 bytes is malformed - honest
        // senders emit only those two sizes (perhapsEncode sets 0 or XEDDSA_SIGNATURE_SIZE). Drop
        // it: a crafted partial signature would otherwise land in the unsigned branch below while
        // its bytes inflated the size estimate, letting a forged broadcast dodge the downgrade drop.
        LOG_WARN("Malformed XEdDSA signature (%u bytes) from 0x%08x, dropping", (unsigned)p->decoded.xeddsa_signature.size,
                 p->from);
        return false;
    } else {
        if (p->pki_encrypted)
            return true;
        if (strict) {
            LOG_WARN("Dropping unsigned packet from 0x%08x in Strict signature mode", p->from);
            return false;
        }
        if (compatible)
            return true;

        // In Balanced, preserve legacy unsigned-unicast compatibility and only reject the class a
        // signing node always signs: a non-PKI broadcast whose signed encoding would still fit the
        // LoRa frame. Canonical sizing removes unknown protobuf fields before mirroring the
        // sender-side signedDataFits() gate, so this counts the same fields that gate counted.
        // Unicast packets and broadcasts too big to carry a signature are never signed, so they
        // must not be hard-failed here even for a known signer (PKI already returned above).
        // isKnownXeddsaSigner consults the warm tier too: a signer evicted from the hot store
        // must not become impersonatable via unsigned broadcasts until it is re-heard.
        if (nodeDB->isKnownXeddsaSigner(p->from) && isBroadcast(p->to)) {
            size_t canonicalSize;
            if (!canonicalSignableSize(&p->decoded, &canonicalSize))
                return true; // can't size it; never drop on a sizing failure
            if (canonicalSize + XEDDSA_SIGNATURE_FIELD_BYTES + MESHTASTIC_HEADER_LENGTH <= MAX_LORA_PAYLOAD_LEN) {
                LOG_WARN("Dropping unsigned broadcast from 0x%08x that previously signed", p->from);
                return false;
            }
        }
    }
    return true;
}
#endif

RoutingAuthVerdict passesRoutingAuthGate(meshtastic_MeshPacket *p)
{
    // Routing still needs the original encrypted representation for byte-for-byte relay and for
    // MQTT uplink. Authenticate a copy here; handleReceived() performs the normal in-place decode
    // only after stateful routing filters have completed.
    if (routingAuthCacheMatches(*p))
        return RoutingAuthVerdict::ACCEPT;

    meshtastic_MeshPacket wire = *p;
    meshtastic_MeshPacket authCandidate = *p;
    routingAuthEvaluations++;
    if (authCandidate.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        // Already-decoded remote ingress (notably Portduino SimRadio) did not pass through a
        // decryptor. Never trust serialized local authentication metadata on that boundary.
        authCandidate.pki_encrypted = false;
        authCandidate.public_key.size = 0;
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
        concurrency::LockGuard g(cryptLock);
        if (!checkXeddsaReceivePolicy(&authCandidate)) {
            LOG_WARN("Already-decoded packet rejected by signature policy before routing state update");
            return RoutingAuthVerdict::REJECT;
        }
#endif
        p->xeddsa_signed = authCandidate.xeddsa_signed;
        wire = *p;
        storeRoutingAuthCache(wire, authCandidate);
        return RoutingAuthVerdict::ACCEPT;
    }
    const DecodeState state = perhapsDecode(&authCandidate);
    if (state == DecodeState::DECODE_POLICY_REJECT) {
        LOG_WARN("Packet rejected by signature policy before routing state update");
        return RoutingAuthVerdict::REJECT;
    }
    if (state == DecodeState::DECODE_FATAL) {
        LOG_WARN("Fatal decode error before routing state update");
        return RoutingAuthVerdict::REJECT;
    }
    if (state == DecodeState::DECODE_FAILURE) {
        LOG_WARN("Decryptable packet failed decoding/authentication before routing state update");
        return RoutingAuthVerdict::REJECT;
    }

    // Only an explicit unknown-channel result remains eligible for opaque relay.
    if (state == DecodeState::DECODE_OPAQUE)
        return RoutingAuthVerdict::OPAQUE_RELAY_ONLY;
    storeRoutingAuthCache(wire, authCandidate);
    return RoutingAuthVerdict::ACCEPT;
}

#if !(MESHTASTIC_EXCLUDE_PKI)
// The fallback costs three X25519 ops before the AEAD tag is checked. Budget is global because p->from is
// attacker-controlled; successful runs refund, and their key is then persisted for the fast path.
#define ADMIN_KEY_FALLBACK_BURST 8
#define ADMIN_KEY_FALLBACK_REFILL_MS 250

static uint32_t adminKeyFallbackTokens = ADMIN_KEY_FALLBACK_BURST;
static uint32_t adminKeyFallbackRefillMs = 0;

static bool adminKeyFallbackAllowed()
{
    bool haveAdminKey = false;
    for (int i = 0; i < 3; i++) {
        if (config.security.admin_key[i].size == 32) {
            haveAdminKey = true;
            break;
        }
    }
    if (!haveAdminKey)
        return false; // nothing to try, so do not spend a token

    uint32_t now = millis();
    if (adminKeyFallbackRefillMs == 0)
        adminKeyFallbackRefillMs = now;
    uint32_t elapsed = now - adminKeyFallbackRefillMs;
    if (elapsed >= ADMIN_KEY_FALLBACK_REFILL_MS) {
        uint32_t refill = elapsed / ADMIN_KEY_FALLBACK_REFILL_MS;
        adminKeyFallbackRefillMs += refill * ADMIN_KEY_FALLBACK_REFILL_MS;
        if (refill >= ADMIN_KEY_FALLBACK_BURST - adminKeyFallbackTokens)
            adminKeyFallbackTokens = ADMIN_KEY_FALLBACK_BURST;
        else
            adminKeyFallbackTokens += refill;
    }

    if (adminKeyFallbackTokens == 0)
        return false;

    adminKeyFallbackTokens--;
    return true;
}

static void adminKeyFallbackRefund()
{
    if (adminKeyFallbackTokens < ADMIN_KEY_FALLBACK_BURST)
        adminKeyFallbackTokens++;
}
#endif

DecodeState perhapsDecode(meshtastic_MeshPacket *p)
{
    concurrency::LockGuard g(cryptLock);

    if (config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY &&
        !nodeInfoLiteHasUser(nodeDB->getMeshNode(p->from))) {
        LOG_DEBUG("Node 0x%08x not in nodeDB-> Rebroadcast mode KNOWN_ONLY will ignore packet", p->from);
        return DecodeState::DECODE_FAILURE;
    }

    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag)
        return DecodeState::DECODE_SUCCESS; // If packet was already decoded just return

    // Authentication metadata is local-only. Re-establish it below only after successful PKI decryption.
    p->pki_encrypted = false;
    p->public_key.size = 0;

    size_t rawSize = p->encrypted.size;
    if (rawSize > sizeof(bytes)) {
        LOG_ERROR("Packet too large to attempt decryption! (rawSize=%d > 256)", rawSize);
        return DecodeState::DECODE_FATAL;
    }
    bool decrypted = false;
    bool pkiAttempted = false;
    bool matchedChannel = false;
    ChannelIndex chIndex = 0;
#if !(MESHTASTIC_EXCLUDE_PKI)
    meshtastic_NodeInfoLite *ourNode = nullptr;
    if (p->channel == 0 && isToUs(p) && p->to > 0 && !isBroadcast(p->to) && rawSize > MESHTASTIC_PKC_OVERHEAD &&
        (ourNode = nodeDB->getMeshNode(p->to)) != nullptr && ourNode->public_key.size > 0) {
        pkiAttempted = true;
        LOG_DEBUG("Attempt PKI decryption");
        // Resolve the sender's public key only for actual PKI-decrypt candidates: prefer NodeDB
        // (hot store or warm tier), else a not-yet-committed key held during an in-progress
        // key-verification handshake. On a full NodeDB miss, copyPublicKey() falls through to a
        // linear scan of TrafficManagement's large NodeInfo cache, so it must not run for every
        // encrypted channel packet from an unknown sender - only for packets we might decrypt.
        meshtastic_NodeInfoLite_public_key_t remotePublic = {0, {0}};
        bool haveRemoteKey = nodeDB->copyPublicKey(p->from, remotePublic);
        // A pending key is an unverified identity claim supplied by whoever opened the handshake, so it is
        // accepted only for the exchange itself (checked after decode). perhapsEncode applies the same rule.
        bool havePendingKey = false;
        if (!haveRemoteKey) {
            havePendingKey = crypto->getPendingPublicKey(p->from, remotePublic);
            haveRemoteKey = havePendingKey;
        }
        // Try the sender's known key first, then each configured admin key so an authorized admin can
        // reach a node that has not yet learned their key. AES-CCM AEAD rejects wrong candidates.
        bool viaAdminKey = false;
        bool viaPendingKey = false;
        if (haveRemoteKey && crypto->decryptCurve25519(p->from, remotePublic, p->id, rawSize, p->encrypted.bytes, bytes)) {
            decrypted = true;
            viaPendingKey = havePendingKey;
        }
        if (!decrypted && adminKeyFallbackAllowed()) {
            for (int i = 0; i < 3 && !decrypted; i++) {
                if (config.security.admin_key[i].size != 32)
                    continue;
                remotePublic.size = 32;
                memcpy(remotePublic.bytes, config.security.admin_key[i].bytes, 32);

                if (crypto->decryptCurve25519(p->from, remotePublic, p->id, rawSize, p->encrypted.bytes, bytes)) {
                    decrypted = true;
                    viaAdminKey = true;
                    break; // stop after first successful decryption
                }
            }
            if (decrypted)
                adminKeyFallbackRefund();
        }
        if (decrypted) {
            LOG_INFO("PKI Decryption worked!");
            meshtastic_Data decodedtmp;
            memset(&decodedtmp, 0, sizeof(decodedtmp));
            size_t payloadSize = rawSize - MESHTASTIC_PKC_OVERHEAD;
            if (pb_decode_from_bytes(bytes, payloadSize, &meshtastic_Data_msg, &decodedtmp) &&
                decodedtmp.portnum != meshtastic_PortNum_UNKNOWN_APP) {
                if (viaPendingKey && decodedtmp.portnum != meshtastic_PortNum_KEY_VERIFICATION_APP) {
                    // The pending key only proves the handshake initiator holds it, not that they are
                    // p->from. Beyond the exchange it would let them send DMs that look authenticated.
                    LOG_WARN("Refusing pending-key decrypt of port %u from 0x%08x", (unsigned)decodedtmp.portnum, p->from);
                    return DecodeState::DECODE_FAILURE;
                }
                decrypted = true;
                rawSize = payloadSize; // commit the overhead subtraction only on full success
                LOG_INFO("Packet decrypted using PKI!");
                p->pki_encrypted = true;
                memcpy(p->public_key.bytes, remotePublic.bytes, 32);
                p->public_key.size = 32;
                p->decoded = decodedtmp;
                p->which_payload_variant = meshtastic_MeshPacket_decoded_tag; // change type to decoded
                if (viaAdminKey) {
                    // Persist the admin key for the sender so future packets take the fast path and we can
                    // PKI-reply; p->from is bound into the AEAD nonce, so the trusted admin authenticated
                    // it. commitRemoteKey is the bare-key commit primitive: it bypasses updateUser's
                    // User-payload path deliberately and handles the TrafficManagement write-through.
                    // AdminChannelProven = possession shown to the admin channel, not via an XEdDSA
                    // NodeInfo signature, so the key stays TOFU-grade for signing purposes.
                    nodeDB->commitRemoteKey(p->from, remotePublic.bytes, NodeDB::KeyCommitTrust::AdminChannelProven);
                }
            } else {
                // AEAD already authenticated this ciphertext, so no other candidate could decode it -
                // the payload is simply malformed.
                LOG_ERROR("PKC Decrypted, but pb_decode failed!");
                return DecodeState::DECODE_FAILURE;
            }
        }
    }
#endif

    // assert(p->which_payloadVariant == MeshPacket_encrypted_tag);
    if (!decrypted) {
        // Try to find a channel that works with this hash
        for (chIndex = 0; chIndex < channels.getNumChannels(); chIndex++) {
            // Try to use this hash/channel pair
            if (channels.decryptForHash(chIndex, p->channel)) {
                matchedChannel = true;
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
                    LOG_DEBUG("Invalid protobufs in received mesh packet id=0x%08x (bad psk?)", p->id);
                } else if (decodedtmp.portnum == meshtastic_PortNum_UNKNOWN_APP) {
                    LOG_DEBUG("Invalid portnum (bad psk?)");
#if !(MESHTASTIC_EXCLUDE_PKI)
                } else if (!owner.is_licensed && isToUs(p) && decodedtmp.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
                    LOG_WARN("Rejecting legacy DM");
                    return DecodeState::DECODE_FAILURE;
#endif
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

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
        // Run before merging local-only bitfield state into the decoded Data.
        if (!checkXeddsaReceivePolicy(p))
            return DecodeState::DECODE_POLICY_REJECT;
#endif

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
#if ARCH_PORTDUINO
        if (portduino_config.traceFilename != "" || portduino_config.logoutputlevel == level_trace) {
            LOG_TRACE("%s", MeshPacketSerializer::JsonSerialize(p, false).c_str());
        } else if (portduino_config.JSONFilename != "") {
            if (portduino_config.JSONFileRotate != 0) {
                static uint32_t fileage = 0;

                if (portduino_config.JSONFileRotate != 0 &&
                    (fileage == 0 || !Throttle::isWithinTimespanMs(fileage, portduino_config.JSONFileRotate * 60 * 1000))) {
                    time_t timestamp = time(NULL);
                    struct tm *timeinfo;
                    char buffer[80];
                    timeinfo = localtime(&timestamp);
                    strftime(buffer, 80, "%Y%m%d-%H%M%S", timeinfo);

                    std::string datetime(buffer);
                    if (JSONFile.is_open()) {
                        JSONFile.close();
                    }
                    JSONFile.open(portduino_config.JSONFilename + "_" + datetime, std::ios::out | std::ios::app);
                    fileage = millis();
                }
            }
            if (portduino_config.JSONFilter == (_meshtastic_PortNum)0 || portduino_config.JSONFilter == p->decoded.portnum) {
                JSONFile << MeshPacketSerializer::JsonSerialize(p, false) << std::endl;
            }
        }
#endif
        return DecodeState::DECODE_SUCCESS;
    } else {
        LOG_WARN("No suitable channel found for decoding, hash was 0x%x!", p->channel);
        return (matchedChannel || pkiAttempted) ? DecodeState::DECODE_FAILURE : DecodeState::DECODE_OPAQUE;
    }
}

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
/** Exact sender-side sign gate: would this Data still fit the LoRa frame with a 64-byte
 * signature attached? Sized with the real encoder so it tracks whatever fields are present. */
static bool signedDataFits(meshtastic_Data *d)
{
    const pb_size_t prevSize = d->xeddsa_signature.size;
    d->xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
    size_t encodedSize;
    const bool sized = pb_get_encoded_size(&encodedSize, &meshtastic_Data_msg, d);
    d->xeddsa_signature.size = prevSize;
    return sized && encodedSize + MESHTASTIC_HEADER_LENGTH <= MAX_LORA_PAYLOAD_LEN;
}
#endif

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
            // We own signing for packets we originate; discard any signature a client preset.
            // Outside the XEdDSA guard: the field exists in the protobuf on every build, and a
            // stale/garbage signature transmitted by a non-signing build would hard-fail
            // verification at every XEdDSA-enabled receiver that knows our key.
            p->decoded.xeddsa_signature.size = 0;
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
            // Sign broadcast packets when the Data still fits a LoRa frame with the signature
            // attached. This must be the exact encoded-size criterion, not a payload-size
            // heuristic: a heuristic band where we sign-then-fail-TOO_LARGE breaks packets that
            // were deliverable unsigned, and perhapsDecode() applies the mirror-image rule when
            // deciding whether an unsigned broadcast from a known signer is a downgrade.
            if (!p->pki_encrypted && isBroadcast(p->to) && signedDataFits(&p->decoded)) {
                if (crypto->xeddsa_sign(p->from, p->id, p->decoded.portnum, p->decoded.payload.bytes, p->decoded.payload.size,
                                        p->decoded.xeddsa_signature.bytes)) {
                    p->decoded.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
                    LOG_DEBUG("XEdDSA signed packet 0x%08x", p->id);
                }
            }
#endif
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
            LOG_DEBUG("Original message - %.*s ", (int)p->decoded.payload.size, p->decoded.payload.bytes);

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
        // Resolve the destination's public key: prefer NodeDB (hot store or warm tier - evicted
        // long-tail nodes keep their key there), otherwise (for a key-verification follow-on packet
        // that explicitly requested PKI) fall back to the not-yet-verified key held during an
        // in-progress handshake. This lets us DH-encode the follow-on packet before the peer's key
        // has been committed to NodeDB.
        meshtastic_NodeInfoLite_public_key_t destKey = {0, {0}};
        bool haveDestKey = nodeDB->copyPublicKey(p->to, destKey);
        if (!haveDestKey && p->pki_encrypted && p->decoded.portnum == meshtastic_PortNum_KEY_VERIFICATION_APP &&
            crypto->getPendingPublicKey(p->to, destKey)) {
            haveDestKey = true;
        }
        // We may want to retool things so we can send a PKC packet when the client specifies a key and nodenum, even if the node
        // is not in the local nodedb
        // First, only PKC encrypt packets we are originating
        if (isFromUs(p) &&
#if ARCH_PORTDUINO
            // Sim radio via the cli flag skips PKC
            !portduino_config.force_simradio &&
#endif
            // Don't use PKC with Ham mode
            !owner.is_licensed &&
            // Don't use PKC on 'serial' or 'gpio' channels unless explicitly requested
            !(p->pki_encrypted != true && (strcasecmp(channels.getName(chIndex), Channels::serialChannel) == 0 ||
                                           strcasecmp(channels.getName(chIndex), Channels::gpioChannel) == 0)) &&
            // Check for valid keys and single node destination
            config.security.private_key.size == 32 && !isBroadcast(p->to) &&
            // Some portnums either make no sense to send with PKC
            p->decoded.portnum != meshtastic_PortNum_TRACEROUTE_APP && p->decoded.portnum != meshtastic_PortNum_NODEINFO_APP &&
            p->decoded.portnum != meshtastic_PortNum_ROUTING_APP && p->decoded.portnum != meshtastic_PortNum_POSITION_APP &&
            // We allow Key Verification messages to be sent without a known destination key, since the point of those messages is
            // to exchange keys. The first exchange (no usable key yet) falls through to channel encryption; the follow-on packet
            // uses the pending key resolved into haveDestKey/destKey above.
            // Though possible the first packet each direction should go non-pkc
            // to handle the case where the remote node has our key, but we don't have theirs.
            !(p->decoded.portnum == meshtastic_PortNum_KEY_VERIFICATION_APP && !haveDestKey)) {
            LOG_DEBUG("Use PKI!");
            if (numbytes + MESHTASTIC_HEADER_LENGTH + MESHTASTIC_PKC_OVERHEAD > MAX_LORA_PAYLOAD_LEN)
                return meshtastic_Routing_Error_TOO_LARGE;
            // Check for a usable public key for the destination (NodeDB or a pending key-verification key)
            if (!haveDestKey) {
                LOG_WARN("Unknown public key for destination node 0x%08x (portnum %d), refusing to send legacy DM", p->to,
                         p->decoded.portnum);
                return meshtastic_Routing_Error_PKI_SEND_FAIL_PUBLIC_KEY;
            }
            if (p->pki_encrypted && !memfll(p->public_key.bytes, 0, 32) && memcmp(p->public_key.bytes, destKey.bytes, 32) != 0) {
                LOG_WARN("Client public key differs from requested: 0x%02x, stored key begins 0x%02x", *p->public_key.bytes,
                         *destKey.bytes);
                return meshtastic_Routing_Error_PKI_FAILED;
            }
            crypto->encryptCurve25519(p->to, getFrom(p), destKey, p->id, numbytes, bytes, p->encrypted.bytes);
            numbytes += MESHTASTIC_PKC_OVERHEAD;
            p->channel = 0;
            p->pki_encrypted = true;
        } else {
            if (p->pki_encrypted == true) {
                // Client specifically requested PKI encryption
                return meshtastic_Routing_Error_PKI_FAILED;
            }
            hash = channels.setActiveByIndex(chIndex);

            // Now that we are encrypting the packet channel should be the hash (no longer the index)
            p->channel = hash;
            if (hash < 0) {
                // No suitable channel could be found for
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
            // No suitable channel could be found for
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

    // Store a copy of the encrypted packet for MQTT.
    // Local, not a class member: handleReceived re-enters itself when a module
    // reply broadcast goes through MeshService::sendToMesh -> Router::sendLocal,
    // and a member would be silently overwritten without release on the inner
    // call. Each invocation now owns its own copy (issue #9632, #10101, #8729).
    DEBUG_HEAP_BEFORE;
    meshtastic_MeshPacket *p_encrypted = packetPool.allocCopy(*p);
    DEBUG_HEAP_AFTER("Router::handleReceived", p_encrypted);

    // Consume the decoded/authenticated handoff after preserving the exact encrypted packet and
    // before mutating any packet fields that participate in the exact cache match.
    if (src == RX_SRC_RADIO)
        applyRoutingAuthCache(p);

    // Also, we should set the time from the ISR and it should have msec level resolution.
    // Keep the decoded working packet and encrypted MQTT copy on the same local arrival timestamp.
    const uint32_t rxTime = getValidTime(RTCQualityFromNet);
    p->rx_time = rxTime;
    if (p_encrypted)
        p_encrypted->rx_time = rxTime;

    // Take those raw bytes and convert them back into a well structured protobuf we can understand
    auto decodedState = perhapsDecode(p);
    if (decodedState == DecodeState::DECODE_FATAL || decodedState == DecodeState::DECODE_POLICY_REJECT ||
        decodedState == DecodeState::DECODE_FAILURE) {
        // Fatal decoding error, we can't do anything with this packet
        LOG_WARN(decodedState == DecodeState::DECODE_POLICY_REJECT
                     ? "Packet rejected by signature policy"
                     : (decodedState == DecodeState::DECODE_FATAL ? "Fatal decode error, dropping packet"
                                                                  : "Decryptable packet failed decoding, dropping packet"));
        // A policy rejection is attacker-controlled input and must not cancel a valid pending
        // transmission with the same (from, id). Preserve the pre-existing fatal-decode behavior.
        if (decodedState == DecodeState::DECODE_FATAL)
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

#if MESHTASTIC_PREHOP_DROP
        // Pre-hop firmware drop, post-decode half: the bitfield that proves the origin populated hop_start is
        // encrypted under the channel key, so it can only be evaluated now that the packet is decoded. A packet
        // whose hop_start is still missing/unknown comes from pre-hop firmware - keep it out of module
        // processing, admin handling, phone delivery, MQTT and rebroadcast. Local-origin packets are exempt.
        if (!isFromUs(p) && classifyHopStart(*p) != HopStartStatus::VALID) {
            logHopStartDrop(*p, "post-decode pre-hop drop");
            cancelSending(p->from, p->id);
            skipHandle = true;
        }
#endif

        // Neighbor info module is disabled, ignore expensive neighbor info packets
        if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            p->decoded.portnum == meshtastic_PortNum_NEIGHBORINFO_APP &&
            (!moduleConfig.has_neighbor_info || !moduleConfig.neighbor_info.enabled)) {
            LOG_DEBUG("Neighbor info module is disabled, ignore neighbor packet");
            cancelSending(p->from, p->id);
            skipHandle = true;
        }

#if !MESHTASTIC_EXCLUDE_BEACON
        // Beacon listening is disabled: drop beacon packets so they are neither surfaced to the
        // phone nor handled on-device (same pattern as the disabled neighbor-info case above).
        if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            p->decoded.portnum == meshtastic_PortNum_MESH_BEACON_APP &&
            (!moduleConfig.has_mesh_beacon ||
             !(moduleConfig.mesh_beacon.flags & meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LISTEN_ENABLED))) {
            LOG_DEBUG("Beacon listening is disabled, ignore beacon packet");
            cancelSending(p->from, p->id);
            skipHandle = true;
        }
#endif

        bool shouldIgnoreNonstandardPorts =
            config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY;
#if USERPREFS_EVENT_MODE
        shouldIgnoreNonstandardPorts = true;
#endif
        if (shouldIgnoreNonstandardPorts && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            !IS_ONE_OF(p->decoded.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP,
                       meshtastic_PortNum_POSITION_APP, meshtastic_PortNum_NODEINFO_APP, meshtastic_PortNum_ROUTING_APP,
                       meshtastic_PortNum_TELEMETRY_APP, meshtastic_PortNum_ADMIN_APP, meshtastic_PortNum_ALERT_APP,
                       meshtastic_PortNum_KEY_VERIFICATION_APP, meshtastic_PortNum_WAYPOINT_APP,
                       meshtastic_PortNum_STORE_FORWARD_APP, meshtastic_PortNum_TRACEROUTE_APP,
                       meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP)) {
            LOG_DEBUG("Ignore packet on non-standard portnum for CORE_PORTNUMS_ONLY");
            cancelSending(p->from, p->id);
            skipHandle = true;
        }
    } else {
        printPacket("packet decoding failed or skipped (no PSK?)", p);
    }

    // call modules here
    // If this could be a spoofed packet, don't let the modules see it.
    if (!skipHandle) {
        MeshModule::callModules(*p, src);

#if !MESHTASTIC_EXCLUDE_MQTT
        if (p_encrypted == nullptr) {
            LOG_WARN("p_encrypted is null, skipping MQTT publish");
        } else {
            // Mark as pki_encrypted if it is not yet decoded and MQTT encryption is also enabled, hash matches and it's a DM not
            // to us (because we would be able to decrypt it)
            if (decodedState == DecodeState::DECODE_OPAQUE && moduleConfig.mqtt.encryption_enabled && p->channel == 0x00 &&
                !isBroadcast(p->to) && !isToUs(p))
                p_encrypted->pki_encrypted = true;
            // After potentially altering it, publish received message to MQTT if we're not the original transmitter of the packet
            if ((decodedState == DecodeState::DECODE_SUCCESS || p_encrypted->pki_encrypted) && moduleConfig.mqtt.enabled &&
                !isFromUs(p) && mqtt) {
                if (decodedState == DecodeState::DECODE_SUCCESS && p->decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP &&
                    moduleConfig.mqtt.encryption_enabled) {
                    // For TRACEROUTE_APP packets release the original encrypted packet and encrypt a new from the changed packet
                    // Only release the original after successful allocation to avoid losing an incomplete but valid packet
                    auto *p_encrypted_new = packetPool.allocCopy(*p);
                    if (p_encrypted_new) {
                        auto encodeResult = perhapsEncode(p_encrypted_new);
                        if (encodeResult != meshtastic_Routing_Error_NONE) {
                            // Encryption failed, release the new packet and fall back to sending the original encrypted packet to
                            // MQTT
                            LOG_WARN("Encryption of new TR packet failed, sending original TR to MQTT");
                            packetPool.release(p_encrypted_new);
                            p_encrypted_new = nullptr;
                        } else {
                            // Successfully re-encrypted, release the original encrypted packet and use the new one for MQTT
                            packetPool.release(p_encrypted);
                            p_encrypted = p_encrypted_new;
                        }
                    } else {
                        // Allocation failed, log a warning and fall back to sending the original encrypted packet to MQTT
                        LOG_WARN("Failed to allocate new encrypted packet for TR, sending original TR to MQTT");
                    }
                }
                mqtt->onSend(*p_encrypted, *p, p->channel);
            }
        }
#endif
    }

    packetPool.release(p_encrypted); // Release the encrypted packet (release() handles nullptr)
}

void Router::perhapsHandleReceived(meshtastic_MeshPacket *p)
{
#if ARCH_PORTDUINO
    // Even ignored packets get logged in the trace
    if (portduino_config.traceFilename != "" || portduino_config.logoutputlevel == level_trace) {
        p->rx_time = getValidTime(RTCQualityFromNet); // store the arrival timestamp for the phone
        LOG_TRACE("%s", MeshPacketSerializer::JsonSerializeEncrypted(p).c_str());
    }
#endif
    // assert(radioConfig.has_preferences);
    if (is_in_repeated(config.lora.ignore_incoming, p->from)) {
        clearRoutingAuthCache();
        LOG_DEBUG("Ignore msg, 0x%08x is in our ignore list", p->from);
        packetPool.release(p);
        return;
    }

    meshtastic_NodeInfoLite const *node = nodeDB->getMeshNode(p->from);
    if (nodeInfoLiteIsIgnored(node)) {
        clearRoutingAuthCache();
        LOG_DEBUG("Ignore msg, 0x%08x is ignored", p->from);
        packetPool.release(p);
        return;
    }

    if (p->from == NODENUM_BROADCAST) {
        clearRoutingAuthCache();
        LOG_DEBUG("Ignore msg from broadcast address");
        packetPool.release(p);
        return;
    }

    if (config.lora.ignore_mqtt && p->via_mqtt) {
        clearRoutingAuthCache();
        LOG_DEBUG("Msg came in via MQTT from 0x%08x", p->from);
        packetPool.release(p);
        return;
    }

    if (shouldDropPacketForPreHop(*p)) {
        clearRoutingAuthCache();
        logHopStartDrop(*p, "pre-hop drop");
        packetPool.release(p);
        return;
    }

    // Decrypt and authenticate before Reliable/Flooding/NextHop filters can update retry
    // timers, packet history, implicit ACK state, cancellation, or relay queues. A packet for
    // an unknown channel passes as opaque traffic and retains the existing relay behavior.
    const auto authVerdict = passesRoutingAuthGate(p);
    if (authVerdict == RoutingAuthVerdict::REJECT) {
        packetPool.release(p);
        return;
    }
    if (authVerdict == RoutingAuthVerdict::OPAQUE_RELAY_ONLY) {
        relayOpaquePacket(p);
        packetPool.release(p);
        return;
    }

    if (shouldFilterReceived(p)) {
        clearRoutingAuthCache();
        LOG_DEBUG("Incoming msg was filtered from 0x%08x", p->from);
        packetPool.release(p);
        return;
    }

    // Note: we avoid calling shouldFilterReceived if we are supposed to ignore certain nodes - because some overrides might
    // cache/learn of the existence of nodes (i.e. FloodRouter) that they should not
    handleReceived(p);
    packetPool.release(p);
}
