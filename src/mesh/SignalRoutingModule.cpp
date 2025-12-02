#include "SignalRoutingModule.h"
#include "graph/Graph.h"
#include "MeshService.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "memGet.h"
#include "pb_decode.h"
#include <Arduino.h>
#include <algorithm>

SignalRoutingModule *signalRoutingModule;

// Helper to get node display name for logging
static void getNodeDisplayName(NodeNum nodeId, char *buf, size_t bufSize) {
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    if (node && node->has_user && node->user.long_name[0]) {
        snprintf(buf, bufSize, "%s (%s, %08x)", node->user.long_name, node->user.short_name, nodeId);
    } else {
        snprintf(buf, bufSize, "Unknown (%08x)", nodeId);
    }
}

SignalRoutingModule::SignalRoutingModule()
    : ProtobufModule("SignalRouting", meshtastic_PortNum_SIGNAL_ROUTING_APP, &meshtastic_SignalRoutingInfo_msg),
      concurrency::OSThread("SignalRouting")
{
#ifdef ARCH_STM32WL
    // STM32WL only has 64KB RAM total - disable signal routing entirely
    LOG_INFO("SignalRouting: Disabled on STM32WL (insufficient RAM)");
    routingGraph = nullptr;
    disable();
    return;
#endif

#ifdef ARCH_RP2040
    // RP2040 RAM guard: Graph uses ~25-35KB worst case (100 nodes, 6 edges each)
    // 30KB threshold leaves headroom for graph + Dijkstra temp allocations
    uint32_t freeHeap = memGet.getFreeHeap();
    if (freeHeap < 30 * 1024) {
        LOG_WARN("SignalRouting: Insufficient RAM on RP2040 (%u bytes free), disabling signal-based routing", freeHeap);
        routingGraph = nullptr;
        disable();
        return;
    }
#endif

    routingGraph = new Graph();
    trackNodeCapability(nodeDB->getNodeNum(), CapabilityStatus::Capable);
    uint32_t nowMs = millis();
    lastHeartbeatTime = nowMs;
    lastNotificationTime = nowMs;

    // We want to see all packets for signal quality updates
    isPromiscuous = true;

    // Set initial broadcast delay (30 seconds after startup)
    setIntervalFromNow(30 * 1000);

    // Initialize RGB LED pins and turn off
#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    pinMode(RGBLED_RED, OUTPUT);
    pinMode(RGBLED_GREEN, OUTPUT);
    pinMode(RGBLED_BLUE, OUTPUT);
#ifdef RGBLED_CA
    // Common anode: high = off
    analogWrite(RGBLED_RED, 255);
    analogWrite(RGBLED_GREEN, 255);
    analogWrite(RGBLED_BLUE, 255);
#else
    // Common cathode: low = off
    analogWrite(RGBLED_RED, 0);
    analogWrite(RGBLED_GREEN, 0);
    analogWrite(RGBLED_BLUE, 0);
#endif
    // Initialize heartbeat timing so first heartbeat is delayed
    LOG_INFO("SignalRouting: RGB LED initialized");
#endif

    LOG_INFO("SignalRouting: Module initialized (version %d)", SIGNAL_ROUTING_VERSION);
}

int32_t SignalRoutingModule::runOnce()
{
    uint32_t nowMs = millis();
    uint32_t nowSecs = getTime();

    pruneCapabilityCache(nowSecs);
    pruneRelayIdentityCache(nowMs);
    processSpeculativeRetransmits(nowMs);

#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    updateRgbLed();
    bool notificationsIdle = (nowMs - lastNotificationTime) > MIN_FLASH_INTERVAL_MS;
    bool heartbeatDue = (nowMs - lastHeartbeatTime) >= heartbeatIntervalMs;
    if (!rgbLedActive && notificationsIdle && heartbeatDue) {
        flashRgbLed(24, 24, 24, HEARTBEAT_FLASH_MS);
        lastHeartbeatTime = nowMs;
    }
#endif

    if (routingGraph && signalBasedRoutingEnabled) {
        if (nowMs - lastBroadcast >= SIGNAL_ROUTING_BROADCAST_SECS * 1000) {
            sendSignalRoutingInfo();
        }
    }

    uint32_t timeToHeartbeat = heartbeatIntervalMs;
    if (nowMs - lastHeartbeatTime < heartbeatIntervalMs) {
        timeToHeartbeat = heartbeatIntervalMs - (nowMs - lastHeartbeatTime);
    }

    uint32_t timeToBroadcast = SIGNAL_ROUTING_BROADCAST_SECS * 1000;
    if (nowMs - lastBroadcast < SIGNAL_ROUTING_BROADCAST_SECS * 1000) {
        timeToBroadcast = (SIGNAL_ROUTING_BROADCAST_SECS * 1000) - (nowMs - lastBroadcast);
    }

    uint32_t timeToSpeculative = timeToBroadcast;
    if (!speculativeRetransmits.empty()) {
        uint32_t soonest = timeToBroadcast;
        for (const auto& entry : speculativeRetransmits) {
            if (entry.second.expiryMs > nowMs) {
                soonest = std::min(soonest, entry.second.expiryMs - nowMs);
            } else {
                soonest = 0;
                break;
            }
        }
        timeToSpeculative = soonest;
    }

    uint32_t timeToLed = UINT32_MAX;
#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    if (rgbLedActive) {
        if (rgbLedOffTime > nowMs) {
            timeToLed = rgbLedOffTime - nowMs;
        } else {
            timeToLed = 0;
        }
    }
