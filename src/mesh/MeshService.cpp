#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#include "../concurrency/Periodic.h"
#include "BluetoothCommon.h" // needed for updateBatteryLevel, FIXME, eventually when we pull mesh out into a lib we shouldn't be whacking bluetooth from here
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "Power.h"
#include "PowerFSM.h"
#include "TypeConversions.h"
#include "gps/RTC.h"
#include "graphics/draw/MessageRenderer.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/AdminModule.h"
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#include "modules/RoutingModule.h"
#include <assert.h>
#include <string>

#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

/*
receivedPacketQueue - this is a queue of messages we've received from the mesh, which we are keeping to deliver to the phone.
It is implemented with a FreeRTos queue (wrapped with a little RTQueue class) of pointers to MeshPacket protobufs (which were
alloced with new). After a packet ptr is removed from the queue and processed it should be deleted.  (eventually we should move
sent packets into a 'sentToPhone' queue of packets we can delete just as soon as we are sure the phone has acked those packets -
when the phone writes to FromNum)

mesh - an instance of Mesh class.  Which manages the interface to the mesh radio library, reception of packets from other nodes,
arbitrating to select a node number and keeping the current nodedb.

*/

/* Broadcast when a newly powered mesh node wants to find a node num it can use

The algorithm is as follows:
* when a node starts up, it broadcasts their user and the normal flow is for all other nodes to reply with their User as well (so
the new node can build its node db)
*/

MeshService *service;

#define MAX_MQTT_PROXY_MESSAGES 16
static MemoryPool<meshtastic_MqttClientProxyMessage, MAX_MQTT_PROXY_MESSAGES> staticMqttClientProxyMessagePool;

#define MAX_QUEUE_STATUS 4
static MemoryPool<meshtastic_QueueStatus, MAX_QUEUE_STATUS> staticQueueStatusPool;

#define MAX_CLIENT_NOTIFICATIONS 4
static MemoryPool<meshtastic_ClientNotification, MAX_CLIENT_NOTIFICATIONS> staticClientNotificationPool;

Allocator<meshtastic_MqttClientProxyMessage> &mqttClientProxyMessagePool = staticMqttClientProxyMessagePool;

Allocator<meshtastic_ClientNotification> &clientNotificationPool = staticClientNotificationPool;

Allocator<meshtastic_QueueStatus> &queueStatusPool = staticQueueStatusPool;

#include "PositionPrecision.h"
#include "Router.h"

MeshService::MeshService()
#ifdef ARCH_PORTDUINO
    : toPhoneQueue(MAX_RX_TOPHONE), toPhoneQueueStatusQueue(MAX_RX_QUEUESTATUS_TOPHONE),
      toPhoneMqttProxyQueue(MAX_RX_MQTTPROXY_TOPHONE), toPhoneClientNotificationQueue(MAX_RX_NOTIFICATION_TOPHONE)
#endif
{
    lastQueueStatus = {0, 0, 16, 0};
}

void MeshService::init()
{
#if HAS_GPS
    if (gps)
        gpsObserver.observe(&gps->newStatus);
#endif
}

int MeshService::handleFromRadio(const meshtastic_MeshPacket *mp)
{
    powerFSM.trigger(EVENT_PACKET_FOR_PHONE); // Possibly keep the node from sleeping

    nodeDB->updateFrom(*mp); // update our DB state based off sniffing every RX packet from the radio
    bool isPreferredRebroadcaster =
        IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER, meshtastic_Config_DeviceConfig_Role_ROUTER_LATE,
                  meshtastic_Config_DeviceConfig_Role_CLIENT_BASE);
    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP && mp->decoded.request_id > 0) {
        LOG_DEBUG("Received telemetry response. Skip sending our NodeInfo");
        //  ignore our request for its NodeInfo
    } else if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
               !nodeInfoLiteHasUser(nodeDB->getMeshNode(mp->from)) && nodeInfoModule && !isPreferredRebroadcaster &&
               !nodeDB->isFull()) {
        if (airTime->isTxAllowedChannelUtil(true)) {
            const int8_t hopsUsed = getHopsAway(*mp, config.lora.hop_limit);
            if (hopsUsed > (int32_t)(config.lora.hop_limit + 2)) {
                LOG_DEBUG("Skip send NodeInfo: %d hops away is too far away", hopsUsed);
            } else {
                LOG_INFO("Heard new node on ch. %d, send NodeInfo and ask for response", mp->channel);
                nodeInfoModule->sendOurNodeInfo(mp->from, true, mp->channel);
            }
        } else {
            LOG_DEBUG("Skip sending NodeInfo > 25%% ch. util");
        }
    }

    printPacket("Forwarding to phone", mp);
    if (auto *toPhone = packetPool.allocCopy(*mp))
        sendToPhone(toPhone);

    return 0;
}

