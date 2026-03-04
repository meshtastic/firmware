#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#include "../concurrency/Periodic.h"
#include "BluetoothCommon.h" // needed for updateBatteryLevel, FIXME, eventually when we pull mesh out into a lib we shouldn't be whacking bluetooth from here
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "TypeConversions.h"
#include "concurrency/LockGuard.h"
#include "graphics/draw/MessageRenderer.h"
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

#define MAX_MQTT_PROXY_MESSAGES 16
static MemoryPool<meshtastic_MqttClientProxyMessage, MAX_MQTT_PROXY_MESSAGES> staticMqttClientProxyMessagePool;

#define MAX_QUEUE_STATUS 4
static MemoryPool<meshtastic_QueueStatus, MAX_QUEUE_STATUS> staticQueueStatusPool;

#define MAX_CLIENT_NOTIFICATIONS 4
static MemoryPool<meshtastic_ClientNotification, MAX_CLIENT_NOTIFICATIONS> staticClientNotificationPool;

Allocator<meshtastic_MqttClientProxyMessage> &mqttClientProxyMessagePool = staticMqttClientProxyMessagePool;

Allocator<meshtastic_ClientNotification> &clientNotificationPool = staticClientNotificationPool;

Allocator<meshtastic_QueueStatus> &queueStatusPool = staticQueueStatusPool;

#include "Router.h"

MeshService::MeshService()
{
    lastQueueStatus = {0, 0, 16, 0};
}

int MeshService::findClientSlotByPtrLocked(const PhoneAPI *client) const
{
    if (!client)
        return -1;

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        if (phoneClients[i].client == client) {
            return i;
        }
    }

    return -1;
}

int MeshService::findClientSlotByPtrLocked(const PhoneAPI *client)
{
    return static_cast<const MeshService *>(this)->findClientSlotByPtrLocked(client);
}

int MeshService::findFreeClientSlotLocked() const
{
    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        if (phoneClients[i].client == nullptr && !phoneClients[i].active) {
            return i;
        }
    }

    return -1;
}

void MeshService::releasePacketFanoutEntryLocked(PacketFanoutEntry *entry)
{
    if (!entry)
        return;

    if (entry->refcount > 0)
        entry->refcount--;

    if (entry->refcount == 0) {
        if (entry->payload)
            releaseToPool(entry->payload);

        entry->payload = nullptr;
        packetFanoutPool.release(entry);
    }
}

void MeshService::releaseQueueStatusFanoutEntryLocked(QueueStatusFanoutEntry *entry)
{
    if (!entry)
        return;

    if (entry->refcount > 0)
        entry->refcount--;

    if (entry->refcount == 0) {
        if (entry->payload)
            releaseQueueStatusToPool(entry->payload);

        entry->payload = nullptr;
        queueStatusFanoutPool.release(entry);
    }
}

void MeshService::releaseMqttProxyFanoutEntryLocked(MqttProxyFanoutEntry *entry)
{
    if (!entry)
        return;

    if (entry->refcount > 0)
        entry->refcount--;

    if (entry->refcount == 0) {
        if (entry->payload)
            releaseMqttClientProxyMessageToPool(entry->payload);

        entry->payload = nullptr;
        mqttProxyFanoutPool.release(entry);
    }
}

void MeshService::releaseClientNotificationFanoutEntryLocked(ClientNotificationFanoutEntry *entry)
{
    if (!entry)
        return;

    if (entry->refcount > 0)
        entry->refcount--;

    if (entry->refcount == 0) {
        if (entry->payload)
            releaseClientNotificationToPool(entry->payload);

        entry->payload = nullptr;
        clientNotificationFanoutPool.release(entry);
    }
}