#endif

    uint32_t nextDelay = std::min({timeToHeartbeat, timeToBroadcast, timeToSpeculative, timeToLed});
    if (nextDelay < 20) {
        nextDelay = 20;
    }
    return static_cast<int32_t>(nextDelay);
}

void SignalRoutingModule::sendSignalRoutingInfo(NodeNum dest)
{
    meshtastic_SignalRoutingInfo info = meshtastic_SignalRoutingInfo_init_zero;
    buildSignalRoutingInfo(info);

    char ourName[64];
    getNodeDisplayName(nodeDB->getNodeNum(), ourName, sizeof(ourName));

    if (info.neighbors_count > 0) {
        meshtastic_MeshPacket *p = allocDataProtobuf(info);
        p->to = dest;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

        LOG_INFO("SignalRouting: Broadcasting %d neighbors from %s", info.neighbors_count, ourName);

        service->sendToMesh(p);
        lastBroadcast = millis();

        // Record our transmission for contention window tracking
        if (routingGraph) {
            uint32_t currentTime = getValidTime(RTCQualityFromNet);
            if (!currentTime) {
                currentTime = getTime();
            }
            routingGraph->recordNodeTransmission(nodeDB->getNodeNum(), p->id, currentTime);
        }
    } else {
        LOG_INFO("SignalRouting: No direct neighbors to broadcast from %s (waiting for direct packets)", ourName);
    }
}

void SignalRoutingModule::buildSignalRoutingInfo(meshtastic_SignalRoutingInfo &info)
{
    info.node_id = nodeDB->getNodeNum();
    info.signal_based_capable = true;
    info.routing_version = SIGNAL_ROUTING_VERSION;
    info.neighbors_count = 0;

    if (!routingGraph) return;

    const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!edges || edges->empty()) {
        return;
    }

    // Copy up to MAX_SIGNAL_ROUTING_NEIGHBORS
    size_t count = std::min(edges->size(), static_cast<size_t>(MAX_SIGNAL_ROUTING_NEIGHBORS));
    info.neighbors_count = count;

    for (size_t i = 0; i < count; i++) {
        const Edge& edge = (*edges)[i];
        meshtastic_NeighborLink& neighbor = info.neighbors[i];

        neighbor.node_id = edge.to;
        neighbor.last_rx_time = edge.lastUpdate;
        neighbor.position_variance = edge.variance;
        neighbor.signal_based_capable = isSignalBasedCapable(edge.to);

        // Convert ETX back to approximate RSSI/SNR
        Graph::etxToSignal(edge.etx, neighbor.rssi, neighbor.snr);
    }
}