/// Do idle processing (mostly processing messages which have been queued from the radio)
void MeshService::loop()
{
    if (lastQueueStatus.free == 0) { // check if there is now free space in TX queue
        meshtastic_QueueStatus qs = router->getQueueStatus();
        if (qs.free != lastQueueStatus.free)
            (void)sendQueueStatusToPhone(qs, 0, 0);
    }
    if (oldFromNum != fromNum) { // We don't want to generate extra notifies for multiple new packets
        int result = fromNumChanged.notifyObservers(fromNum);
        if (result == 0) // If any observer returns non-zero, we will try again
            oldFromNum = fromNum;
    }
}

/// The radioConfig object just changed, call this to force the hw to change to the new settings
void MeshService::reloadConfig(int saveWhat)
{
    // Only LoRa config and channels (freq/PSK/slot) affect the radio. Saves that only touch
    // module config, device state, or the node database (e.g. favoriting a node) have no reason
    // to re-init the LoRa chip - skip it there to avoid an unnecessary and risky SPI reconfigure.
    if (saveWhat & (SEGMENT_CONFIG | SEGMENT_CHANNELS)) {
        // If we can successfully set this radio to these settings, save them to disk

        // This will also update the region as needed
        nodeDB->resetRadioConfig(); // Don't let the phone send us fatally bad settings

        configChanged.notifyObservers(NULL); // This will cause radio hardware to change freqs etc
    }
    nodeDB->saveToDisk(saveWhat);
}

/// The owner User record just got updated, update our node DB and broadcast the info into the mesh
void MeshService::reloadOwner(bool shouldSave)
{
    // LOG_DEBUG("reloadOwner()");
    // update our local data directly
    nodeDB->updateUser(nodeDB->getNodeNum(), owner);
    assert(nodeInfoModule);
    // update everyone else and save to disk
    if (nodeInfoModule && shouldSave) {
        nodeInfoModule->sendOurNodeInfo();
    }
}

// search the queue for a request id and return the matching nodenum
NodeNum MeshService::getNodenumFromRequestId(uint32_t request_id)
{
    NodeNum nodenum = 0;
    for (int i = 0; i < toPhoneQueue.numUsed(); i++) {
        meshtastic_MeshPacket *p = toPhoneQueue.dequeuePtr(0);
        if (p->id == request_id) {
            nodenum = p->to;
            // make sure to continue this to make one full loop
        }
        // put it right back on the queue
        toPhoneQueue.enqueue(p, 0);
    }
    return nodenum;
}