void MeshService::clearClientSlotLocked(PhoneClientSlot &slot)
{
    while (auto entry = slot.packetQueue.dequeuePtr(0)) {
        releasePacketFanoutEntryLocked(entry);
    }

    while (auto entry = slot.queueStatusQueue.dequeuePtr(0)) {
        releaseQueueStatusFanoutEntryLocked(entry);
    }

    while (auto entry = slot.mqttProxyQueue.dequeuePtr(0)) {
        releaseMqttProxyFanoutEntryLocked(entry);
    }

    while (auto entry = slot.clientNotificationQueue.dequeuePtr(0)) {
        releaseClientNotificationFanoutEntryLocked(entry);
    }

    if (slot.packetInflight) {
        releasePacketFanoutEntryLocked(slot.packetInflight);
        slot.packetInflight = nullptr;
    }

    if (slot.queueStatusInflight) {
        releaseQueueStatusFanoutEntryLocked(slot.queueStatusInflight);
        slot.queueStatusInflight = nullptr;
    }

    if (slot.mqttProxyInflight) {
        releaseMqttProxyFanoutEntryLocked(slot.mqttProxyInflight);
        slot.mqttProxyInflight = nullptr;
    }

    if (slot.clientNotificationInflight) {
        releaseClientNotificationFanoutEntryLocked(slot.clientNotificationInflight);
        slot.clientNotificationInflight = nullptr;
    }

    slot.active = false;
    slot.state = STATE_DISCONNECTED;
    slot.client = nullptr;
}

void MeshService::updateApiStateLocked(APIState preferred)
{
    api_state_mask = 0;
    for (int state = STATE_BLE; state <= STATE_ETH; state++) {
        if (apiStateCounts[state] > 0) {
            api_state_mask |= apiStateBit(static_cast<APIState>(state));
        }
    }

    if (preferred != STATE_DISCONNECTED && apiStateCounts[preferred] > 0) {
        api_state = preferred;
        return;
    }

    if (api_state != STATE_DISCONNECTED && apiStateCounts[api_state] > 0) {
        return;
    }

    for (int state = STATE_BLE; state <= STATE_ETH; state++) {
        if (apiStateCounts[state] > 0) {
            api_state = static_cast<APIState>(state);
            return;
        }
    }

    api_state = STATE_DISCONNECTED;
}

bool MeshService::enqueuePacketFanoutLocked(meshtastic_MeshPacket *p)
{
    if (!p)
        return false;

    auto entry = packetFanoutPool.allocZeroed();
    if (!entry) {
        LOG_WARN("Failed to allocate packet fanout entry");
        releaseToPool(p);
        return false;
    }

    entry->payload = p;

    bool delivered = false;
    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        auto &slot = phoneClients[i];
        if (!slot.active)
            continue;

        if (slot.packetQueue.numFree() == 0) {
            LOG_WARN("Packet fanout queue full for client slot %d, drop oldest", i);
            if (auto oldEntry = slot.packetQueue.dequeuePtr(0)) {
                releasePacketFanoutEntryLocked(oldEntry);
            }
        }

        if (slot.packetQueue.enqueue(entry, 0)) {
            entry->refcount++;
            delivered = true;
        } else {
            LOG_WARN("Failed to enqueue packet fanout for client slot %d", i);
        }
    }

    if (!delivered) {
        releaseToPool(p);
        entry->payload = nullptr;
        packetFanoutPool.release(entry);
        return false;
    }

    return true;
}

bool MeshService::enqueueQueueStatusFanoutLocked(meshtastic_QueueStatus *qs)
{
    if (!qs)
        return false;

    auto entry = queueStatusFanoutPool.allocZeroed();
    if (!entry) {
        LOG_WARN("Failed to allocate queue status fanout entry");
        releaseQueueStatusToPool(qs);
        return false;
    }

    entry->payload = qs;

    bool delivered = false;
    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        auto &slot = phoneClients[i];
        if (!slot.active)
            continue;

        if (slot.queueStatusQueue.numFree() == 0) {
            LOG_INFO("QueueStatus fanout queue full for client slot %d, discard oldest", i);
            if (auto oldEntry = slot.queueStatusQueue.dequeuePtr(0)) {
                releaseQueueStatusFanoutEntryLocked(oldEntry);
            }
        }

        if (slot.queueStatusQueue.enqueue(entry, 0)) {
            entry->refcount++;
            delivered = true;
        } else {
            LOG_WARN("Failed to enqueue QueueStatus fanout for client slot %d", i);
        }
    }

    if (!delivered) {
        releaseQueueStatusToPool(qs);
        entry->payload = nullptr;
        queueStatusFanoutPool.release(entry);
        return false;
    }

    return true;
}