void SignalRoutingModule::preProcessSignalRoutingPacket(const meshtastic_MeshPacket *p)
{
    if (!routingGraph || !p) return;

    // Only process SignalRoutingInfo packets
    if (p->decoded.portnum != meshtastic_PortNum_SIGNAL_ROUTING_APP) return;

    // Decode the protobuf to get neighbor data
    meshtastic_SignalRoutingInfo info = meshtastic_SignalRoutingInfo_init_zero;
    if (!pb_decode_from_bytes(p->decoded.payload.bytes, p->decoded.payload.size,
                              &meshtastic_SignalRoutingInfo_msg, &info)) {
        return;
    }

    if (info.neighbors_count == 0) return;

    trackNodeCapability(p->from, info.signal_based_capable ? CapabilityStatus::Capable : CapabilityStatus::Legacy);

    char senderName[64];
    getNodeDisplayName(p->from, senderName, sizeof(senderName));
    LOG_DEBUG("SignalRouting: Pre-processing %d neighbors from %s for relay decision",
              info.neighbors_count, senderName);

    // Add edges from the sender to each of their neighbors
    for (pb_size_t i = 0; i < info.neighbors_count; i++) {
        const meshtastic_NeighborLink& neighbor = info.neighbors[i];
        trackNodeCapability(neighbor.node_id,
                            neighbor.signal_based_capable ? CapabilityStatus::Capable : CapabilityStatus::Legacy);
        float etx = Graph::calculateETX(neighbor.rssi, neighbor.snr);
        routingGraph->updateEdge(p->from, neighbor.node_id, etx, neighbor.last_rx_time, neighbor.position_variance);
    }
}

bool SignalRoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_SignalRoutingInfo *p)
{
    // Note: Graph may have already been updated by preProcessSignalRoutingPacket()
    // This is intentional - we want up-to-date data for relay decisions
    if (!routingGraph || !p) return false;

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    trackNodeCapability(mp.from, p->signal_based_capable ? CapabilityStatus::Capable : CapabilityStatus::Legacy);

    if (p->neighbors_count == 0) {
        LOG_DEBUG("SignalRouting: %s has no neighbors (version %d)", senderName, p->routing_version);
        return false;
    }

    LOG_INFO("SignalRouting: Received %d neighbors from %s (version %d, capable=%s)",
             p->neighbors_count, senderName, p->routing_version,
             p->signal_based_capable ? "true" : "false");

    // Flash cyan for network topology update
    flashRgbLed(0, 255, 255, 150);

    // Add edges from the sender to each of their neighbors
    // (This may be redundant if preProcessSignalRoutingPacket already ran, but it's idempotent)
    for (pb_size_t i = 0; i < p->neighbors_count; i++) {
        const meshtastic_NeighborLink& neighbor = p->neighbors[i];

        char neighborName[64];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));

        trackNodeCapability(neighbor.node_id,
                            neighbor.signal_based_capable ? CapabilityStatus::Capable : CapabilityStatus::Legacy);

        // Calculate ETX from the received RSSI/SNR
        float etx = Graph::calculateETX(neighbor.rssi, neighbor.snr);

        // Add edge: sender -> neighbor with variance for route cost calculation
        routingGraph->updateEdge(mp.from, neighbor.node_id, etx, neighbor.last_rx_time, neighbor.position_variance);

        LOG_INFO("  -> %s: RSSI=%d, SNR=%d, ETX=%.2f, variance=%u, capable=%s",
                 neighborName, neighbor.rssi, neighbor.snr, etx, neighbor.position_variance,
                 neighbor.signal_based_capable ? "true" : "false");
    }

    // Allow others to see this packet too
    return false;
}