#if MESHTASTIC_ENABLE_FRAME_INJECTION
// Deliver a client-supplied frame into the receive pipeline as if it arrived off the LoRa chip. Mirrors
// the portduino SimRadio SIMULATOR_APP unwrap so the same host wire format works on real hardware: the
// frame rides inside a Compressed envelope wrapped in a MeshPacket that carries from/to/id/channel.
//   Compressed.portnum == UNKNOWN_APP -> Compressed.data is verbatim ciphertext, decrypted as if off-air
//   otherwise                         -> Compressed.data is the plaintext payload for Compressed.portnum
void MeshService::injectAsReceived(meshtastic_MeshPacket &p)
{
    meshtastic_Compressed scratch;
    if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_Compressed_msg, &scratch)) {
            if (scratch.portnum == meshtastic_PortNum_UNKNOWN_APP) {
                p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
                memcpy(p.encrypted.bytes, scratch.data.bytes, scratch.data.size);
                p.encrypted.size = scratch.data.size;
            } else {
                memcpy(&p.decoded.payload, &scratch.data, sizeof(scratch.data));
                p.decoded.portnum = scratch.portnum;
            }
        } else {
            LOG_ERROR("inject: could not decode Compressed envelope, dropping");
            return;
        }
    }
    // The real RX path (RadioLibInterface::handleReceiveInterrupt) drops sender==0; mirror it so injection
    // behaves identically to an over-the-air frame.
    if (p.from == 0) {
        LOG_WARN("inject: dropping frame with from==0 (matches real LoRa RX)");
        return;
    }
    meshtastic_MeshPacket *mp = packetPool.allocCopy(p);
    if (!mp)
        return;
    if (mp->rx_snr == 0) // plausible synthetic link metadata unless the caller set it
        mp->rx_snr = 8;
    if (mp->rx_rssi == 0)
        mp->rx_rssi = -40;
    mp->rx_time = getValidTime(RTCQualityFromNet);
    LOG_INFO("inject: RX from=0x%08x to=0x%08x id=0x%08x ch=%d %s", mp->from, mp->to, mp->id, mp->channel,
             mp->which_payload_variant == meshtastic_MeshPacket_encrypted_tag ? "encrypted" : "decoded");
    router->enqueueReceivedMessage(mp);
}
#endif

/**
 *  Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
 * Called by PhoneAPI.handleToRadio.  Note: p is a scratch buffer, this function is allowed to write to it but it can not keep a
 * reference
 */
void MeshService::handleToRadio(meshtastic_MeshPacket &p)
{
#if defined(ARCH_PORTDUINO)
    if (SimRadio::instance && p.decoded.portnum == meshtastic_PortNum_SIMULATOR_APP) {
        // Simulates device received a packet via the LoRa chip
        SimRadio::instance->unpackAndReceive(p);
        return;
    }
#endif
#if MESHTASTIC_ENABLE_FRAME_INJECTION
    // Real-hardware analog of the SimRadio path above: deliver a client-supplied frame into the RX
    // pipeline exactly as if it had arrived off the LoRa chip. Reached before the p.from=0 line below,
    // so an injected sender is preserved. Build-flag gated (off by default) - it lets anything with a
    // wired connection forge over-the-air traffic, so it must never ship enabled.
    if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag && p.decoded.portnum == meshtastic_PortNum_SIMULATOR_APP) {
        injectAsReceived(p);
        return;
    }
#endif
    p.from = 0;                          // We don't let clients assign nodenums to their sent messages
    p.next_hop = NO_NEXT_HOP_PREFERENCE; // We don't let clients assign next_hop to their sent messages
    p.relay_node = NO_RELAY_NODE;        // We don't let clients assign relay_node to their sent messages

    if (p.id == 0)
        p.id = generatePacketId(); // If the phone didn't supply one, then pick one

    p.rx_time = getValidTime(RTCQualityFromNet); // Record the time the packet arrived from the phone

    IF_SCREEN(if (p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && p.decoded.payload.size > 0 &&
                  p.to != NODENUM_BROADCAST && p.to != 0) // DM only
              {
                  perhapsDecode(&p);
                  if (const StoredMessage *sm = messageStore.tryAddFromPacket(p))
                      graphics::MessageRenderer::handleNewMessage(nullptr, *sm, p); // notify UI
              })
#if !MESHTASTIC_EXCLUDE_ADMIN
    // Note admin requests on their way out: AdminModule only accepts a response from a remote we
    // actually asked. Runs before encryption, while the payload is still readable.
    if (adminModule && p.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        p.decoded.portnum == meshtastic_PortNum_ADMIN_APP)
        adminModule->noteOutgoingAdminRequest(p);
#endif

    // Send the packet into the mesh
    DEBUG_HEAP_BEFORE;
    auto a = packetPool.allocCopy(p);
    DEBUG_HEAP_AFTER("MeshService::handleToRadio", a);
    if (a)
        sendToMesh(a, RX_SRC_USER);

    bool loopback = false; // if true send any packet the phone sends back itself (for testing)
    if (loopback) {
        // no need to copy anymore because handle from radio assumes it should _not_ delete
        // packetPool.allocCopy(r.variant.packet);
        handleFromRadio(&p);
        // handleFromRadio will tell the phone a new packet arrived
    }
}

