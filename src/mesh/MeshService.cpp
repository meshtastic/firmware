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
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#include "power.h"
#include <assert.h>
#include <string>

#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "nimble/NimbleBluetooth.h"
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
* If a node ever receives a User (not just the first broadcast) message where the sender node number equals our node number, that
indicates a collision has occurred and the following steps should happen:

If the receiving node (that was already in the mesh)'s macaddr is LOWER than the new User who just tried to sign in: it gets to
keep its nodenum.  We send a broadcast message of OUR User (we use a broadcast so that the other node can receive our message,
considering we have the same id - it also serves to let observers correct their nodedb) - this case is rare so it should be okay.

If any node receives a User where the macaddr is GTE than their local macaddr, they have been vetoed and should pick a new random
nodenum (filtering against whatever it knows about the nodedb) and rebroadcast their User.

FIXME in the initial proof of concept we just skip the entire want/deny flow and just hand pick node numbers at first.
*/

MeshService service;

static MemoryDynamic<meshtastic_MqttClientProxyMessage> staticMqttClientProxyMessagePool;

static MemoryDynamic<meshtastic_QueueStatus> staticQueueStatusPool;

Allocator<meshtastic_MqttClientProxyMessage> &mqttClientProxyMessagePool = staticMqttClientProxyMessagePool;

Allocator<meshtastic_QueueStatus> &queueStatusPool = staticQueueStatusPool;

#include "Router.h"

MeshService::MeshService()
    : toPhoneQueue(MAX_RX_TOPHONE), toPhoneQueueStatusQueue(MAX_RX_TOPHONE), toPhoneMqttProxyQueue(MAX_RX_TOPHONE)
{
    lastQueueStatus = {0, 0, 16, 0};
}

void MeshService::init()
{
    // moved much earlier in boot (called from setup())
    // nodeDB.init();
#if HAS_GPS
    if (gps)
        gpsObserver.observe(&gps->newStatus);
#endif
}

int MeshService::handleFromRadio(const meshtastic_MeshPacket *mp)
{
    powerFSM.trigger(EVENT_PACKET_FOR_PHONE); // Possibly keep the node from sleeping

    nodeDB->updateFrom(*mp); // update our DB state based off sniffing every RX packet from the radio
    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP && mp->decoded.request_id > 0) {
        LOG_DEBUG(
            "Received telemetry response. Skip sending our NodeInfo because this potentially a Repeater which will ignore our "
            "request for its NodeInfo.\n");
    } else if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag && !nodeDB->getMeshNode(mp->from)->has_user &&
               nodeInfoModule) {
        LOG_INFO("Heard a node on channel %d we don't know, sending NodeInfo and asking for a response.\n", mp->channel);
        if (airTime->isTxAllowedChannelUtil(true)) {
            nodeInfoModule->sendOurNodeInfo(mp->from, true, mp->channel);
        } else {
            LOG_DEBUG("Skip sending NodeInfo due to > 25 percent channel util.\n");
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
bool MeshService::reloadConfig(int saveWhat)
{
    // If we can successfully set this radio to these settings, save them to disk

    // This will also update the region as needed
    bool didReset = nodeDB->resetRadioConfig(); // Don't let the phone send us fatally bad settings

    configChanged.notifyObservers(NULL); // This will cause radio hardware to change freqs etc
    nodeDB->saveToDisk(saveWhat);

    return didReset;
}

/// The owner User record just got updated, update our node DB and broadcast the info into the mesh
void MeshService::reloadOwner(bool shouldSave)
{
    // LOG_DEBUG("reloadOwner()\n");
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
#if defined(ARCH_PORTDUINO) && !HAS_RADIO
    // Simulates device is receiving a packet via the LoRa chip
    if (p.decoded.portnum == meshtastic_PortNum_SIMULATOR_APP) {
        // Simulator packet (=Compressed packet) is encapsulated in a MeshPacket, so need to unwrap first
        meshtastic_Compressed scratch;
        meshtastic_Compressed *decoded = NULL;
        if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            p.decoded.payload.size =
                pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_Compressed_msg, &scratch);
            if (p.decoded.payload.size) {
                decoded = &scratch;
                // Extract the original payload and replace
                memcpy(&p.decoded.payload, &decoded->data, sizeof(decoded->data));
                // Switch the port from PortNum_SIMULATOR_APP back to the original PortNum
                p.decoded.portnum = decoded->portnum;
            } else
                LOG_ERROR("Error decoding protobuf for simulator message!\n");
        }
        // Let SimRadio receive as if it did via its LoRa chip
        SimRadio::instance->startReceive(&p);
        return;
    }
#endif
    p.from = 0; // We don't let phones assign nodenums to their sent messages

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
        LOG_DEBUG("NOTE: tophone queue status queue is full, discarding oldest\n");
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

    if (res == ERRNO_OK && ccToPhone) { // Check if p is not released in case it couldn't be sent
        sendToPhone(packetPool.allocCopy(*p));
    }
}

bool MeshService::trySendPosition(NodeNum dest, bool wantReplies)
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());

    assert(node);

    if (hasValidPosition(node)) {
#if HAS_GPS && !MESHTASTIC_EXCLUDE_GPS
        if (positionModule) {
            LOG_INFO("Sending position ping to 0x%x, wantReplies=%d, channel=%d\n", dest, wantReplies, node->channel);
            positionModule->sendOurPosition(dest, wantReplies, node->channel);
            return true;
        }
    } else {
#endif
        if (nodeInfoModule) {
            LOG_INFO("Sending nodeinfo ping to 0x%x, wantReplies=%d, channel=%d\n", dest, wantReplies, node->channel);
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
            LOG_WARN("ToPhone queue is full, discarding oldest\n");
            meshtastic_MeshPacket *d = toPhoneQueue.dequeuePtr(0);
            if (d)
                releaseToPool(d);
        } else {
            LOG_WARN("ToPhone queue is full, dropping packet.\n");
            releaseToPool(p);
            fromNum++; // Make sure to notify observers in case they are reconnected so they can get the packets
            return;
        }
    }

    assert(toPhoneQueue.enqueue(p, 0));
    fromNum++;
}

void MeshService::sendMqttMessageToClientProxy(meshtastic_MqttClientProxyMessage *m)
{
    LOG_DEBUG("Sending mqtt message on topic '%s' to client for proxying to server\n", m->topic);
    if (toPhoneMqttProxyQueue.numFree() == 0) {
        LOG_WARN("MqttClientProxyMessagePool queue is full, discarding oldest\n");
        meshtastic_MqttClientProxyMessage *d = toPhoneMqttProxyQueue.dequeuePtr(0);
        if (d)
            releaseMqttClientProxyMessageToPool(d);
    }

    assert(toPhoneMqttProxyQueue.enqueue(m, 0));
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
#ifdef GPS_EXTRAVERBOSE
        LOG_DEBUG("onGPSchanged() - lost validLocation\n");
#endif
    }
    // Used fixed position if configured regardless of GPS lock
    if (config.position.fixed_position) {
        LOG_WARN("Using fixed position\n");
        pos = TypeConversions::ConvertToPosition(node->position);
    }

    // Add a fresh timestamp
    pos.time = getValidTime(RTCQualityFromNet);

    // In debug logs, identify position by @timestamp:stage (stage 4 = nodeDB)
    LOG_DEBUG("onGPSChanged() pos@%x time=%u lat=%d lon=%d alt=%d\n", pos.timestamp, pos.time, pos.latitude_i, pos.longitude_i,
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