ProcessMessage SignalRoutingModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.request_id != 0 &&
        mp.to == nodeDB->getNodeNum()) {
        cancelSpeculativeRetransmit(nodeDB->getNodeNum(), mp.decoded.request_id);
    }

    // Only track DIRECT neighbors - packets heard directly over radio with no relays
    // Conditions for a direct neighbor:
    // 1. Has valid signal data (rx_rssi or rx_snr)
    // 2. Not received via MQTT
    // 3. relay_node matches the last byte of mp.from (meaning the sender transmitted directly to us)
    //    When a packet is relayed, relay_node is set to the relayer's last byte, not the original sender's
    
    bool hasSignalData = (mp.rx_rssi != 0 || mp.rx_snr != 0);
    bool notViaMqtt = !mp.via_mqtt;
    uint8_t fromLastByte = mp.from & 0xFF;
    bool isDirectFromSender = (mp.relay_node == fromLastByte);
    
    // Debug logging to understand why packets might not be tracked
    if (hasSignalData) {
        LOG_DEBUG("SignalRouting: Packet from 0x%08x: relay=0x%02x, fromLastByte=0x%02x, viaMqtt=%d, direct=%d",
                  mp.from, mp.relay_node, fromLastByte, mp.via_mqtt, isDirectFromSender);
    }
    
    if (hasSignalData && notViaMqtt && isDirectFromSender) {
        rememberRelayIdentity(mp.from, fromLastByte);
        trackNodeCapability(mp.from, CapabilityStatus::Unknown);

        char senderName[64];
        getNodeDisplayName(mp.from, senderName, sizeof(senderName));

        float etx = Graph::calculateETX(mp.rx_rssi, mp.rx_snr);
        LOG_INFO("SignalRouting: Direct neighbor %s: RSSI=%d, SNR=%.1f, ETX=%.2f",
                 senderName, mp.rx_rssi, mp.rx_snr, etx);

        // Brief purple flash for any direct packet received
        flashRgbLed(128, 0, 128, 100);

        // Record that this node transmitted (for contention window tracking)
        if (routingGraph) {
            uint32_t currentTime = getValidTime(RTCQualityFromNet);
            if (!currentTime) {
                currentTime = getTime();
            }
            routingGraph->recordNodeTransmission(mp.from, mp.id, currentTime);
        }

        updateNeighborInfo(mp.from, mp.rx_rssi, mp.rx_snr, mp.rx_time);
    }

    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP) {
        handleNodeInfoPacket(mp);
    }

    // Periodic graph maintenance
    if (routingGraph) {
        uint32_t currentTime = getValidTime(RTCQualityFromNet);
        if (!currentTime) {
            currentTime = getTime();
        }
        if (currentTime - lastGraphUpdate > GRAPH_UPDATE_INTERVAL_MS) {
            routingGraph->ageEdges(currentTime);
            lastGraphUpdate = currentTime;
            LOG_DEBUG("SignalRouting: Aged edges and updated graph");
        }
    }

    return ProcessMessage::CONTINUE;
}

bool SignalRoutingModule::shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p)
{
    if (!p || !signalBasedRoutingEnabled || !routingGraph) {
        return false;
    }

    if (isBroadcast(p->to)) {
        return topologyHealthyForBroadcast();
    }

    if (!topologyHealthyForUnicast(p->to)) {
        return false;
    }

    if (!isSignalBasedCapable(p->to) && !isLegacyRouter(p->to)) {
        return false;
    }

    NodeNum nextHop = getNextHop(p->to);
    if (nextHop == 0) {
        return false;
    }

    if (!isSignalBasedCapable(nextHop) && !isLegacyRouter(nextHop)) {
        return false;
    }

    return true;
}

bool SignalRoutingModule::shouldRelayBroadcast(const meshtastic_MeshPacket *p)
{
    if (!routingGraph || !isBroadcast(p->to)) {
        return true;
    }

    if (!topologyHealthyForBroadcast()) {
        return true;
    }

    if (p->decoded.portnum == meshtastic_PortNum_SIGNAL_ROUTING_APP) {
        preProcessSignalRoutingPacket(p);
    }

    NodeNum myNode = nodeDB->getNodeNum();
    NodeNum sourceNode = p->from;
    NodeNum heardFrom = resolveHeardFrom(p, sourceNode);

    uint32_t currentTime = getValidTime(RTCQualityFromNet);
    if (!currentTime) {
        currentTime = getTime();
    }

    bool shouldRelay = routingGraph->shouldRelayEnhanced(myNode, sourceNode, heardFrom, currentTime, p->id);

    char myName[64], sourceName[64], heardFromName[64];
    getNodeDisplayName(myNode, myName, sizeof(myName));
    getNodeDisplayName(sourceNode, sourceName, sizeof(sourceName));
    getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));

    LOG_INFO("SignalRouting: Broadcast from %s (heard via %s): %s relay",
             sourceName, heardFromName, shouldRelay ? "SHOULD" : "should NOT");

    if (shouldRelay) {
        routingGraph->recordNodeTransmission(myNode, p->id, currentTime);
        flashRgbLed(255, 128, 0, 150);
    } else {
        flashRgbLed(255, 0, 0, 100);
    }

    return shouldRelay;
}

NodeNum SignalRoutingModule::getNextHop(NodeNum destination)
{
    if (!routingGraph) {
        return 0;
    }

    uint32_t currentTime = getValidTime(RTCQualityFromNet);
    if (!currentTime) {
        currentTime = getTime();
    }
    Route route = routingGraph->calculateRoute(destination, currentTime);

    if (route.nextHop != 0) {
        LOG_DEBUG("SignalRouting: Next hop for %08x is %08x (cost: %.2f)", destination, route.nextHop, route.cost);
        return route.nextHop;
    }

    return 0; // No route found
}