/** Attempt to cancel a previously sent packet from this _local_ node.  Returns true if a packet was found we could cancel */
bool MeshService::cancelSending(PacketId id)
{
    return router->cancelSending(nodeDB->getNodeNum(), id);
}

ErrorCode MeshService::sendQueueStatusToPhone(const meshtastic_QueueStatus &qs, ErrorCode res, uint32_t mesh_packet_id)
{
    meshtastic_QueueStatus *copied = queueStatusPool.allocCopy(qs);
    if (!copied)
        return ERRNO_UNKNOWN;

    copied->res = res;
    copied->mesh_packet_id = mesh_packet_id;

    if (toPhoneQueueStatusQueue.numFree() == 0) {
        LOG_INFO("tophone queue status queue is full, discard oldest");
        meshtastic_QueueStatus *d = toPhoneQueueStatusQueue.dequeuePtr(0);
        if (d)
            releaseQueueStatusToPool(d);
    }

    lastQueueStatus = *copied;

    res = toPhoneQueueStatusQueue.enqueue(copied, 0);
    fromNum++;

    return res ? ERRNO_OK : ERRNO_UNKNOWN;
}

void MeshService::sendToMesh(meshtastic_MeshPacket *p, RxSource src, bool ccToPhone)
{
    uint32_t mesh_packet_id = p->id;
    nodeDB->updateFrom(*p); // update our local DB for this packet (because phone might have sent position packets etc...)

    // callModules' loopback gate keeps RX_SRC_LOCAL packets from RoutingModule, the only module
    // that forwards to the phone, so deliver our own reply's copy here or the client never sees it.
    const bool localDelivery = isToUs(p);
    if (src == RX_SRC_LOCAL && localDelivery)
        ccToPhone = true;

    // Note: We might return !OK if our fifo was full, at that point the only option we have is to drop it
    ErrorCode res = router->sendLocal(p, src);

    /* NOTE(pboldin): Prepare and send QueueStatus message to the phone as a
     * high-priority message. */
    meshtastic_QueueStatus qs = router->getQueueStatus();
    // SHOULD_RELEASE means "caller frees", not a send failure, so don't report it as one.
    ErrorCode r = sendQueueStatusToPhone(qs, (res == ERRNO_SHOULD_RELEASE && localDelivery) ? ERRNO_OK : res, mesh_packet_id);
    if (r != ERRNO_OK) {
        LOG_DEBUG("Can't send status to phone");
    }

    if ((res == ERRNO_OK || res == ERRNO_SHOULD_RELEASE) && ccToPhone) { // Check if p is not released in case it couldn't be sent
        DEBUG_HEAP_BEFORE;
        auto a = packetPool.allocCopy(*p);
        DEBUG_HEAP_AFTER("MeshService::sendToMesh", a);

        if (a)
            sendToPhone(a);
    }

    // Router may ask us to release the packet if it wasn't sent
    if (res == ERRNO_SHOULD_RELEASE) {
        releaseToPool(p);
    }
}

bool MeshService::trySendPosition(NodeNum dest, bool wantReplies)
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());

    assert(node);

    if (nodeDB->hasValidPosition(node)) {
#if HAS_GPS && !MESHTASTIC_EXCLUDE_GPS
        if (positionModule) {
            if (!config.position.fixed_position && !nodeDB->hasLocalPositionSinceBoot()) {
                LOG_DEBUG("Skip position ping; no fresh position since boot");
                return false;
            }
            // Prefer the node's current channel, but fall back to the first channel with
            // position enabled (matching PositionModule::sendOurPosition() behavior).
            uint8_t sendChan = node->channel;
            if (getPositionPrecisionForChannel(sendChan) == 0) {
                bool found = false;
                for (uint8_t ch = 0; ch < 8; ++ch) {
                    if (getPositionPrecisionForChannel(ch) != 0) {
                        sendChan = ch;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // No channel with position enabled: fall back to sending nodeinfo, as before.
                    if (nodeInfoModule) {
                        LOG_INFO(
                            "No channel with position enabled; sending nodeinfo instead to 0x%08x, wantReplies=%d, channel=%d",
                            dest, wantReplies, node->channel);
                        nodeInfoModule->sendOurNodeInfo(dest, wantReplies, node->channel);
                    }
                    return false;
                }
            }
            LOG_INFO("Send position ping to 0x%08x, wantReplies=%d, channel=%d", dest, wantReplies, sendChan);
            positionModule->sendOurPosition(dest, wantReplies, sendChan);
            return true;
        }
    } else {
#endif
        if (nodeInfoModule) {
            LOG_INFO("Send nodeinfo ping to 0x%08x, wantReplies=%d, channel=%d", dest, wantReplies, node->channel);
            nodeInfoModule->sendOurNodeInfo(dest, wantReplies, node->channel);
        }
    }
    return false;
}