bool MeshService::enqueueMqttProxyFanoutLocked(meshtastic_MqttClientProxyMessage *m)
{
    if (!m)
        return false;

    auto entry = mqttProxyFanoutPool.allocZeroed();
    if (!entry) {
        LOG_WARN("Failed to allocate MQTT proxy fanout entry");
        releaseMqttClientProxyMessageToPool(m);
        return false;
    }

    entry->payload = m;

    bool delivered = false;
    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        auto &slot = phoneClients[i];
        if (!slot.active)
            continue;

        if (slot.mqttProxyQueue.numFree() == 0) {
            LOG_WARN("MqttClientProxy fanout queue full for client slot %d, discard oldest", i);
            if (auto oldEntry = slot.mqttProxyQueue.dequeuePtr(0)) {
                releaseMqttProxyFanoutEntryLocked(oldEntry);
            }
        }

        if (slot.mqttProxyQueue.enqueue(entry, 0)) {
            entry->refcount++;
            delivered = true;
        } else {
            LOG_WARN("Failed to enqueue MqttClientProxy fanout for client slot %d", i);
        }
    }

    if (!delivered) {
        releaseMqttClientProxyMessageToPool(m);
        entry->payload = nullptr;
        mqttProxyFanoutPool.release(entry);
        return false;
    }

    return true;
}

bool MeshService::enqueueClientNotificationFanoutLocked(meshtastic_ClientNotification *cn)
{
    if (!cn)
        return false;

    auto entry = clientNotificationFanoutPool.allocZeroed();
    if (!entry) {
        LOG_WARN("Failed to allocate ClientNotification fanout entry");
        releaseClientNotificationToPool(cn);
        return false;
    }

    entry->payload = cn;

    bool delivered = false;
    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        auto &slot = phoneClients[i];
        if (!slot.active)
            continue;

        if (slot.clientNotificationQueue.numFree() == 0) {
            LOG_WARN("ClientNotification fanout queue full for client slot %d, discard oldest", i);
            if (auto oldEntry = slot.clientNotificationQueue.dequeuePtr(0)) {
                releaseClientNotificationFanoutEntryLocked(oldEntry);
            }
        }

        if (slot.clientNotificationQueue.enqueue(entry, 0)) {
            entry->refcount++;
            delivered = true;
        } else {
            LOG_WARN("Failed to enqueue ClientNotification fanout for client slot %d", i);
        }
    }

    if (!delivered) {
        releaseClientNotificationToPool(cn);
        entry->payload = nullptr;
        clientNotificationFanoutPool.release(entry);
        return false;
    }

    return true;
}

bool MeshService::registerPhoneClient(PhoneAPI *client, APIState state)
{
    if (!client || state == STATE_DISCONNECTED)
        return false;

    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex < 0) {
        slotIndex = findFreeClientSlotLocked();
        if (slotIndex < 0) {
            LOG_ERROR("No free phone client slots available (max=%d)", MAX_PHONE_API_CLIENTS);
            return false;
        }
    }

    auto &slot = phoneClients[slotIndex];

    if (slot.active) {
        if (slot.state != STATE_DISCONNECTED && apiStateCounts[slot.state] > 0)
            apiStateCounts[slot.state]--;
    }
    clearClientSlotLocked(slot);

    slot.client = client;
    slot.state = state;
    slot.active = true;

    apiStateCounts[state]++;
    updateApiStateLocked(state);

    return true;
}

void MeshService::unregisterPhoneClient(PhoneAPI *client)
{
    if (!client)
        return;

    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex < 0)
        return;

    auto &slot = phoneClients[slotIndex];

    if (slot.active && slot.state != STATE_DISCONNECTED && apiStateCounts[slot.state] > 0) {
        apiStateCounts[slot.state]--;
    }

    clearClientSlotLocked(slot);
    updateApiStateLocked();
}