void SignalRoutingModule::updateNeighborInfo(NodeNum nodeId, int32_t rssi, float snr, uint32_t lastRxTime, uint32_t variance)
{
    if (!routingGraph) return;

    // Calculate ETX and update the graph
    float etx = Graph::calculateETX(rssi, snr);
    int changeType = routingGraph->updateEdge(nodeDB->getNodeNum(), nodeId, etx, lastRxTime, variance);

    // If significant change, consider sending an update sooner
    if (changeType != Graph::EDGE_NO_CHANGE) {
        char neighborName[64];
        getNodeDisplayName(nodeId, neighborName, sizeof(neighborName));

        if (changeType == Graph::EDGE_NEW) {
            LOG_INFO("SignalRouting: New neighbor %s detected", neighborName);
            // Flash green for new neighbor
            flashRgbLed(0, 255, 0, 300);
        } else if (changeType == Graph::EDGE_SIGNIFICANT_CHANGE) {
            LOG_INFO("SignalRouting: Significant ETX change for %s", neighborName);
            // Flash blue for signal quality change
            flashRgbLed(0, 0, 255, 300);
        }

        // Trigger early broadcast if we haven't sent recently (rate limit: 60s)
        uint32_t now = millis();
        if (now - lastBroadcast > 60 * 1000) {
            setIntervalFromNow(5 * 1000); // Send update in 5 seconds
        }
    }
}

void SignalRoutingModule::handleSpeculativeRetransmit(const meshtastic_MeshPacket *p)
{
    if (!p || !signalBasedRoutingEnabled || !routingGraph) {
        return;
    }

    if (isBroadcast(p->to) || p->from != nodeDB->getNodeNum() || p->id == 0) {
        return;
    }

    if (!shouldUseSignalBasedRouting(p)) {
        return;
    }

    uint64_t key = makeSpeculativeKey(p->from, p->id);
    if (speculativeRetransmits.find(key) != speculativeRetransmits.end()) {
        return;
    }

    meshtastic_MeshPacket *copy = packetPool.allocCopy(*p);
    if (!copy) {
        return;
    }

    SpeculativeRetransmitEntry entry;
    entry.key = key;
    entry.origin = p->from;
    entry.packetId = p->id;
    entry.expiryMs = millis() + SPECULATIVE_RETRANSMIT_TIMEOUT_MS;
    entry.packetCopy = copy;
    speculativeRetransmits[key] = entry;

    LOG_DEBUG("SignalRouting: Speculative retransmit armed for packet %08x (expires in %ums)", p->id,
              SPECULATIVE_RETRANSMIT_TIMEOUT_MS);
}

bool SignalRoutingModule::isSignalBasedCapable(NodeNum nodeId)
{
    if (nodeId == nodeDB->getNodeNum()) {
        return true;
    }

    CapabilityStatus status = getCapabilityStatus(nodeId);
    return status == CapabilityStatus::Capable;
}

float SignalRoutingModule::getSignalBasedCapablePercentage() const
{
    uint32_t now = getTime();
    size_t total = 1;   // include ourselves
    size_t capable = 1; // we are always capable

    size_t nodeCount = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < nodeCount; ++i) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == nodeDB->getNodeNum()) {
            continue;
        }
        if (node->last_heard == 0 || (now - node->last_heard) > CAPABILITY_TTL_SECS) {
            continue;
        }
        total++;
        if (getCapabilityStatus(node->num) == CapabilityStatus::Capable) {
            capable++;
        }
    }

    if (total == 0) {
        return 0.0f;
    }
    return static_cast<float>(capable) / static_cast<float>(total);
}

/**
 * Flash RGB LED for Signal Routing notifications
 * Colors: Green = new neighbor, Blue = signal change, Cyan = topology update
 */
void SignalRoutingModule::flashRgbLed(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms)
{
#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    uint32_t now = millis();

    // Debounce: ignore rapid-fire flash requests
    if (now - lastFlashTime < MIN_FLASH_INTERVAL_MS) {
        return;
    }

    // Set LED to specified color
#ifdef RGBLED_CA
    // Common anode: high = off, low = on (invert values)
    analogWrite(RGBLED_RED, 255 - r);
    analogWrite(RGBLED_GREEN, 255 - g);
    analogWrite(RGBLED_BLUE, 255 - b);
#else
    // Common cathode: low = off, high = on
    analogWrite(RGBLED_RED, r);
    analogWrite(RGBLED_GREEN, g);
    analogWrite(RGBLED_BLUE, b);
#endif

    // Schedule LED off after duration
    rgbLedOffTime = now + duration_ms;
    rgbLedActive = true;
    lastFlashTime = now;

    // Track notification time to prevent heartbeat during active notifications
    lastNotificationTime = now;
#endif
}