// Re-decode nested string-bearing payloads before local phone delivery so PB_VALIDATE_UTF8 rejects
// malformed NodeInfo/Waypoint data a strict phone decoder could crash on. Mesh relay is unaffected.
bool MeshService::phonePayloadIsDecodable(const meshtastic_Data &d)
{
    // User/Waypoint are all-static nanopb messages (no PB_ENABLE_MALLOC/callback fields), so the
    // decoded scratch owns no heap and needs no pb_release.
    switch (d.portnum) {
    case meshtastic_PortNum_NODEINFO_APP: {
        meshtastic_User u = meshtastic_User_init_zero;
        return pb_decode_from_bytes(d.payload.bytes, d.payload.size, &meshtastic_User_msg, &u);
    }
    case meshtastic_PortNum_WAYPOINT_APP: {
        meshtastic_Waypoint w = meshtastic_Waypoint_init_zero;
        return pb_decode_from_bytes(d.payload.bytes, d.payload.size, &meshtastic_Waypoint_msg, &w);
    }
    default:
        return true;
    }
}

void MeshService::sendToPhone(meshtastic_MeshPacket *p)
{
    perhapsDecode(p);

    // Withhold decoded nested payloads a strict phone decoder would reject; still-encrypted packets
    // pass through (the phone may hold the key).
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag && !phonePayloadIsDecodable(p->decoded)) {
        LOG_WARN("Dropping undecodable portnum=%d payload from phone delivery (from=0x%08x)", p->decoded.portnum, p->from);
        releaseToPool(p);
        fromNum++; // notify observers so the phone can resync
        return;
    }

#ifdef ARCH_ESP32
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
    if (moduleConfig.store_forward.enabled && storeForwardModule->isServer() &&
        p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        releaseToPool(p); // Copy is already stored in StoreForward history
        fromNum++;        // Notify observers for packet from radio
        return;
    }
#endif
#endif

    if (toPhoneQueue.numFree() == 0) {
        if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
            p->decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP) {
            LOG_WARN("ToPhone queue is full, discard oldest");
            meshtastic_MeshPacket *d = toPhoneQueue.dequeuePtr(0);
            if (d)
                releaseToPool(d);
        } else {
            LOG_WARN("ToPhone queue is full, drop packet");
            releaseToPool(p);
            fromNum++; // Make sure to notify observers in case they are reconnected so they can get the packets
            return;
        }
    }

    if (toPhoneQueue.enqueue(p, 0) == false) {
        LOG_CRIT("Failed to queue a packet into toPhoneQueue!");
        releaseToPool(p);
        fromNum++; // notify observers so phone can resync
        return;
    }
    fromNum++;
}

void MeshService::sendMqttMessageToClientProxy(meshtastic_MqttClientProxyMessage *m)
{
    LOG_DEBUG("Send mqtt message on topic '%s' to client for proxy", m->topic);
    if (toPhoneMqttProxyQueue.numFree() == 0) {
        LOG_WARN("MqttClientProxyMessagePool queue is full, discard oldest");
        meshtastic_MqttClientProxyMessage *d = toPhoneMqttProxyQueue.dequeuePtr(0);
        if (d)
            releaseMqttClientProxyMessageToPool(d);
    }

    if (toPhoneMqttProxyQueue.enqueue(m, 0) == false) {
        LOG_CRIT("Failed to queue a packet into toPhoneMqttProxyQueue!");
        releaseMqttClientProxyMessageToPool(m);
        return;
    }
    fromNum++;
}