meshtastic_MeshPacket *MeshService::getForPhone(PhoneAPI *client)
{
    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex < 0)
        return nullptr;

    auto &slot = phoneClients[slotIndex];
    if (!slot.active)
        return nullptr;

    if (!slot.packetInflight)
        slot.packetInflight = slot.packetQueue.dequeuePtr(0);

    return slot.packetInflight ? slot.packetInflight->payload : nullptr;
}

meshtastic_QueueStatus *MeshService::getQueueStatusForPhone(PhoneAPI *client)
{
    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex < 0)
        return nullptr;

    auto &slot = phoneClients[slotIndex];
    if (!slot.active)
        return nullptr;

    if (!slot.queueStatusInflight)
        slot.queueStatusInflight = slot.queueStatusQueue.dequeuePtr(0);

    return slot.queueStatusInflight ? slot.queueStatusInflight->payload : nullptr;
}

meshtastic_MqttClientProxyMessage *MeshService::getMqttClientProxyMessageForPhone(PhoneAPI *client)
{
    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex < 0)
        return nullptr;

    auto &slot = phoneClients[slotIndex];
    if (!slot.active)
        return nullptr;

    if (!slot.mqttProxyInflight)
        slot.mqttProxyInflight = slot.mqttProxyQueue.dequeuePtr(0);

    return slot.mqttProxyInflight ? slot.mqttProxyInflight->payload : nullptr;
}

meshtastic_ClientNotification *MeshService::getClientNotificationForPhone(PhoneAPI *client)
{
    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex < 0)
        return nullptr;

    auto &slot = phoneClients[slotIndex];
    if (!slot.active)
        return nullptr;

    if (!slot.clientNotificationInflight)
        slot.clientNotificationInflight = slot.clientNotificationQueue.dequeuePtr(0);

    return slot.clientNotificationInflight ? slot.clientNotificationInflight->payload : nullptr;
}

void MeshService::releaseToPoolForPhone(PhoneAPI *client, meshtastic_MeshPacket *p)
{
    if (!p)
        return;

    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex >= 0) {
        auto &slot = phoneClients[slotIndex];
        if (slot.packetInflight && slot.packetInflight->payload == p) {
            auto *entry = slot.packetInflight;
            slot.packetInflight = nullptr;
            releasePacketFanoutEntryLocked(entry);
            return;
        }
    }

    LOG_WARN("Packet release mismatch in fanout, releasing directly");
    releaseToPool(p);
}

void MeshService::releaseQueueStatusToPoolForPhone(PhoneAPI *client, meshtastic_QueueStatus *p)
{
    if (!p)
        return;

    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex >= 0) {
        auto &slot = phoneClients[slotIndex];
        if (slot.queueStatusInflight && slot.queueStatusInflight->payload == p) {
            auto *entry = slot.queueStatusInflight;
            slot.queueStatusInflight = nullptr;
            releaseQueueStatusFanoutEntryLocked(entry);
            return;
        }
    }

    LOG_WARN("QueueStatus release mismatch in fanout, releasing directly");
    releaseQueueStatusToPool(p);
}

void MeshService::releaseMqttClientProxyMessageToPoolForPhone(PhoneAPI *client, meshtastic_MqttClientProxyMessage *p)
{
    if (!p)
        return;

    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex >= 0) {
        auto &slot = phoneClients[slotIndex];
        if (slot.mqttProxyInflight && slot.mqttProxyInflight->payload == p) {
            auto *entry = slot.mqttProxyInflight;
            slot.mqttProxyInflight = nullptr;
            releaseMqttProxyFanoutEntryLocked(entry);
            return;
        }
    }

    LOG_WARN("MqttClientProxy release mismatch in fanout, releasing directly");
    releaseMqttClientProxyMessageToPool(p);
}