/**
 * Turn off RGB LED (called periodically)
 */
void SignalRoutingModule::updateRgbLed()
{
#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    if (rgbLedActive && millis() >= rgbLedOffTime) {
#ifdef RGBLED_CA
        // Common anode: high = off
        analogWrite(RGBLED_RED, 255);
        analogWrite(RGBLED_GREEN, 255);
        analogWrite(RGBLED_BLUE, 255);
#else
        // Common cathode: low = off
        analogWrite(RGBLED_RED, 0);
        analogWrite(RGBLED_GREEN, 0);
        analogWrite(RGBLED_BLUE, 0);
#endif
        rgbLedActive = false;
    }
#endif
}

void SignalRoutingModule::handleNodeInfoPacket(const meshtastic_MeshPacket &mp)
{
    meshtastic_User user = meshtastic_User_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &user)) {
        return;
    }

    CapabilityStatus status = capabilityFromRole(user.role);
    if (status != CapabilityStatus::Unknown) {
        trackNodeCapability(mp.from, status);
    }

    if (user.has_is_unmessagable && user.is_unmessagable) {
        trackNodeCapability(mp.from, CapabilityStatus::Legacy);
    }
}

SignalRoutingModule::CapabilityStatus SignalRoutingModule::capabilityFromRole(
    meshtastic_Config_DeviceConfig_Role role) const
{
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
    case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE:
        return CapabilityStatus::Capable;
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
    case meshtastic_Config_DeviceConfig_Role_TRACKER:
    case meshtastic_Config_DeviceConfig_Role_SENSOR:
    case meshtastic_Config_DeviceConfig_Role_TAK:
    case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:
    case meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND:
        return CapabilityStatus::Legacy;
    default:
        return CapabilityStatus::Unknown;
    }
}

void SignalRoutingModule::trackNodeCapability(NodeNum nodeId, CapabilityStatus status)
{
    if (nodeId == 0) {
        return;
    }

    uint32_t now = getTime();
    auto &record = capabilityRecords[nodeId];
    record.lastUpdated = now;

    if (status == CapabilityStatus::Capable) {
        record.status = CapabilityStatus::Capable;
    } else if (status == CapabilityStatus::Legacy) {
        record.status = CapabilityStatus::Legacy;
    } else if (record.status == CapabilityStatus::Unknown) {
        record.status = CapabilityStatus::Unknown;
    }
}

void SignalRoutingModule::pruneCapabilityCache(uint32_t nowSecs)
{
    for (auto it = capabilityRecords.begin(); it != capabilityRecords.end();) {
        if ((nowSecs - it->second.lastUpdated) > CAPABILITY_TTL_SECS) {
            it = capabilityRecords.erase(it);
        } else {
            ++it;
        }
    }
}

SignalRoutingModule::CapabilityStatus SignalRoutingModule::getCapabilityStatus(NodeNum nodeId) const
{
    auto it = capabilityRecords.find(nodeId);
    if (it == capabilityRecords.end()) {
        return CapabilityStatus::Unknown;
    }

    uint32_t now = getTime();
    if ((now - it->second.lastUpdated) > CAPABILITY_TTL_SECS) {
        return CapabilityStatus::Unknown;
    }

    return it->second.status;
}

bool SignalRoutingModule::isLegacyRouter(NodeNum nodeId) const
{
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    if (!node || !node->has_user) {
        return false;
    }

    auto role = node->user.role;
    return role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
           role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE ||
           role == meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT;
}

bool SignalRoutingModule::topologyHealthyForBroadcast() const
{
    if (!routingGraph) {
        return false;
    }

    if (routingGraph->getNodeCount() < MIN_CAPABLE_NODES) {
        return false;
    }

    return getSignalBasedCapablePercentage() >= MIN_CAPABLE_RATIO;
}