void MeshService::sendRoutingErrorResponse(meshtastic_Routing_Error error, const meshtastic_MeshPacket *mp)
{
    if (!mp) {
        LOG_WARN("Cannot send routing error response: null packet");
        return;
    }

    // Use the routing module to send the error response
    if (routingModule) {
        routingModule->sendAckNak(error, mp->from, mp->id, mp->channel);
    } else {
        LOG_ERROR("Cannot send routing error response: no routing module");
    }
}

void MeshService::sendClientNotification(meshtastic_ClientNotification *n)
{
    LOG_DEBUG("Send client notification to phone");
    if (toPhoneClientNotificationQueue.numFree() == 0) {
        LOG_WARN("ClientNotification queue is full, discard oldest");
        meshtastic_ClientNotification *d = toPhoneClientNotificationQueue.dequeuePtr(0);
        if (d)
            releaseClientNotificationToPool(d);
    }

    if (toPhoneClientNotificationQueue.enqueue(n, 0) == false) {
        LOG_CRIT("Failed to queue a notification into toPhoneClientNotificationQueue!");
        releaseClientNotificationToPool(n);
        return;
    }
    fromNum++;
}

meshtastic_NodeInfoLite *MeshService::refreshLocalMeshNode()
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    assert(node);

    // Update our local node info with our time (even if we don't decide to update anyone else)
    node->last_heard =
        getValidTime(RTCQualityFromNet); // This nodedb timestamp might be stale, so update it if our clock is kinda valid

#if !MESHTASTIC_EXCLUDE_POSITIONDB
    // Make sure our own NodeNum has a slot in the position map so subsequent
    // updates (and the bundled NodeInfo emission to the phone) have somewhere
    // to read from. Insert a default-zero entry on first call.
    nodeDB->touchNodePositionTime(node->num, getValidTime(RTCQualityFromNet));
#endif

    if (powerStatus->getHasBattery() == 1) {
        updateBatteryLevel(powerStatus->getBatteryChargePercent());
    }

    return node;
}

#if HAS_GPS
int MeshService::onGPSChanged(const meshtastic::GPSStatus *newStatus)
{
    // Update our local node info with our position (even if we don't decide to update anyone else)
    const meshtastic_NodeInfoLite *node = refreshLocalMeshNode();
    meshtastic_Position pos = meshtastic_Position_init_default;

    if (newStatus->getHasLock()) {
        // load data from GPS object, will add timestamp + battery further down
        pos = gps->p;
    } else {
        // The GPS has lost lock
#ifdef GPS_DEBUG
        LOG_DEBUG("onGPSchanged() - lost validLocation");
#endif
    }
    // Used fixed position if configured regardless of GPS lock
    if (config.position.fixed_position) {
        LOG_WARN("Use fixed position");
        meshtastic_PositionLite fixedSlot;
        if (nodeDB->copyNodePosition(node->num, fixedSlot))
            pos = TypeConversions::ConvertToPosition(fixedSlot);
    }

    // Add a fresh timestamp
    pos.time = getValidTime(RTCQualityFromNet);

    // In debug logs, identify position by @timestamp:stage (stage 4 = nodeDB)
    LOG_DEBUG("onGPSChanged() pos@%x time=%u lat=%d lon=%d alt=%d", pos.timestamp, pos.time, pos.latitude_i, pos.longitude_i,
              pos.altitude);

    // Update our current position in the local DB
    nodeDB->updatePosition(nodeDB->getNodeNum(), pos, RX_SRC_LOCAL);

    return 0;
}
#endif
bool MeshService::isToPhoneQueueEmpty()
{
    return toPhoneQueue.isEmpty();
}

uint32_t MeshService::GetTimeSinceMeshPacket(const meshtastic_MeshPacket *mp)
{
    uint32_t now = getTime();

    uint32_t last_seen = mp->rx_time;
    int delta = (int)(now - last_seen);
    if (delta < 0) // our clock must be slightly off still - not set from GPS yet
        delta = 0;

    return delta;
}