void MeshService::releaseClientNotificationToPoolForPhone(PhoneAPI *client, meshtastic_ClientNotification *p)
{
    if (!p)
        return;

    concurrency::LockGuard guard(&phoneClientsLock);

    int slotIndex = findClientSlotByPtrLocked(client);
    if (slotIndex >= 0) {
        auto &slot = phoneClients[slotIndex];
        if (slot.clientNotificationInflight && slot.clientNotificationInflight->payload == p) {
            auto *entry = slot.clientNotificationInflight;
            slot.clientNotificationInflight = nullptr;
            releaseClientNotificationFanoutEntryLocked(entry);
            return;
        }
    }

    LOG_WARN("ClientNotification release mismatch in fanout, releasing directly");
    releaseClientNotificationToPool(p);
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
    bool isPreferredRebroadcaster = config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER;
    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP && mp->decoded.request_id > 0) {
        LOG_DEBUG("Received telemetry response. Skip sending our NodeInfo");
        //  ignore our request for its NodeInfo
    } else if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag && !nodeDB->getMeshNode(mp->from)->has_user &&
               nodeInfoModule && !isPreferredRebroadcaster && !nodeDB->isFull()) {
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
    concurrency::LockGuard guard(&phoneClientsLock);

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        auto &slot = phoneClients[i];
        if (!slot.active)
            continue;

        if (slot.packetInflight && slot.packetInflight->payload && slot.packetInflight->payload->id == request_id) {
            return slot.packetInflight->payload->to;
        }

        int used = slot.packetQueue.numUsed();
        for (int q = 0; q < used; q++) {
            auto *entry = slot.packetQueue.dequeuePtr(0);
            if (!entry)
                break;

            NodeNum nodenum = 0;
            if (entry->payload && entry->payload->id == request_id) {
                nodenum = entry->payload->to;
            }

            // put it right back on the queue
            slot.packetQueue.enqueue(entry, 0);

            if (nodenum != 0) {
                return nodenum;
            }
        }
    }

    return 0;
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

    IF_SCREEN(if (p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && p.decoded.payload.size > 0 &&
                  p.to != NODENUM_BROADCAST && p.to != 0) // DM only
              {
                  perhapsDecode(&p);
                  const StoredMessage &sm = messageStore.addFromPacket(p);
                  graphics::MessageRenderer::handleNewMessage(nullptr, sm, p); // notify UI
              })
    // Send the packet into the mesh
    DEBUG_HEAP_BEFORE;
    auto a = packetPool.allocCopy(p);
    DEBUG_HEAP_AFTER("MeshService::handleToRadio", a);
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

    lastQueueStatus = *copied;

    bool delivered = false;
    {
        concurrency::LockGuard guard(&phoneClientsLock);
        delivered = enqueueQueueStatusFanoutLocked(copied);
    }

    if (delivered) {
        fromNum++;
        return ERRNO_OK;
    }

    return ERRNO_UNKNOWN;
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
        DEBUG_HEAP_BEFORE;
        auto a = packetPool.allocCopy(*p);
        DEBUG_HEAP_AFTER("MeshService::sendToMesh", a);

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
        if (api_state_mask != 0)
            fromNum++; // Notify observers for packet from radio when there are active API clients
        return;
    }
#endif
#endif

    bool delivered = false;
    {
        concurrency::LockGuard guard(&phoneClientsLock);
        delivered = enqueuePacketFanoutLocked(p);
    }

    if (delivered) {
        fromNum++;
    }
}

void MeshService::sendMqttMessageToClientProxy(meshtastic_MqttClientProxyMessage *m)
{
    LOG_DEBUG("Send mqtt message on topic '%s' to client for proxy", m->topic);

    bool delivered = false;
    {
        concurrency::LockGuard guard(&phoneClientsLock);
        delivered = enqueueMqttProxyFanoutLocked(m);
    }

    if (delivered) {
        fromNum++;
    }
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

    bool delivered = false;
    {
        concurrency::LockGuard guard(&phoneClientsLock);
        delivered = enqueueClientNotificationFanoutLocked(n);
    }

    if (delivered) {
        fromNum++;
    }
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
    concurrency::LockGuard guard(&phoneClientsLock);

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        auto &slot = phoneClients[i];
        if (!slot.active)
            continue;

        if (slot.packetInflight || !slot.packetQueue.isEmpty()) {
            return false;
        }
    }

    return true;
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