bool SignalRoutingModule::topologyHealthyForUnicast(NodeNum destination) const
{
    if (!routingGraph) {
        return false;
    }

    if (routingGraph->getNodeCount() < 2) {
        return false;
    }

    float ratio = getSignalBasedCapablePercentage();
    if (ratio < (MIN_CAPABLE_RATIO / 2.0f)) {
        return false;
    }

    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(destination);
    if (!node || node->last_heard == 0) {
        return false;
    }

    uint32_t now = getTime();
    return (now - node->last_heard) < CAPABILITY_TTL_SECS;
}

void SignalRoutingModule::rememberRelayIdentity(NodeNum nodeId, uint8_t relayId)
{
    if (relayId == 0 || nodeId == 0) {
        return;
    }

    uint32_t nowMs = millis();
    auto &bucket = relayIdentityCache[relayId];
    bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                [nowMs](const RelayIdentityEntry &entry) {
                                    return (nowMs - entry.lastHeardMs) > RELAY_ID_CACHE_TTL_MS;
                                }),
                 bucket.end());

    for (auto &entry : bucket) {
        if (entry.nodeId == nodeId) {
            entry.lastHeardMs = nowMs;
            return;
        }
    }

    RelayIdentityEntry entry;
    entry.nodeId = nodeId;
    entry.lastHeardMs = nowMs;
    bucket.push_back(entry);
}

void SignalRoutingModule::pruneRelayIdentityCache(uint32_t nowMs)
{
    for (auto it = relayIdentityCache.begin(); it != relayIdentityCache.end();) {
        auto &bucket = it->second;
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [nowMs](const RelayIdentityEntry &entry) {
                                        return (nowMs - entry.lastHeardMs) > RELAY_ID_CACHE_TTL_MS;
                                    }),
                     bucket.end());
        if (bucket.empty()) {
            it = relayIdentityCache.erase(it);
        } else {
            ++it;
        }
    }
}

NodeNum SignalRoutingModule::resolveRelayIdentity(uint8_t relayId) const
{
    auto it = relayIdentityCache.find(relayId);
    if (it == relayIdentityCache.end()) {
        return 0;
    }

    uint32_t nowMs = millis();
    NodeNum bestNode = 0;
    uint32_t newest = 0;
    for (const auto &entry : it->second) {
        if ((nowMs - entry.lastHeardMs) > RELAY_ID_CACHE_TTL_MS) {
            continue;
        }
        if (entry.lastHeardMs >= newest) {
            newest = entry.lastHeardMs;
            bestNode = entry.nodeId;
        }
    }
    return bestNode;
}

NodeNum SignalRoutingModule::resolveHeardFrom(const meshtastic_MeshPacket *p, NodeNum sourceNode) const
{
    if (!p) {
        return sourceNode;
    }

    if (p->relay_node == 0) {
        return sourceNode;
    }

    if ((sourceNode & 0xFF) == p->relay_node) {
        return sourceNode;
    }

    NodeNum resolved = resolveRelayIdentity(p->relay_node);
    if (resolved != 0) {
        return resolved;
    }

    if (routingGraph) {
        auto neighbors = routingGraph->getDirectNeighbors(nodeDB->getNodeNum());
        for (NodeNum neighbor : neighbors) {
            if ((neighbor & 0xFF) == p->relay_node) {
                return neighbor;
            }
        }
    }

    return sourceNode;
}

void SignalRoutingModule::processSpeculativeRetransmits(uint32_t nowMs)
{
    for (auto it = speculativeRetransmits.begin(); it != speculativeRetransmits.end();) {
        if (nowMs >= it->second.expiryMs) {
            if (it->second.packetCopy) {
                LOG_INFO("SignalRouting: Speculative retransmit for packet %08x", it->second.packetId);
                service->sendToMesh(it->second.packetCopy);
                it->second.packetCopy = nullptr;
            }
            it = speculativeRetransmits.erase(it);
        } else {
            ++it;
        }
    }
}

void SignalRoutingModule::cancelSpeculativeRetransmit(NodeNum origin, uint32_t packetId)
{
    uint64_t key = makeSpeculativeKey(origin, packetId);
    auto it = speculativeRetransmits.find(key);
    if (it == speculativeRetransmits.end()) {
        return;
    }

    if (it->second.packetCopy) {
        packetPool.release(it->second.packetCopy);
    }
    speculativeRetransmits.erase(it);
}

uint64_t SignalRoutingModule::makeSpeculativeKey(NodeNum origin, uint32_t packetId)
{
    return (static_cast<uint64_t>(origin) << 32) | packetId;
}
