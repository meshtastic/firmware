#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#include "../concurrency/Periodic.h"
#include "BluetoothCommon.h" // needed for updateBatteryLevel, FIXME, eventually when we pull mesh out into a lib we shouldn't be whacking bluetooth from here
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "TypeConversions.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#include "modules/RoutingModule.h"
#include "power.h"
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

static MemoryDynamic<meshtastic_MqttClientProxyMessage> staticMqttClientProxyMessagePool;

static MemoryDynamic<meshtastic_QueueStatus> staticQueueStatusPool;

static MemoryDynamic<meshtastic_ClientNotification> staticClientNotificationPool;

Allocator<meshtastic_MqttClientProxyMessage> &mqttClientProxyMessagePool = staticMqttClientProxyMessagePool;

Allocator<meshtastic_ClientNotification> &clientNotificationPool = staticClientNotificationPool;

Allocator<meshtastic_QueueStatus> &queueStatusPool = staticQueueStatusPool;

#include "Router.h"

MeshService::MeshService()
    : toPhoneQueue(MAX_RX_TOPHONE), toPhoneQueueStatusQueue(MAX_RX_TOPHONE), toPhoneMqttProxyQueue(MAX_RX_TOPHONE),
      toPhoneClientNotificationQueue(MAX_RX_TOPHONE / 2)
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
        IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER, meshtastic_Config_DeviceConfig_Role_REPEATER);
    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP && mp->decoded.request_id > 0) {
        LOG_DEBUG("Received telemetry response. Skip sending our NodeInfo"); //  because this potentially a Repeater which will
                                                                             //  ignore our request for its NodeInfo
    } else if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag && !nodeDB->getMeshNode(mp->from)->has_user &&
               nodeInfoModule && !isPreferredRebroadcaster && !nodeDB->isFull()) {
        if (airTime->isTxAllowedChannelUtil(true)) {
            // Hops used by the request. If somebody in between running modified firmware modified it, ignore it
            auto hopStart = mp->hop_start;
            auto hopLimit = mp->hop_limit;
            uint8_t hopsUsed = hopStart < hopLimit ? config.lora.hop_limit : hopStart - hopLimit;
            if (hopsUsed > config.lora.hop_limit + 2) {
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
    sendToPhone(packetPool.allocCopy(*mp));

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
    // If we can successfully set this radio to these settings, save them to disk

    // This will also update the region as needed
    nodeDB->resetRadioConfig(); // Don't let the phone send us fatally bad settings

    configChanged.notifyObservers(NULL); // This will cause radio hardware to change freqs etc
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
    p.from = 0;                          // We don't let clients assign nodenums to their sent messages
    p.next_hop = NO_NEXT_HOP_PREFERENCE; // We don't let clients assign next_hop to their sent messages
    p.relay_node = NO_RELAY_NODE;        // We don't let clients assign relay_node to their sent messages

    if (p.id == 0)
        p.id = generatePacketId(); // If the phone didn't supply one, then pick one

    p.rx_time = getValidTime(RTCQualityFromNet); // Record the time the packet arrived from the phone
                                                 // (so we update our nodedb for the local node)

    // Send the packet into the mesh

    sendToMesh(packetPool.allocCopy(p), RX_SRC_USER);

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

    // Note: We might return !OK if our fifo was full, at that point the only option we have is to drop it
    ErrorCode res = router->sendLocal(p, src);

    /* NOTE(pboldin): Prepare and send QueueStatus message to the phone as a
     * high-priority message. */
    meshtastic_QueueStatus qs = router->getQueueStatus();
    ErrorCode r = sendQueueStatusToPhone(qs, res, mesh_packet_id);
    if (r != ERRNO_OK) {
        LOG_DEBUG("Can't send status to phone");
    }

    if ((res == ERRNO_OK || res == ERRNO_SHOULD_RELEASE) && ccToPhone) { // Check if p is not released in case it couldn't be sent
        sendToPhone(packetPool.allocCopy(*p));
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
            LOG_INFO("Send position ping to 0x%x, wantReplies=%d, channel=%d", dest, wantReplies, node->channel);
            positionModule->sendOurPosition(dest, wantReplies, node->channel);
            return true;
        }
    } else {
#endif
        if (nodeInfoModule) {
            LOG_INFO("Send nodeinfo ping to 0x%x, wantReplies=%d, channel=%d", dest, wantReplies, node->channel);
            nodeInfoModule->sendOurNodeInfo(dest, wantReplies, node->channel);
        }
    }
    return false;
}

void MeshService::sendToPhone(meshtastic_MeshPacket *p)
{
    perhapsDecode(p);

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
        abort();
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
        abort();
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
        abort();
    }
    fromNum++;
}

meshtastic_NodeInfoLite *MeshService::refreshLocalMeshNode()
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    assert(node);

    // We might not have a position yet for our local node, in that case, at least try to send the time
    if (!node->has_position) {
        memset(&node->position, 0, sizeof(node->position));
        node->has_position = true;
    }

    meshtastic_PositionLite &position = node->position;

    // Update our local node info with our time (even if we don't decide to update anyone else)
    node->last_heard =
        getValidTime(RTCQualityFromNet); // This nodedb timestamp might be stale, so update it if our clock is kinda valid

    position.time = getValidTime(RTCQualityFromNet);

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
        pos = TypeConversions::ConvertToPosition(node->position);
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