#include "SignalRoutingModule.h"
#ifdef SIGNAL_ROUTING_LITE_MODE
#include "graph/GraphLite.h"
#else
#include "graph/Graph.h"
#endif
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
#include <unordered_set>

SignalRoutingModule *signalRoutingModule;

// Helper to get node display name for logging
static void getNodeDisplayName(NodeNum nodeId, char *buf, size_t bufSize) {
    if (!nodeDB) {
        snprintf(buf, bufSize, "(%08x)", nodeId);
        return;
    }
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    if (node && node->has_user && node->user.long_name[0]) {
        snprintf(buf, bufSize, "%s (%s, %08x)", node->user.long_name, node->user.short_name, nodeId);
    } else {
        snprintf(buf, bufSize, "Unknown (%08x)", nodeId);
    }
}

// Helper to compute age in seconds; returns -1 if unknown/invalid
static int32_t computeAgeSecs(uint32_t last, uint32_t now)
{
    static constexpr uint32_t MAX_AGE_DISPLAY_SEC = 30 * 24 * 60 * 60; // 30 days
    if (!last) return -1;
    // Guard against bogus future timestamps (e.g., legacy nodes that send 0/invalid)
    if (last > now + 86400) return -1;
    int32_t age = static_cast<int32_t>(now - last);
    if (age < 0) age = 0;
    if (static_cast<uint32_t>(age) > MAX_AGE_DISPLAY_SEC) return -1;
    return age;
}



SignalRoutingModule::SignalRoutingModule()
    : ProtobufModule("SignalRouting", meshtastic_PortNum_SIGNAL_ROUTING_APP, &meshtastic_SignalRoutingInfo_msg),
      concurrency::OSThread("SignalRouting")
{
#ifdef ARCH_STM32WL
    // STM32WL only has 64KB RAM total - disable signal routing entirely
    LOG_INFO("[SR] Disabled on STM32WL (insufficient RAM)");
    routingGraph = nullptr;
    disable();
    return;
#endif

#ifdef ARCH_RP2040
    // RP2040 RAM guard: Graph uses ~25-35KB worst case (100 nodes, 6 edges each)
    // 30KB threshold leaves headroom for graph + Dijkstra temp allocations
    uint32_t freeHeap = memGet.getFreeHeap();
    if (freeHeap < 30 * 1024) {
        LOG_WARN("[SR] Insufficient RAM on RP2040 (%u bytes free), disabling signal-based routing", freeHeap);
        routingGraph = nullptr;
        disable();
        return;
    }
#endif
    // Simple flag-based selection
    #if SIGNAL_ROUTING_LITE_MODE == 1
        LOG_INFO("[SR] Using lite mode (SIGNAL_ROUTING_LITE_MODE=1)");
        routingGraph = new GraphLite();
    #else
        LOG_INFO("[SR] Using full graph mode (SIGNAL_ROUTING_LITE_MODE=0 or undefined)");
        routingGraph = new Graph();
    #endif

    if (!routingGraph) {
        LOG_WARN("[SR] Failed to allocate Graph, disabling signal-based routing");
        disable();
        return;
    }

    if (!nodeDB) {
        LOG_WARN("[SR] NodeDB not available, disabling signal-based routing");
        delete routingGraph;
        routingGraph = nullptr;
        disable();
        return;
    }

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
    LOG_INFO("[SR] RGB LED initialized");
#endif

    LOG_INFO("[SR] Module initialized (version %d)", SIGNAL_ROUTING_VERSION);
}

int32_t SignalRoutingModule::runOnce()
{
    uint32_t nowMs = millis();
    uint32_t nowSecs = getTime();

    pruneCapabilityCache(nowSecs);
    pruneGatewayRelations(nowSecs);
    pruneRelayIdentityCache(nowMs);
    processSpeculativeRetransmits(nowMs);

#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    updateRgbLed();
    bool notificationsIdle = (nowMs - lastNotificationTime) > MIN_FLASH_INTERVAL_MS;
    bool heartbeatDue = (nowMs - lastHeartbeatTime) >= heartbeatIntervalMs;
    if (!rgbLedActive && notificationsIdle && heartbeatDue) {
        flashRgbLed(24, 24, 24, HEARTBEAT_FLASH_MS, false);
        lastHeartbeatTime = nowMs;
    }
#endif

    if (routingGraph && signalBasedRoutingEnabled) {
        if (nowMs - lastBroadcast >= SIGNAL_ROUTING_BROADCAST_SECS * 1000) {
            sendSignalRoutingInfo();
        }

        // Periodic topology logging (every 5 minutes)
        static uint32_t lastTopologyLog = 0;
        if (nowMs - lastTopologyLog >= 300 * 1000) {
            logNetworkTopology();
            lastTopologyLog = nowMs;
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
#ifdef SIGNAL_ROUTING_LITE_MODE
    if (speculativeRetransmitCount > 0) {
        uint32_t soonest = timeToBroadcast;
        for (uint8_t i = 0; i < speculativeRetransmitCount; i++) {
            if (speculativeRetransmits[i].expiryMs > nowMs) {
                soonest = std::min(soonest, speculativeRetransmits[i].expiryMs - nowMs);
            } else {
                soonest = 0;
                break;
            }
        }
        timeToSpeculative = soonest;
    }
#else
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
#endif

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
    if (!isActiveRoutingRole()) {
        return;
    }

    meshtastic_SignalRoutingInfo info = meshtastic_SignalRoutingInfo_init_zero;
    buildSignalRoutingInfo(info);

    char ourName[64];
    getNodeDisplayName(nodeDB->getNodeNum(), ourName, sizeof(ourName));

    // Always send SignalRoutingInfo to announce our capability, even with 0 neighbors
    meshtastic_MeshPacket *p = allocDataProtobuf(info);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    LOG_INFO("[SR] SENDING: Broadcasting %d neighbors from %s (capable=%s) to network",
             info.neighbors_count, ourName, info.signal_based_capable ? "yes" : "no");

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
}

void SignalRoutingModule::buildSignalRoutingInfo(meshtastic_SignalRoutingInfo &info)
{
    info.node_id = nodeDB->getNodeNum();
    info.signal_based_capable = isActiveRoutingRole();
    info.routing_version = SIGNAL_ROUTING_VERSION;
    info.neighbors_count = 0;

    if (!routingGraph || !nodeDB) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* nodeEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!nodeEdges || nodeEdges->edgeCount == 0) {
        return;
    }

    // Prefer reported edges (peer perspective) over mirrored estimates, then order by ETX
    const EdgeLite* reported[GRAPH_LITE_MAX_EDGES_PER_NODE];
    const EdgeLite* mirrored[GRAPH_LITE_MAX_EDGES_PER_NODE];
    uint8_t reportedCount = 0;
    uint8_t mirroredCount = 0;

    for (uint8_t i = 0; i < nodeEdges->edgeCount; i++) {
        const EdgeLite* e = &nodeEdges->edges[i];
        if (e->source == EdgeLite::Source::Reported) {
            reported[reportedCount++] = e;
        } else {
            mirrored[mirroredCount++] = e;
        }
    }

    auto sortByEtxLite = [](const EdgeLite* a, const EdgeLite* b) { return a->getEtx() < b->getEtx(); };
    std::sort(reported, reported + reportedCount, sortByEtxLite);
    std::sort(mirrored, mirrored + mirroredCount, sortByEtxLite);

    const EdgeLite* selected[MAX_SIGNAL_ROUTING_NEIGHBORS];
    size_t selectedCount = 0;
    for (uint8_t i = 0; i < reportedCount && selectedCount < MAX_SIGNAL_ROUTING_NEIGHBORS; i++) {
        selected[selectedCount++] = reported[i];
    }
    for (uint8_t i = 0; i < mirroredCount && selectedCount < MAX_SIGNAL_ROUTING_NEIGHBORS; i++) {
        selected[selectedCount++] = mirrored[i];
    }

    info.neighbors_count = selectedCount;

    for (size_t i = 0; i < selectedCount; i++) {
        const EdgeLite& edge = *selected[i];
        meshtastic_SignalNeighbor& neighbor = info.neighbors[i];

        neighbor.node_id = edge.to;
        neighbor.position_variance = edge.variance; // Already uint8, 0-255 scaled
        neighbor.signal_based_capable = isSignalBasedCapable(edge.to);

        int32_t rssi32, snr32;
        GraphLite::etxToSignal(edge.getEtx(), rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));
    }
#else
    const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!edges || edges->empty()) {
        return;
    }

    // Prefer edges with reported quality (peer perspective), then fall back to mirrored estimates
    std::vector<const Edge*> reported;
    std::vector<const Edge*> mirrored;
    reported.reserve(edges->size());
    mirrored.reserve(edges->size());
    for (const Edge& e : *edges) {
        if (e.source == Edge::Source::Reported) {
            reported.push_back(&e);
        } else {
            mirrored.push_back(&e);
        }
    }
    auto sortByEtx = [](const Edge* a, const Edge* b) { return a->etx < b->etx; };
    std::sort(reported.begin(), reported.end(), sortByEtx);
    std::sort(mirrored.begin(), mirrored.end(), sortByEtx);

    std::vector<const Edge*> selected;
    selected.reserve(edges->size());
    for (auto* e : reported) {
        selected.push_back(e);
        if (selected.size() >= MAX_SIGNAL_ROUTING_NEIGHBORS) break;
    }
    if (selected.size() < MAX_SIGNAL_ROUTING_NEIGHBORS) {
        for (auto* e : mirrored) {
            selected.push_back(e);
            if (selected.size() >= MAX_SIGNAL_ROUTING_NEIGHBORS) break;
        }
    }

    info.neighbors_count = selected.size();

    for (size_t i = 0; i < selected.size(); i++) {
        const Edge& edge = *selected[i];
        meshtastic_SignalNeighbor& neighbor = info.neighbors[i];

        neighbor.node_id = edge.to;
        // Scale variance from uint32 (0-3000) to uint8 (0-255)
        uint32_t scaledVar = edge.variance / 12;
        neighbor.position_variance = scaledVar > 255 ? 255 : static_cast<uint8_t>(scaledVar);
        neighbor.signal_based_capable = isSignalBasedCapable(edge.to);

        int32_t rssi32, snr32;
        Graph::etxToSignal(edge.etx, rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));
    }
#endif
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
    LOG_DEBUG("[SR] Pre-processing %d neighbors from %s for relay decision",
              info.neighbors_count, senderName);

    // Add edges from each neighbor TO the sender
    // The RSSI/SNR describes how well the sender hears the neighbor,
    // which characterizes the neighbor→sender transmission quality
    // Use packet rx_time since SignalNeighbor doesn't have last_rx_time
    uint32_t rxTime = p->rx_time ? p->rx_time : getTime();
    for (pb_size_t i = 0; i < info.neighbors_count; i++) {
        const meshtastic_SignalNeighbor& neighbor = info.neighbors[i];
        trackNodeCapability(neighbor.node_id,
                            neighbor.signal_based_capable ? CapabilityStatus::Capable : CapabilityStatus::Legacy);
        float etx =
#ifdef SIGNAL_ROUTING_LITE_MODE
            GraphLite::calculateETX(neighbor.rssi, neighbor.snr);
#else
            Graph::calculateETX(neighbor.rssi, neighbor.snr);
#endif
        // Scale position_variance from uint8 (0-255) back to full range (0-3000) for graph storage
        uint32_t scaledVariance = static_cast<uint32_t>(neighbor.position_variance) * 12;
        // Edge direction: neighbor → sender (the direction of the transmission that produced the RSSI)
#ifdef SIGNAL_ROUTING_LITE_MODE
        routingGraph->updateEdge(neighbor.node_id, p->from, etx, rxTime, scaledVariance,
                                 EdgeLite::Source::Reported);
        // Also mirror: sender's view of this neighbor for others to consume
        routingGraph->updateEdge(p->from, neighbor.node_id, etx, rxTime, scaledVariance,
                                 EdgeLite::Source::Mirrored);
#else
        routingGraph->updateEdge(neighbor.node_id, p->from, etx, rxTime, scaledVariance,
                                 Edge::Source::Reported);
        routingGraph->updateEdge(p->from, neighbor.node_id, etx, rxTime, scaledVariance,
                                 Edge::Source::Mirrored);
#endif
    }
}

bool SignalRoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_SignalRoutingInfo *p)
{
    // Note: Graph may have already been updated by preProcessSignalRoutingPacket()
    // This is intentional - we want up-to-date data for relay decisions
    if (!routingGraph || !p) return false;

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    CapabilityStatus newStatus = p->signal_based_capable ? CapabilityStatus::Capable : CapabilityStatus::Legacy;
    CapabilityStatus oldStatus = getCapabilityStatus(mp.from);
    trackNodeCapability(mp.from, newStatus);

    if (oldStatus != newStatus) {
        LOG_INFO("[SR] Capability update: %s changed from %d to %d",
                senderName, (int)oldStatus, (int)newStatus);
    }

    if (p->neighbors_count == 0) {
        LOG_INFO("[SR] %s is online (SR v%d, %s) - no neighbors detected yet",
                 senderName, p->routing_version,
                 p->signal_based_capable ? "SR-capable" : "legacy mode");

        // Clear gateway relationships for SR-capable nodes with no neighbors - they can't be gateways
        if (p->signal_based_capable) {
            clearGatewayRelationsFor(mp.from);
        }

        return false;
    }

    LOG_INFO("[SR] RECEIVED: %s reports %d neighbors (SR v%d, %s)",
             senderName, p->neighbors_count, p->routing_version,
             p->signal_based_capable ? "SR-capable" : "legacy mode");

    // Flash cyan for network topology update
    flashRgbLed(0, 255, 255, 150, true);

    // Clear all existing edges for this node before adding the new ones from the broadcast
    // This ensures our view of the sender's connectivity matches exactly what it reported
#ifdef SIGNAL_ROUTING_LITE_MODE
    routingGraph->clearEdgesForNode(mp.from);
#else
    routingGraph->clearEdgesForNode(mp.from);
#endif

    // Add edges from each neighbor TO the sender
    // The RSSI/SNR describes how well the sender hears the neighbor,
    // which characterizes the neighbor→sender transmission quality
    // Use packet rx_time since SignalNeighbor doesn't have last_rx_time
    // (This may be redundant if preProcessSignalRoutingPacket already ran, but it's idempotent)
    uint32_t rxTime = mp.rx_time ? mp.rx_time : getTime();
    for (pb_size_t i = 0; i < p->neighbors_count; i++) {
        const meshtastic_SignalNeighbor& neighbor = p->neighbors[i];

        char neighborName[64];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));

        trackNodeCapability(neighbor.node_id,
                            neighbor.signal_based_capable ? CapabilityStatus::Capable : CapabilityStatus::Legacy);

        // Calculate ETX from the received RSSI/SNR
        float etx =
#ifdef SIGNAL_ROUTING_LITE_MODE
            GraphLite::calculateETX(neighbor.rssi, neighbor.snr);
#else
            Graph::calculateETX(neighbor.rssi, neighbor.snr);
#endif

        // Scale position_variance from uint8 (0-255) back to full range (0-3000) for graph storage
        uint32_t scaledVariance = static_cast<uint32_t>(neighbor.position_variance) * 12;

        // Add edge: neighbor -> sender (the direction of the transmission that produced the RSSI)
#ifdef SIGNAL_ROUTING_LITE_MODE
        int edgeChange = routingGraph->updateEdge(neighbor.node_id, mp.from, etx, rxTime, scaledVariance,
                                                  EdgeLite::Source::Reported);
        routingGraph->updateEdge(mp.from, neighbor.node_id, etx, rxTime, scaledVariance,
                                 EdgeLite::Source::Mirrored);
#else
        int edgeChange = routingGraph->updateEdge(neighbor.node_id, mp.from, etx, rxTime, scaledVariance,
                                                  Edge::Source::Reported);
        routingGraph->updateEdge(mp.from, neighbor.node_id, etx, rxTime, scaledVariance,
                                 Edge::Source::Mirrored);
#endif

        // Log topology if this is a new edge or significant change
        if (edgeChange == Graph::EDGE_NEW || edgeChange == Graph::EDGE_SIGNIFICANT_CHANGE) {
            logNetworkTopology();
        }

        // Classify signal quality for user-friendly display
        const char* quality;
        if (etx < 2.0f) quality = "excellent";
        else if (etx < 4.0f) quality = "good";
        else if (etx < 8.0f) quality = "fair";
        else quality = "poor";

        LOG_INFO("  ├── %s: %s link (%s, ETX=%.1f, var=%u)",
                 neighborName,
                 neighbor.signal_based_capable ? "SR-node" : "legacy",
                 quality, etx,
                 neighbor.position_variance);

        // If the sender is SR-capable and reports this neighbor as directly reachable,
        // clear ALL gateway relationships for this neighbor - it's now reachable via the SR network
        if (p->signal_based_capable) {
            NodeNum gatewayForNeighbor = getGatewayFor(neighbor.node_id);
            if (gatewayForNeighbor != 0 && gatewayForNeighbor != mp.from) {
                char gwName[64];
                getNodeDisplayName(gatewayForNeighbor, gwName, sizeof(gwName));
                LOG_INFO("[SR] Clearing gateways for %s (now directly reachable via %s, was via %s)",
                         neighborName, senderName, gwName);
                clearDownstreamFromAllGateways(neighbor.node_id);
            }
        }
    }

    // Log network topology summary
    LOG_DEBUG("[SR] Network topology updated - %s now connected to %d neighbors",
             senderName, p->neighbors_count);

    // Allow others to see this packet too
    return false;
}

/**
 * Log the current network topology graph in a readable format
 */
void SignalRoutingModule::logNetworkTopology()
{
    if (!routingGraph) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // LITE mode: use fixed-size arrays only, no heap allocations
    NodeNum nodeBuf[GRAPH_LITE_MAX_NODES];
    size_t nodeCount = routingGraph->getAllNodeIds(nodeBuf, GRAPH_LITE_MAX_NODES);
    if (nodeCount == 0) {
        LOG_INFO("[SR] Network Topology: No nodes in graph yet");
        return;
    }
    LOG_INFO("[SR] Network Topology: %d nodes total", nodeCount);

    // Sort in place using fixed array (avoid std::vector heap allocation)
    std::sort(nodeBuf, nodeBuf + nodeCount);

    for (size_t nodeIdx = 0; nodeIdx < nodeCount; nodeIdx++) {
        NodeNum nodeId = nodeBuf[nodeIdx];
        char nodeName[48]; // Reduced buffer size for stack safety
        getNodeDisplayName(nodeId, nodeName, sizeof(nodeName));

        const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeId);
        if (!edges || edges->edgeCount == 0) {
            CapabilityStatus status = getCapabilityStatus(nodeId);
            const char* statusStr = (status == CapabilityStatus::Capable) ? "SR-capable" :
                                   (status == CapabilityStatus::Legacy) ? "legacy" : "unknown";
            LOG_INFO("[SR] +- %s: no neighbors (%s)", nodeName, statusStr);
            continue;
        }

        // Count gateway downstreams using fixed iteration (no heap allocation)
        uint8_t downstreamCount = 0;
        uint32_t now = getTime();
        for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
            const GatewayDownstreamSet &set = gatewayDownstream[i];
            if (set.gateway == nodeId && (now - set.lastSeen) <= CAPABILITY_TTL_SECS) {
                downstreamCount = set.count;
                break;
            }
        }

        if (downstreamCount == 0) {
            LOG_INFO("[SR] +- %s: connected to %d nodes", nodeName, edges->edgeCount);
        } else {
            LOG_INFO("[SR] +- %s: connected to %d nodes (gateway for %d nodes)", nodeName, edges->edgeCount, downstreamCount);
        }

        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            const EdgeLite& edge = edges->edges[i];
            char neighborName[48]; // Reduced buffer size for stack safety
            getNodeDisplayName(edge.to, neighborName, sizeof(neighborName));

            float etx = edge.getEtx();
            const char* quality;
            if (etx < 2.0f) quality = "excellent";
            else if (etx < 4.0f) quality = "good";
            else if (etx < 8.0f) quality = "fair";
            else quality = "poor";

            int32_t age = computeAgeSecs(edges->lastFullUpdate, getTime());
            char ageBuf[16];
            if (age < 0) {
                snprintf(ageBuf, sizeof(ageBuf), "-");
            } else {
                snprintf(ageBuf, sizeof(ageBuf), "%d", age);
            }

            LOG_INFO("[SR] |  +- %s: %s link (ETX=%.1f, %s sec ago)",
                    neighborName, quality, etx, ageBuf);
        }
    }

    // Add legend explaining ETX to signal quality mapping
    LOG_INFO("[SR] ETX to signal mapping: ETX=1.0~RSSI=-60dB/SNR=10dB, ETX=2.0~RSSI=-90dB/SNR=0dB, ETX=4.0~RSSI=-110dB/SNR=-5dB");
    LOG_DEBUG("[SR] Topology logging complete");

#else
    // Full mode: use dynamic allocations (std::vector) for flexibility
    auto appendGatewayDownstreams = [&](NodeNum gateway, std::vector<NodeNum> &out) {
        auto it = gatewayDownstream.find(gateway);
        if (it == gatewayDownstream.end()) return;
        out.insert(out.end(), it->second.begin(), it->second.end());
    };

    auto allNodes = routingGraph->getAllNodes();
    if (allNodes.empty()) {
        LOG_INFO("[SR] Network Topology: No nodes in graph yet");
        return;
    }
    LOG_INFO("[SR] Network Topology: %d nodes total", allNodes.size());
    // Sort nodes for consistent output
    std::vector<NodeNum> sortedNodes(allNodes.begin(), allNodes.end());
    std::sort(sortedNodes.begin(), sortedNodes.end());

    for (NodeNum nodeId : sortedNodes) {
        char nodeName[64];
        getNodeDisplayName(nodeId, nodeName, sizeof(nodeName));

        const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeId);
        if (!edges || edges->empty()) {
            CapabilityStatus status = getCapabilityStatus(nodeId);
            const char* statusStr = (status == CapabilityStatus::Capable) ? "SR-capable" :
                                   (status == CapabilityStatus::Legacy) ? "legacy" : "unknown";
            LOG_INFO("[SR] +- %s: no neighbors (%s)", nodeName, statusStr);
            continue;
        }

        std::vector<NodeNum> downstreams;
        appendGatewayDownstreams(nodeId, downstreams);

        if (downstreams.empty()) {
            LOG_INFO("[SR] +- %s: connected to %d nodes", nodeName, edges->size());
        } else {
            std::sort(downstreams.begin(), downstreams.end());
            downstreams.erase(std::unique(downstreams.begin(), downstreams.end()), downstreams.end());
            char buf[128];
            size_t pos = 0;
            size_t maxList = std::min<size_t>(downstreams.size(), 4);
            for (size_t i = 0; i < maxList; i++) {
                char dn[32];
                getNodeDisplayName(downstreams[i], dn, sizeof(dn));
                int written = snprintf(buf + pos, sizeof(buf) - pos, (i == 0) ? "%s" : ", %s", dn);
                if (written < 0 || (size_t)written >= (sizeof(buf) - pos)) {
                    buf[sizeof(buf) - 1] = '\0';
                    break;
                }
                pos += static_cast<size_t>(written);
            }
            if (downstreams.size() > maxList && pos < sizeof(buf) - 6) {
                snprintf(buf + pos, sizeof(buf) - pos, ", +%zu", downstreams.size() - maxList);
            }
            LOG_INFO("[SR] +- %s: connected to %d nodes (gateway for %zu nodes: %s)", nodeName, edges->size(), downstreams.size(), buf);
        }

        // Sort edges by ETX for consistent output
        std::vector<Edge> sortedEdges = *edges;
        std::sort(sortedEdges.begin(), sortedEdges.end(),
                 [](const Edge& a, const Edge& b) { return a.etx < b.etx; });

        for (size_t i = 0; i < sortedEdges.size(); i++) {
            const Edge& edge = sortedEdges[i];
            char neighborName[64];
            getNodeDisplayName(edge.to, neighborName, sizeof(neighborName));

            const char* quality;
            if (edge.etx < 2.0f) quality = "excellent";
            else if (edge.etx < 4.0f) quality = "good";
            else if (edge.etx < 8.0f) quality = "fair";
            else quality = "poor";

            int32_t age = computeAgeSecs(edge.lastUpdate, getTime());
            char ageBuf[16];
            if (age < 0) {
                snprintf(ageBuf, sizeof(ageBuf), "-");
            } else {
                snprintf(ageBuf, sizeof(ageBuf), "%d", age);
            }

            LOG_INFO("[SR] |  +- %s: %s link (ETX=%.1f, %s sec ago)",
                    neighborName, quality, edge.etx, ageBuf);
        }
    }

    // Add legend explaining ETX to signal quality mapping
    LOG_INFO("[SR] ETX to signal mapping: ETX=1.0~RSSI=-60dB/SNR=10dB, ETX=2.0~RSSI=-90dB/SNR=0dB, ETX=4.0~RSSI=-110dB/SNR=-5dB");
    LOG_DEBUG("[SR] Topology logging complete");
#endif
}

ProcessMessage SignalRoutingModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Update NodeDB with packet information like FloodingRouter does
    if (nodeDB) {
        nodeDB->updateFrom(mp);
    }

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
    if (hasSignalData && notViaMqtt) {
        LOG_DEBUG("[SR] Packet from 0x%08x: relay=0x%02x, fromLastByte=0x%02x, direct=%d",
                  mp.from, mp.relay_node, fromLastByte, isDirectFromSender);
        if (!isDirectFromSender && mp.relay_node != 0) {
            LOG_DEBUG("[SR] Relayed packet detected - relay node presence will be updated via inferred relayer");
        }
    }
    
    if (hasSignalData && notViaMqtt && isDirectFromSender) {
        rememberRelayIdentity(mp.from, fromLastByte);
        trackNodeCapability(mp.from, CapabilityStatus::Unknown);

        char senderName[64];
        getNodeDisplayName(mp.from, senderName, sizeof(senderName));

        float etx =
#ifdef SIGNAL_ROUTING_LITE_MODE
            GraphLite::calculateETX(mp.rx_rssi, mp.rx_snr);
#else
            Graph::calculateETX(mp.rx_rssi, mp.rx_snr);
#endif
        LOG_INFO("[SR] Direct neighbor %s: RSSI=%d, SNR=%.1f, ETX=%.2f",
                 senderName, mp.rx_rssi, mp.rx_snr, etx);

        // Remove this node from ALL gateway relationships since we can hear it directly
        clearDownstreamFromAllGateways(mp.from);

        // Brief purple flash for any direct packet received
        flashRgbLed(128, 0, 128, 100, true);

        // Record that this node transmitted (for contention window tracking)
        if (routingGraph) {
            uint32_t currentTime = getValidTime(RTCQualityFromNet);
            if (!currentTime) {
                currentTime = getTime();
            }
            routingGraph->recordNodeTransmission(mp.from, mp.id, currentTime);
        }

        // Note: rx_time is already Unix epoch seconds from getValidTime()
        updateNeighborInfo(mp.from, mp.rx_rssi, mp.rx_snr, mp.rx_time);
        LOG_DEBUG("[SR] Direct neighbor %s detected (RSSI=%d, SNR=%.1f)",
                 senderName, mp.rx_rssi, mp.rx_snr);
    } else if (notViaMqtt && !isDirectFromSender && mp.relay_node != 0) {
        // Process relayed packets to infer network topology
        // We don't have direct signal info to the original sender, but we can infer connectivity
        NodeNum inferredRelayer = resolveRelayIdentity(mp.relay_node);

        if (inferredRelayer != 0 && inferredRelayer != mp.from) {
            // We know that inferredRelayer relayed a packet from mp.from
            // This suggests connectivity between mp.from and inferredRelayer
            LOG_DEBUG("[SR] Inferred connectivity: %08x -> %08x (relayed via %02x)",
                     mp.from, inferredRelayer, mp.relay_node);

            // Track that both the original sender and relayer are active
            trackNodeCapability(mp.from, CapabilityStatus::Unknown);
            trackNodeCapability(inferredRelayer, CapabilityStatus::Unknown);

            // Relay node is actively participating, tracked via SR capability system

            // Record gateway relationship: inferredRelayer is gateway for mp.from
            // But only if we don't have a direct connection to mp.from ourselves
            bool hasDirectConnection = false;
#ifdef SIGNAL_ROUTING_LITE_MODE
            const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (edges) {
                for (uint8_t i = 0; i < edges->edgeCount; i++) {
                    if (edges->edges[i].to == mp.from) {
                        hasDirectConnection = true;
                        break;
                    }
                }
            }
#else
            const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (edges) {
                for (const Edge& e : *edges) {
                    if (e.to == mp.from) {
                        hasDirectConnection = true;
                        break;
                    }
                }
            }
#endif
            if (!hasDirectConnection) {
                recordGatewayRelation(inferredRelayer, mp.from);
            }

            // Update relay node's edge in the graph since it's actively relaying
            if (hasSignalData) {
                updateNeighborInfo(inferredRelayer, mp.rx_rssi, mp.rx_snr, mp.rx_time);
            } else {
                // No direct signal data available - preserve existing edge or create with defaults
#ifdef SIGNAL_ROUTING_LITE_MODE
                const NodeEdgesLite *relayEdges = routingGraph->getEdgesFrom(inferredRelayer);
                bool hasExistingEdge = false;
                int32_t existingRssi = -70; // default
                int32_t existingSnr = 5;   // default

                if (relayEdges) {
                    for (uint8_t i = 0; i < relayEdges->edgeCount; i++) {
                        if (relayEdges->edges[i].to == nodeDB->getNodeNum()) {
                            // Found existing edge - preserve its signal data by recalculating backwards
                            float existingEtx = relayEdges->edges[i].getEtx();
                            int32_t approxRssi;
                            GraphLite::etxToSignal(existingEtx, approxRssi, existingSnr);
                            existingRssi = approxRssi;
                            hasExistingEdge = true;
                            break;
                        }
                    }
                }

                if (hasExistingEdge) {
                    LOG_DEBUG("[SR] Preserving existing signal data for relay node 0x%08x", inferredRelayer);
                } else {
                    LOG_DEBUG("[SR] Using default signal data for new relay node 0x%08x", inferredRelayer);
                }

                updateNeighborInfo(inferredRelayer, existingRssi, existingSnr, mp.rx_time);
#else
                // Non-lite mode: simpler approach - just use defaults since we can't easily query existing edges
                updateNeighborInfo(inferredRelayer, -70, 5, mp.rx_time);
#endif
            }

            // Record transmission for contention window tracking
            if (routingGraph) {
                uint32_t currentTime = getValidTime(RTCQualityFromNet);
                if (!currentTime) {
                    currentTime = getTime();
                }
                routingGraph->recordNodeTransmission(mp.from, mp.id, currentTime);
                routingGraph->recordNodeTransmission(inferredRelayer, mp.id, currentTime);
            }
        }
    }

    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        handleSniffedPayload(mp, isDirectFromSender);
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
            LOG_DEBUG("[SR] Aged edges");
        }
    }

    return ProcessMessage::CONTINUE;
}

bool SignalRoutingModule::shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p)
{
    if (!p || !signalBasedRoutingEnabled || !routingGraph || !nodeDB) {
        LOG_DEBUG("[SR] SR disabled or unavailable (enabled=%d, graph=%p, nodeDB=%p)",
                 signalBasedRoutingEnabled, routingGraph, nodeDB);
        return false;
    }

    // If the packet wasn't decrypted, still consider SR but note we are routing opaque payload.
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        LOG_INFO("[SR] Packet not decoded (hash/PSK mismatch) - routing header only");
    }

    // Use smaller buffers to reduce stack pressure on memory-constrained devices
    char destName[40], senderName[40];
    getNodeDisplayName(p->to, destName, sizeof(destName));
    getNodeDisplayName(p->from, senderName, sizeof(senderName));

    if (isBroadcast(p->to)) {
        LOG_DEBUG("[SR] Considering broadcast from %s to %s (hop_limit=%d)",
                 senderName, destName, p->hop_limit);

        if (!isActiveRoutingRole()) {
            LOG_DEBUG("[SR] Passive role - entering SR path for relay veto");
            return true; // enter SR path so shouldRelayBroadcast can veto the relay
        }

        bool healthy = topologyHealthyForBroadcast();
        LOG_DEBUG("[SR] Calculating neighborCount");
        size_t neighborCount = 0;
        if (routingGraph && nodeDB) {
#ifdef SIGNAL_ROUTING_LITE_MODE
            const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (edges) neighborCount = edges->edgeCount;
#else
            auto edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (edges) neighborCount = edges->size();
#endif
        }
        LOG_INFO("[SR] Topology check: %s (%d direct neighbors, %.1f%% capable)",
                 healthy ? "HEALTHY - SR active" : "UNHEALTHY - flooding only",
                 neighborCount, getSignalBasedCapablePercentage());

        if (!healthy && neighborCount > 0) {
            LOG_INFO("[SR] SR not activated despite having neighbors - checking capability status");
#ifndef SIGNAL_ROUTING_LITE_MODE
            if (routingGraph) {
                const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
                if (edges) {
                    for (const Edge& edge : *edges) {
                        CapabilityStatus status = getCapabilityStatus(edge.to);
                        char neighborName[64];
                        getNodeDisplayName(edge.to, neighborName, sizeof(neighborName));
                        LOG_INFO("[SR] Neighbor %s: status=%s", neighborName,
                                status == CapabilityStatus::Capable ? "SR-capable" :
                                status == CapabilityStatus::Legacy ? "legacy" : "unknown");
                    }
                }
            }
#endif
        }
        return healthy;
    }

    // Unicast routing
    LOG_DEBUG("[SR] Considering unicast from %s to %s (hop_limit=%d)",
             senderName, destName, p->hop_limit);

    // Don't use SR for packets addressed to us - let them be delivered normally
    if (p->to == nodeDB->getNodeNum()) {
        LOG_DEBUG("[SR] Packet addressed to local node - not using SR");
        return false;
    }

    if (!isActiveRoutingRole()) {
        LOG_DEBUG("[SR] Passive role - not using SR for unicast");
        return false;
    }

    bool topologyHealthy = topologyHealthyForUnicast(p->to);
    LOG_DEBUG("[SR] Unicast topology %s for destination",
             topologyHealthy ? "HEALTHY" : "unhealthy");

    if (!topologyHealthy) {
        LOG_DEBUG("[SR] Insufficient SR-capable nodes for reliable unicast - using Graph routing with contention window");

        // Use the Graph's shouldRelay logic which has built-in contention window support
        // This provides the same redundancy and coordination logic as regular flooding
        if (routingGraph) {
            NodeNum myNode = nodeDB->getNodeNum();
            NodeNum sourceNode = p->from;
            NodeNum heardFrom = resolveHeardFrom(p, sourceNode);

            uint32_t currentTime = getValidTime(RTCQualityFromNet);
            if (!currentTime) {
                currentTime = getTime();
            }

#ifdef SIGNAL_ROUTING_LITE_MODE
            bool shouldRelay = routingGraph->shouldRelayWithContention(myNode, sourceNode, heardFrom, p->id, currentTime);
#else
            bool shouldRelay = routingGraph->shouldRelayEnhanced(myNode, sourceNode, heardFrom, currentTime, p->id);
#endif
            LOG_DEBUG("[SR] Graph routing decision: %s", shouldRelay ? "SHOULD relay" : "should NOT relay");

            if (!shouldRelay && router) {
                // Cancel any pending transmission that traditional routing might have queued
                router->cancelSending(p->from, p->id);
            }

            return shouldRelay;
        }

        // No routing graph available, fall back to flooding
        return false;
    }

    bool destCapable = isSignalBasedCapable(p->to);
    bool destLegacy = isLegacyRouter(p->to);
    LOG_DEBUG("[SR] Destination %s (SR-capable=%d, legacy-router=%d)",
             destName, destCapable, destLegacy);

    NodeNum sourceNode = p->from;
    NodeNum heardFrom = resolveHeardFrom(p, sourceNode);
    // For unicast with healthy topology, don't allow opportunistic forwarding
    // Only allow opportunistic forwarding when topology is unhealthy
    NodeNum nextHop = getNextHop(p->to, sourceNode, heardFrom, !topologyHealthy);
    if (nextHop == 0) {
        // For unicast packets where topology is healthy (destination exists),
        // don't relay if we can't find a route. Assume other nodes will handle it.
        // Only do opportunistic forwarding for broadcast or when topology is unhealthy.

        // Check if another node is the designated gateway for this destination
        NodeNum designatedGateway = getGatewayFor(p->to);
        if (designatedGateway != 0 && designatedGateway != nodeDB->getNodeNum()) {
            char gwName[64];
            getNodeDisplayName(designatedGateway, gwName, sizeof(gwName));
            LOG_INFO("[SR] Not relaying to %s - %s is the designated gateway", destName, gwName);

            // Cancel any pending transmission that traditional routing might have queued
            if (router) {
                router->cancelSending(p->from, p->id);
            }

            return false; // Don't use SR - designated gateway should handle this packet
        } else {
            LOG_DEBUG("[SR] No route found to destination - allowing traditional routing to attempt delivery");
            return false; // Don't use SR - allow traditional routing/flooding for this unicast packet
        }
    }

    // Gateway preference: if we know the destination is behind a gateway we can reach directly, prefer that
    // But only if we don't already have a direct route to the destination
    if (nextHop != p->to) {  // Only use gateway if we don't have a direct route
        NodeNum gatewayForDest = getGatewayFor(p->to);
        if (gatewayForDest && gatewayForDest != nextHop) {
            bool directToGateway = false;
#ifdef SIGNAL_ROUTING_LITE_MODE
            const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (edges) {
                for (uint8_t i = 0; i < edges->edgeCount; i++) {
                    if (edges->edges[i].to == gatewayForDest) {
                        directToGateway = true;
                        break;
                    }
                }
            }
#else
            const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (edges) {
                for (const Edge& e : *edges) {
                    if (e.to == gatewayForDest) {
                        directToGateway = true;
                        break;
                    }
                }
            }
#endif
            if (directToGateway) {
                LOG_INFO("[SR] Gateway preference: using gateway %08x to reach %08x (was %08x)", gatewayForDest, p->to, nextHop);
                nextHop = gatewayForDest;
            }
        }
    }

    char nextHopName[64];
    getNodeDisplayName(nextHop, nextHopName, sizeof(nextHopName));

    bool nextHopCapable = isSignalBasedCapable(nextHop);
    bool nextHopLegacy = isLegacyRouter(nextHop);
    LOG_DEBUG("[SR] Next hop %s (SR-capable=%d, legacy-router=%d)",
             nextHopName, nextHopCapable, nextHopLegacy);

    if (!nextHopCapable && !nextHopLegacy) {
        LOG_DEBUG("[SR] Next hop not SR-capable and not legacy router - fallback to flood");
        return false;
    }

    LOG_INFO("[SR] Using SR for unicast from %s to %s via %s",
             senderName, destName, nextHopName);
    return true;
}

bool SignalRoutingModule::shouldRelayBroadcast(const meshtastic_MeshPacket *p)
{
    if (!routingGraph || !nodeDB || !isBroadcast(p->to)) {
        return true;
    }

    if (!isActiveRoutingRole()) {
        return false;
    }

    if (!topologyHealthyForBroadcast()) {
        return true;
    }

    // Only access decoded fields if packet is actually decoded
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        p->decoded.portnum == meshtastic_PortNum_SIGNAL_ROUTING_APP) {
        preProcessSignalRoutingPacket(p);
    }

    NodeNum myNode = nodeDB->getNodeNum();
    NodeNum sourceNode = p->from;
    NodeNum heardFrom = resolveHeardFrom(p, sourceNode);

    // Gateway awareness: only force relay if WE are the recorded gateway for the source
    NodeNum gatewayForSource = getGatewayFor(sourceNode);
    bool weAreGateway = (gatewayForSource != 0 && gatewayForSource == myNode);
    size_t downstreamCount = weAreGateway ? getGatewayDownstreamCount(myNode) : 0;

    uint32_t currentTime = getValidTime(RTCQualityFromNet);
    if (!currentTime) {
        currentTime = getTime();
    }

    // Check for stock gateway nodes that can be heard directly
    // If we have stock nodes that could serve as gateways, be conservative with SR relaying
    bool hasStockGateways = false;
    bool heardFromStockGateway = false;
    if (routingGraph && nodeDB) {
#ifdef SIGNAL_ROUTING_LITE_MODE
        // In lite mode, check capability records for legacy nodes
        for (uint8_t i = 0; i < capabilityRecordCount; i++) {
            if (capabilityRecords[i].record.status == CapabilityStatus::Legacy) {
                hasStockGateways = true;
                if (capabilityRecords[i].nodeId == heardFrom) {
                    heardFromStockGateway = true;
                }
            }
        }
#else
        // In full mode, check capability status of nodes we've heard from recently
        for (const auto& pair : capabilityRecords) {
            if (pair.second.status == CapabilityStatus::Legacy) {
                hasStockGateways = true;
                if (pair.first == heardFrom) {
                    heardFromStockGateway = true;
                }
            }
        }
#endif
    }

    // Key insight: If packet comes from a stock gateway, we MUST relay it within the branch
    // to ensure all local nodes receive packets from outside the branch
    bool mustRelayForBranchCoverage = heardFromStockGateway;

    if (heardFromStockGateway) {
        LOG_DEBUG("[SR] Packet from stock gateway %08x - prioritizing branch distribution", heardFrom);
    }

#ifdef SIGNAL_ROUTING_LITE_MODE
    bool shouldRelay = routingGraph->shouldRelaySimple(myNode, sourceNode, heardFrom, currentTime);

    // Apply conservative logic only when NOT required for branch coverage
    if (shouldRelay && hasStockGateways && !mustRelayForBranchCoverage) {
        LOG_DEBUG("[SR] Applying conservative relay logic (stock gateways present, not from gateway)");
        shouldRelay = routingGraph->shouldRelaySimpleConservative(myNode, sourceNode, heardFrom, currentTime);
        if (!shouldRelay) {
            LOG_DEBUG("[SR] Suppressed SR relay - stock gateway can handle external transmission");
        } else {
            LOG_DEBUG("[SR] SR relay proceeding despite conservative logic");
        }
    }
#else
    bool shouldRelay = routingGraph->shouldRelayEnhanced(myNode, sourceNode, heardFrom, currentTime, p->id);

    // Apply conservative logic only when NOT required for branch coverage
    if (shouldRelay && hasStockGateways && !mustRelayForBranchCoverage) {
        LOG_DEBUG("[SR] Applying conservative relay logic (stock gateways present, not from gateway)");
        shouldRelay = routingGraph->shouldRelayEnhancedConservative(myNode, sourceNode, heardFrom, currentTime, p->id);
        if (!shouldRelay) {
            LOG_DEBUG("[SR] Suppressed SR relay - stock gateway provides better external coverage");
        } else {
            LOG_DEBUG("[SR] SR relay proceeding despite conservative logic");
        }
    }
#endif

    if (!shouldRelay && weAreGateway) {
        LOG_INFO("[SR] We are gateway for %08x (downstream=%zu) -> force relay", sourceNode, downstreamCount);
        shouldRelay = true;
    }

    char myName[64], sourceName[64], heardFromName[64];
    getNodeDisplayName(myNode, myName, sizeof(myName));
    getNodeDisplayName(sourceNode, sourceName, sizeof(sourceName));
    getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));

    LOG_INFO("[SR] Broadcast from %s (heard via %s): %s relay",
             sourceName, heardFromName, shouldRelay ? "SHOULD" : "should NOT");

    if (shouldRelay) {
        routingGraph->recordNodeTransmission(myNode, p->id, currentTime);
        flashRgbLed(255, 128, 0, 150, true);
    } else {
        flashRgbLed(255, 0, 0, 100, true);
    }

    return shouldRelay;
}

NodeNum SignalRoutingModule::getNextHop(NodeNum destination, NodeNum sourceNode, NodeNum heardFrom, bool allowOpportunistic)
{
    if (!routingGraph) {
        LOG_DEBUG("[SR] No graph available for routing");
        return 0;
    }

    uint32_t currentTime = getValidTime(RTCQualityFromNet);
    if (!currentTime) {
        currentTime = getTime();
    }

    char destName[64];
    getNodeDisplayName(destination, destName, sizeof(destName));

#ifdef SIGNAL_ROUTING_LITE_MODE
    RouteLite route = routingGraph->calculateRoute(destination, currentTime);
    float routeCost = route.getCost();
#else
    Route route = routingGraph->calculateRoute(destination, currentTime);
    float routeCost = route.cost;
#endif

    if (route.nextHop != 0) {
        char nextHopName[64];
        getNodeDisplayName(route.nextHop, nextHopName, sizeof(nextHopName));

        LOG_DEBUG("[SR] Route to %s via %s (cost: %.2f)",
                 destName, nextHopName, routeCost);

        if (routeCost > 10.0f) {
            LOG_WARN("[SR] High-cost route to %s (%.2f) - poor link quality expected",
                    destName, routeCost);
        }

        return route.nextHop;
    }

    // Fallback 1: if we know a gateway for this destination, and we have a direct link to it, forward there
    NodeNum gatewayForDest = getGatewayFor(destination);
    if (gatewayForDest != 0 && nodeDB) {
#ifdef SIGNAL_ROUTING_LITE_MODE
        const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
        if (myEdges) {
            for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                if (myEdges->edges[i].to == gatewayForDest) {
                    char gwName[64];
                    getNodeDisplayName(gatewayForDest, gwName, sizeof(gwName));
                    LOG_DEBUG("[SR] No direct route to %s, but forwarding to gateway %s", destName, gwName);
                    return gatewayForDest;
                }
            }
        }
#else
        auto edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
        if (edges) {
            for (const Edge& e : *edges) {
                if (e.to == gatewayForDest) {
                    char gwName[64];
                    getNodeDisplayName(gatewayForDest, gwName, sizeof(gwName));
                    LOG_DEBUG("[SR] No direct route to %s, but forwarding to gateway %s", destName, gwName);
                    return gatewayForDest;
                }
            }
        }
#endif
    }

    // Fallback 2: opportunistic forward like broadcast — pick best direct neighbor (lowest ETX) to move the packet
    // Only do this if opportunistic forwarding is allowed
    NodeNum bestNeighbor = 0;
    if (allowOpportunistic) {
        float bestEtx = 1e9f;
#ifdef SIGNAL_ROUTING_LITE_MODE
        const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
        if (myEdges) {
            for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                NodeNum neighbor = myEdges->edges[i].to;
                // Don't forward back to source or heardFrom nodes
                if (neighbor == sourceNode || neighbor == heardFrom) {
                    continue;
                }
                float etx = myEdges->edges[i].getEtx();
                if (etx < bestEtx) {
                    bestEtx = etx;
                    bestNeighbor = neighbor;
                }
            }
        }
#else
        auto edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
        if (edges) {
            for (const Edge& e : *edges) {
                NodeNum neighbor = e.to;
                // Don't forward back to source or heardFrom nodes
                if (neighbor == sourceNode || neighbor == heardFrom) {
                    continue;
                }
                if (e.etx < bestEtx) {
                    bestEtx = e.etx;
                    bestNeighbor = neighbor;
                }
            }
        }
#endif

        if (bestNeighbor != 0) {
            char nhName[64];
            getNodeDisplayName(bestNeighbor, nhName, sizeof(nhName));
            LOG_DEBUG("[SR] No route to %s; forwarding opportunistically via %s (ETX=%.2f)", destName, nhName, bestEtx);
            return bestNeighbor;
        }
    }

    // Fallback 3: if we are recorded as a gateway for this destination, we can deliver directly
    // This handles true gateway scenarios where we have unique connectivity that other SR nodes don't
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    if (myNode != 0 && getGatewayFor(destination) == myNode) {
        LOG_INFO("[SR] We are the designated gateway for %s - delivering directly", destName);
        // Refresh the gateway relationship since we're actively using it
        recordGatewayRelation(myNode, destination);
        return destination; // We are the gateway, deliver directly
    }

    // Fallback 4: if the destination only has us as a neighbor (effective gateway scenario),
    // we should try to deliver directly even without formal gateway designation
    // This handles cases like FMC6 where a node only connects through us
#ifdef SIGNAL_ROUTING_LITE_MODE
    if (routingGraph && nodeDB && myNode != 0) {
        const NodeEdgesLite *destEdges = routingGraph->getEdgesFrom(destination);
        if (destEdges && destEdges->edgeCount == 1 && destEdges->edges[0].to == myNode) {
            LOG_INFO("[SR] %s only connects through us (effective gateway) - delivering directly", destName);
            // Record ourselves as gateway for this destination since we're the only connection
            recordGatewayRelation(myNode, destination);
            return destination; // We are the effective gateway, deliver directly
        }
    }
#endif

    LOG_DEBUG("[SR] No route found to %s", destName);
    return 0;
}

void SignalRoutingModule::updateNeighborInfo(NodeNum nodeId, int32_t rssi, float snr, uint32_t lastRxTime, uint32_t variance)
{
    if (!routingGraph || !nodeDB) return;

    NodeNum myNode = nodeDB->getNodeNum();

    // Calculate ETX from the received signal quality
    // The RSSI/SNR describes how well we received from nodeId,
    // which characterizes the nodeId→us transmission quality
    float etx =
#ifdef SIGNAL_ROUTING_LITE_MODE
        GraphLite::calculateETX(rssi, snr);
#else
        Graph::calculateETX(rssi, snr);
#endif

    // Store edge: nodeId → us (the direction of the transmission we measured)
    // This is used for routing decisions when traffic needs to reach us
    int changeType =
#ifdef SIGNAL_ROUTING_LITE_MODE
        routingGraph->updateEdge(nodeId, myNode, etx, lastRxTime, variance, EdgeLite::Source::Reported);
#else
        routingGraph->updateEdge(nodeId, myNode, etx, lastRxTime, variance, Edge::Source::Reported);
#endif

    // Also store reverse edge: us → nodeId (assuming approximately symmetric link) if we don't yet have a better
    // (reported) estimate of how nodeId hears us. This serves as a fallback until we receive their SR info.
    routingGraph->updateEdge(myNode, nodeId, etx, lastRxTime, variance
#ifndef SIGNAL_ROUTING_LITE_MODE
                             , Edge::Source::Mirrored
#else
                             , EdgeLite::Source::Mirrored
#endif
                             );

    // If significant change, consider sending an update sooner
    if (changeType != Graph::EDGE_NO_CHANGE) {
        char neighborName[64];
        getNodeDisplayName(nodeId, neighborName, sizeof(neighborName));

        if (changeType == Graph::EDGE_NEW) {
            LOG_INFO("[SR] New neighbor %s detected", neighborName);
            // Flash green for new neighbor
            flashRgbLed(0, 255, 0, 300, true);
            // Log topology for new connections
            LOG_INFO("[SR] Topology changed: new neighbor %s", neighborName);
            logNetworkTopology();
        } else if (changeType == Graph::EDGE_SIGNIFICANT_CHANGE) {
            LOG_INFO("[SR] Topology changed: ETX change for %s", neighborName);
            // Flash blue for signal quality change
            flashRgbLed(0, 0, 255, 300, true);
            logNetworkTopology();
        }

        // Trigger early broadcast if we haven't sent recently (rate limit: 60s)
        uint32_t now = millis();
        if (now - lastBroadcast > 60 * 1000) {
            setIntervalFromNow(EARLY_BROADCAST_DELAY_MS); // Send update soon (configurable)
        }
    }
}

void SignalRoutingModule::handleSpeculativeRetransmit(const meshtastic_MeshPacket *p)
{
    if (!p || !signalBasedRoutingEnabled || !routingGraph) {
        return;
    }

    if (!isActiveRoutingRole()) {
        return;
    }

    if (isBroadcast(p->to) || p->from != nodeDB->getNodeNum() || p->id == 0) {
        return;
    }

    if (!shouldUseSignalBasedRouting(p)) {
        return;
    }

    uint64_t key = makeSpeculativeKey(p->from, p->id);

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Check if already exists
    for (uint8_t i = 0; i < speculativeRetransmitCount; i++) {
        if (speculativeRetransmits[i].key == key) {
            return;
        }
    }

    if (speculativeRetransmitCount >= MAX_SPECULATIVE_RETRANSMITS) {
        return; // No room
    }

    meshtastic_MeshPacket *copy = packetPool.allocCopy(*p);
    if (!copy) {
        return;
    }

    SpeculativeRetransmitEntry &entry = speculativeRetransmits[speculativeRetransmitCount++];
    entry.key = key;
    entry.origin = p->from;
    entry.packetId = p->id;
    entry.expiryMs = millis() + SPECULATIVE_RETRANSMIT_TIMEOUT_MS;
    entry.packetCopy = copy;
#else
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
#endif

    LOG_DEBUG("[SR] Speculative retransmit armed for packet %08x (expires in %ums)", p->id,
              SPECULATIVE_RETRANSMIT_TIMEOUT_MS);
}

bool SignalRoutingModule::isSignalBasedCapable(NodeNum nodeId) const
{
    if (!nodeDB) {
        return false;
    }
    if (nodeId == nodeDB->getNodeNum()) {
        return isActiveRoutingRole();
    }

    CapabilityStatus status = getCapabilityStatus(nodeId);
    return status == CapabilityStatus::Capable;
}

float SignalRoutingModule::getSignalBasedCapablePercentage() const
{
    if (!nodeDB) {
        return 0.0f;
    }

    uint32_t now = getTime();
    size_t total = 1;   // include ourselves
    size_t capable = 1; // we are always capable

    size_t nodeCount = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < nodeCount; ++i) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == nodeDB->getNodeNum()) {
            continue;
        }
        if (node->last_heard == 0 || (now - (node->last_heard / 1000)) > CAPABILITY_TTL_SECS) {
            continue;
        }
        total++;
        if (getCapabilityStatus(node->num) == CapabilityStatus::Capable) {
            capable++;
        }
    }

    float percentage = (static_cast<float>(capable) * 100.0f) / static_cast<float>(total);
    LOG_DEBUG("[SR] Capability calculation: %d/%d = %.1f%%", capable, total, percentage);
    return percentage;
}

/**
 * Flash RGB LED for Signal Routing notifications
 * Colors: Green = new neighbor, Blue = signal change, Cyan = topology update
 */
void SignalRoutingModule::flashRgbLed(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms, bool isNotification)
{
#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    uint32_t now = millis();

    if (isNotification && (now - lastEventFlashTime) < MIN_EVENT_FLASH_INTERVAL_MS) {
        return;
    }

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
    if (isNotification) {
        lastEventFlashTime = now;
    }
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

void SignalRoutingModule::handleSniffedPayload(const meshtastic_MeshPacket &mp, bool isDirectNeighbor)
{
    switch (mp.decoded.portnum) {
    case meshtastic_PortNum_NODEINFO_APP:
        handleNodeInfoPacket(mp);
        break;
    case meshtastic_PortNum_POSITION_APP:
        handlePositionPacket(mp, isDirectNeighbor);
        break;
    case meshtastic_PortNum_TELEMETRY_APP:
        handleTelemetryPacket(mp);
        break;
    case meshtastic_PortNum_ROUTING_APP:
        handleRoutingControlPacket(mp);
        break;
    default:
        break;
    }
}

void SignalRoutingModule::handlePositionPacket(const meshtastic_MeshPacket &mp, bool isDirectNeighbor)
{
    meshtastic_Position position = meshtastic_Position_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &position)) {
        return;
    }

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    double latitude = position.has_latitude_i ? position.latitude_i / 1e7 : 0.0;
    double longitude = position.has_longitude_i ? position.longitude_i / 1e7 : 0.0;
    uint32_t dop = position.PDOP;
    uint32_t speed = position.has_ground_speed ? position.ground_speed : 0;

    LOG_DEBUG("[SR] Position packet from %s (direct=%s) lat=%.5f lon=%.5f speed=%u m/s PDOP=%u "
              "rssi=%d snr=%.1f",
              senderName, isDirectNeighbor ? "true" : "false", latitude, longitude, speed, dop, mp.rx_rssi, mp.rx_snr);

    if (isDirectNeighbor && mp.rx_rssi != 0) {
        uint32_t variance = 0;
        if (position.gps_accuracy && position.PDOP) {
            uint32_t dopFactor = std::max<uint32_t>(1, position.PDOP / 100);
            variance = std::min<uint32_t>(3000, (position.gps_accuracy / 1000) * dopFactor);
        } else if (position.has_ground_speed && position.ground_speed) {
            variance = std::min<uint32_t>(3000, position.ground_speed * 5);
        }

        if (variance > 0) {
            updateNeighborInfo(mp.from, mp.rx_rssi, mp.rx_snr, mp.rx_time, variance);
        }
    }
}

void SignalRoutingModule::handleTelemetryPacket(const meshtastic_MeshPacket &mp)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Telemetry_msg, &telemetry)) {
        return;
    }

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    switch (telemetry.which_variant) {
    case meshtastic_Telemetry_device_metrics_tag: {
        const meshtastic_DeviceMetrics &metrics = telemetry.variant.device_metrics;
        int battery = metrics.has_battery_level ? static_cast<int>(metrics.battery_level) : 0;
        float voltage = metrics.has_voltage ? metrics.voltage : 0.0f;
        float air = metrics.has_air_util_tx ? metrics.air_util_tx : 0.0f;
        LOG_DEBUG("[SR] Device metrics from %s batt=%s%d%% volt=%s%.2fV airUtil=%s%.1f%%",
                  senderName, metrics.has_battery_level ? "" : "~", battery, metrics.has_voltage ? "" : "~", voltage,
                  metrics.has_air_util_tx ? "" : "~", air);
        break;
    }
    case meshtastic_Telemetry_environment_metrics_tag: {
        const meshtastic_EnvironmentMetrics &env = telemetry.variant.environment_metrics;
        LOG_DEBUG("[SR] Environment metrics from %s temp=%s%.1fC humidity=%s%.1f%% pressure=%s%.1fhPa",
                  senderName, env.has_temperature ? "" : "~",
                  env.has_temperature ? env.temperature : 0.0f, env.has_relative_humidity ? "" : "~",
                  env.has_relative_humidity ? env.relative_humidity : 0.0f, env.has_barometric_pressure ? "" : "~",
                  env.has_barometric_pressure ? env.barometric_pressure : 0.0f);
        break;
    }
    case meshtastic_Telemetry_air_quality_metrics_tag:
    case meshtastic_Telemetry_power_metrics_tag:
    case meshtastic_Telemetry_local_stats_tag:
    case meshtastic_Telemetry_health_metrics_tag:
    case meshtastic_Telemetry_host_metrics_tag:
        LOG_DEBUG("[SR] Telemetry variant %u from %s", telemetry.which_variant, senderName);
        break;
    default:
        LOG_DEBUG("[SR] Unknown telemetry variant %u from %s", telemetry.which_variant, senderName);
        break;
    }

    CapabilityStatus currentStatus = getCapabilityStatus(mp.from);
    if (currentStatus == CapabilityStatus::Unknown) {
        trackNodeCapability(mp.from, CapabilityStatus::Legacy);
    } else {
        trackNodeCapability(mp.from, currentStatus);
    }
}

void SignalRoutingModule::handleRoutingControlPacket(const meshtastic_MeshPacket &mp)
{
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Routing_msg, &routing)) {
        return;
    }

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    switch (routing.which_variant) {
    case meshtastic_Routing_route_request_tag:
        LOG_DEBUG("[SR] Routing request from %s with %u hops recorded", senderName,
                  routing.route_request.route_count);
        break;
    case meshtastic_Routing_route_reply_tag:
        LOG_DEBUG("[SR] Routing reply from %s for %u hops", senderName, routing.route_reply.route_back_count);
        break;
    case meshtastic_Routing_error_reason_tag:
        if (routing.error_reason == meshtastic_Routing_Error_NONE) {
            LOG_DEBUG("[SR] Routing status from %s (no error)", senderName);
        } else {
            LOG_WARN("[SR] Routing error from %s reason=%u", senderName, routing.error_reason);
        }
        break;
    default:
        LOG_DEBUG("[SR] Routing control variant %u from %s", routing.which_variant, senderName);
        break;
    }

    trackNodeCapability(mp.from, CapabilityStatus::Capable);
}

bool SignalRoutingModule::isActiveRoutingRole() const
{
    switch (config.device.role) {
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
    case meshtastic_Config_DeviceConfig_Role_REPEATER:
    case meshtastic_Config_DeviceConfig_Role_CLIENT:
    case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE:
        return true;
    default:
        return false;
    }
}

SignalRoutingModule::CapabilityStatus SignalRoutingModule::capabilityFromRole(
    meshtastic_Config_DeviceConfig_Role role) const
{
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
    case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN:
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

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: linear search in fixed array
    for (uint8_t i = 0; i < capabilityRecordCount; i++) {
        if (capabilityRecords[i].nodeId == nodeId) {
            capabilityRecords[i].record.lastUpdated = now;
            if (status == CapabilityStatus::Capable) {
                capabilityRecords[i].record.status = CapabilityStatus::Capable;
            } else if (status == CapabilityStatus::Legacy) {
                capabilityRecords[i].record.status = CapabilityStatus::Legacy;
            }
            return;
        }
    }
    // Add new entry
    if (capabilityRecordCount < MAX_CAPABILITY_RECORDS) {
        capabilityRecords[capabilityRecordCount].nodeId = nodeId;
        capabilityRecords[capabilityRecordCount].record.lastUpdated = now;
        capabilityRecords[capabilityRecordCount].record.status = status;
        capabilityRecordCount++;
    }
#else
    auto &record = capabilityRecords[nodeId];
    record.lastUpdated = now;

    if (status == CapabilityStatus::Capable) {
        record.status = CapabilityStatus::Capable;
    } else if (status == CapabilityStatus::Legacy) {
        record.status = CapabilityStatus::Legacy;
    } else if (record.status == CapabilityStatus::Unknown) {
        record.status = CapabilityStatus::Unknown;
    }
#endif
}

void SignalRoutingModule::pruneCapabilityCache(uint32_t nowSecs)
{
#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: remove stale entries by swapping with last
    for (uint8_t i = 0; i < capabilityRecordCount;) {
        if ((nowSecs - capabilityRecords[i].record.lastUpdated) > CAPABILITY_TTL_SECS) {
            if (i < capabilityRecordCount - 1) {
                capabilityRecords[i] = capabilityRecords[capabilityRecordCount - 1];
            }
            capabilityRecordCount--;
        } else {
            i++;
        }
    }
#else
    for (auto it = capabilityRecords.begin(); it != capabilityRecords.end();) {
        if ((nowSecs - it->second.lastUpdated) > CAPABILITY_TTL_SECS) {
            it = capabilityRecords.erase(it);
        } else {
            ++it;
        }
    }
#endif
}

void SignalRoutingModule::pruneGatewayRelations(uint32_t nowSecs)
{
#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: remove stale gateway relations by swapping with last
    for (uint8_t i = 0; i < gatewayRelationCount;) {
        if ((nowSecs - gatewayRelations[i].lastSeen) > CAPABILITY_TTL_SECS) {
            NodeNum prunedDownstream = gatewayRelations[i].downstream;  // Capture before swap
            if (i < gatewayRelationCount - 1) {
                gatewayRelations[i] = gatewayRelations[gatewayRelationCount - 1];
            }
            gatewayRelationCount--;
            char downstreamName[64];
            getNodeDisplayName(prunedDownstream, downstreamName, sizeof(downstreamName));
            LOG_DEBUG("[SR] Pruned stale gateway relation (downstream %s)", downstreamName);
        } else {
            i++;
        }
    }

    // Also prune gateway downstream sets
    for (uint8_t i = 0; i < gatewayDownstreamCount;) {
        if ((nowSecs - gatewayDownstream[i].lastSeen) > CAPABILITY_TTL_SECS) {
            NodeNum prunedGateway = gatewayDownstream[i].gateway;  // Capture before swap
            if (i < gatewayDownstreamCount - 1) {
                gatewayDownstream[i] = gatewayDownstream[gatewayDownstreamCount - 1];
            }
            gatewayDownstreamCount--;
            char gatewayName[64];
            getNodeDisplayName(prunedGateway, gatewayName, sizeof(gatewayName));
            LOG_DEBUG("[SR] Pruned stale gateway downstream set (gateway %s)", gatewayName);
        } else {
            i++;
        }
    }
#else
    // Full mode: remove stale gateway relations based on time
    for (auto it = downstreamGateway.begin(); it != downstreamGateway.end();) {
        if ((nowSecs - it->second.lastSeen) > CAPABILITY_TTL_SECS) {
            // Capture values before erasing for logging
            NodeNum gatewayId = it->second.gateway;
            NodeNum downstreamId = it->first;

            // Remove from gateway's downstream set
            auto gwIt = gatewayDownstream.find(gatewayId);
            if (gwIt != gatewayDownstream.end()) {
                gwIt->second.erase(downstreamId);
                if (gwIt->second.empty()) {
                    gatewayDownstream.erase(gwIt);
                }
            }
            it = downstreamGateway.erase(it);

            char gatewayName[64], downstreamName[64];
            getNodeDisplayName(gatewayId, gatewayName, sizeof(gatewayName));
            getNodeDisplayName(downstreamId, downstreamName, sizeof(downstreamName));
            LOG_DEBUG("[SR] Pruned stale gateway relation (%s is gateway for %s)",
                     gatewayName, downstreamName);
            continue;
        }
        ++it;
    }
#endif
}

SignalRoutingModule::CapabilityStatus SignalRoutingModule::getCapabilityStatus(NodeNum nodeId) const
{
    uint32_t now = getTime();

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: linear search
    for (uint8_t i = 0; i < capabilityRecordCount; i++) {
        if (capabilityRecords[i].nodeId == nodeId) {
            if ((now - capabilityRecords[i].record.lastUpdated) > CAPABILITY_TTL_SECS) {
                return CapabilityStatus::Unknown;
            }
            return capabilityRecords[i].record.status;
        }
    }
    return CapabilityStatus::Unknown;
#else
    auto it = capabilityRecords.find(nodeId);
    if (it == capabilityRecords.end()) {
        return CapabilityStatus::Unknown;
    }

    if ((now - it->second.lastUpdated) > CAPABILITY_TTL_SECS) {
        return CapabilityStatus::Unknown;
    }

    return it->second.status;
#endif
}

bool SignalRoutingModule::isLegacyRouter(NodeNum nodeId) const
{
    if (!nodeDB) {
        return false;
    }
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    if (!node || !node->has_user) {
        return false;
    }

    auto role = node->user.role;
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
    case meshtastic_Config_DeviceConfig_Role_REPEATER:
        return true;
    default:
        return false;
    }
}

bool SignalRoutingModule::topologyHealthyForBroadcast() const
{
    LOG_DEBUG("[SR] Topology healthy for broadcast");
    if (!routingGraph || !nodeDB) {
        LOG_DEBUG("[SR] routingGraph or nodeDB is null, returning false");
        return false;
    }

    // Check if we have direct SR-capable neighbors for intelligent broadcast routing
    LOG_DEBUG("[SR] Checking direct neighbors");

#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* nodeEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!nodeEdges || nodeEdges->edgeCount == 0) {
        LOG_DEBUG("[SR] No edges found, returning false");
        return false;
    }

    size_t capableNeighbors = 0;
    for (uint8_t i = 0; i < nodeEdges->edgeCount; i++) {
        NodeNum to = nodeEdges->edges[i].to;
        CapabilityStatus status = getCapabilityStatus(to);
        if (status == CapabilityStatus::Capable || status == CapabilityStatus::Unknown) {
            capableNeighbors++;
        } else if (isLegacyRouter(to)) {
            capableNeighbors++;
        }
    }
#else
    const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!edges) {
        LOG_DEBUG("[SR] No edges returned, graph corrupted - disabling SR");
        return false;
    }
    if (!edges || edges->empty()) {
        LOG_DEBUG("[SR] No edges found, returning false");
        return false; // No direct neighbors at all
    }

    // Count how many direct neighbors are SR-capable or potentially capable (unknown status)
    LOG_DEBUG("[SR] Counting capable neighbors");
    size_t capableNeighbors = 0;
    for (const Edge& edge : *edges) {
        CapabilityStatus status = getCapabilityStatus(edge.to);
        if (status == CapabilityStatus::Capable || status == CapabilityStatus::Unknown) {
            capableNeighbors++;
        } else if (isLegacyRouter(edge.to)) {
            capableNeighbors++;
        }
    }
#endif

    // Need at least 1 direct neighbor that could be SR-capable for meaningful broadcast routing
    return capableNeighbors >= 1;
}

bool SignalRoutingModule::topologyHealthyForUnicast(NodeNum destination) const
{
    if (!routingGraph || !nodeDB) {
        return false;
    }

    // For unicast, we mainly care that we know about the destination
    // The actual next-hop capability is checked in shouldUseSignalBasedRouting
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

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Find or create bucket for this relayId
    RelayIdentityCacheEntry *bucket = nullptr;
    for (uint8_t i = 0; i < relayIdentityCacheCount; i++) {
        if (relayIdentityCache[i].relayId == relayId) {
            bucket = &relayIdentityCache[i];
            break;
        }
    }
    if (!bucket && relayIdentityCacheCount < MAX_RELAY_IDENTITY_ENTRIES) {
        bucket = &relayIdentityCache[relayIdentityCacheCount++];
        bucket->relayId = relayId;
        bucket->entryCount = 0;
    }
    if (!bucket) return;

    // Prune stale entries in bucket
    for (uint8_t i = 0; i < bucket->entryCount;) {
        if ((nowMs - bucket->entries[i].lastHeardMs) > RELAY_ID_CACHE_TTL_MS) {
            if (i < bucket->entryCount - 1) {
                bucket->entries[i] = bucket->entries[bucket->entryCount - 1];
            }
            bucket->entryCount--;
        } else {
            i++;
        }
    }

    // Update existing or add new
    for (uint8_t i = 0; i < bucket->entryCount; i++) {
        if (bucket->entries[i].nodeId == nodeId) {
            bucket->entries[i].lastHeardMs = nowMs;
            return;
        }
    }
    if (bucket->entryCount < 4) {
        bucket->entries[bucket->entryCount].nodeId = nodeId;
        bucket->entries[bucket->entryCount].lastHeardMs = nowMs;
        bucket->entryCount++;
    }
#else
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
#endif
}

void SignalRoutingModule::pruneRelayIdentityCache(uint32_t nowMs)
{
#ifdef SIGNAL_ROUTING_LITE_MODE
    for (uint8_t b = 0; b < relayIdentityCacheCount;) {
        RelayIdentityCacheEntry *bucket = &relayIdentityCache[b];
        // Prune entries
        for (uint8_t i = 0; i < bucket->entryCount;) {
            if ((nowMs - bucket->entries[i].lastHeardMs) > RELAY_ID_CACHE_TTL_MS) {
                if (i < bucket->entryCount - 1) {
                    bucket->entries[i] = bucket->entries[bucket->entryCount - 1];
                }
                bucket->entryCount--;
            } else {
                i++;
            }
        }
        // Remove empty buckets
        if (bucket->entryCount == 0) {
            if (b < relayIdentityCacheCount - 1) {
                relayIdentityCache[b] = relayIdentityCache[relayIdentityCacheCount - 1];
            }
            relayIdentityCacheCount--;
        } else {
            b++;
        }
    }
#else
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
#endif
}

NodeNum SignalRoutingModule::resolveRelayIdentity(uint8_t relayId) const
{
    uint32_t nowMs = millis();
    NodeNum bestNode = 0;
    uint32_t newest = 0;

#ifdef SIGNAL_ROUTING_LITE_MODE
    for (uint8_t b = 0; b < relayIdentityCacheCount; b++) {
        if (relayIdentityCache[b].relayId == relayId) {
            const RelayIdentityCacheEntry *bucket = &relayIdentityCache[b];
            for (uint8_t i = 0; i < bucket->entryCount; i++) {
                if ((nowMs - bucket->entries[i].lastHeardMs) > RELAY_ID_CACHE_TTL_MS) {
                    continue;
                }
                if (bucket->entries[i].lastHeardMs >= newest) {
                    newest = bucket->entries[i].lastHeardMs;
                    bestNode = bucket->entries[i].nodeId;
                }
            }
            break;
        }
    }
#else
    auto it = relayIdentityCache.find(relayId);
    if (it == relayIdentityCache.end()) {
        return 0;
    }

    for (const auto &entry : it->second) {
        if ((nowMs - entry.lastHeardMs) > RELAY_ID_CACHE_TTL_MS) {
            continue;
        }
        if (entry.lastHeardMs >= newest) {
            newest = entry.lastHeardMs;
            bestNode = entry.nodeId;
        }
    }
#endif

    return bestNode;
}

// Record gateway/downstream relationship inferred from relayed packets
void SignalRoutingModule::removeGatewayRelationship(NodeNum gateway, NodeNum downstream)
{
    if (gateway == 0 || downstream == 0 || gateway == downstream) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Remove gateway relation for this downstream
    for (uint8_t i = 0; i < gatewayRelationCount; ) {
        if (gatewayRelations[i].gateway == gateway && gatewayRelations[i].downstream == downstream) {
            // Remove by shifting remaining elements
            for (uint8_t j = i; j < gatewayRelationCount - 1; j++) {
                gatewayRelations[j] = gatewayRelations[j + 1];
            }
            gatewayRelationCount--;
        } else {
            i++;
        }
    }

    // Remove downstream from gateway's list
    GatewayDownstreamSet *set = nullptr;
    for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
        if (gatewayDownstream[i].gateway == gateway) {
            set = &gatewayDownstream[i];
            break;
        }
    }
    if (set) {
        // Remove the downstream from the set
        uint8_t writeIdx = 0;
        for (uint8_t readIdx = 0; readIdx < set->count; readIdx++) {
            if (set->downstream[readIdx] != downstream) {
                if (writeIdx != readIdx) {
                    set->downstream[writeIdx] = set->downstream[readIdx];
                }
                writeIdx++;
            }
        }
        set->count = writeIdx;
    }
#else
    // Remove from downstreamGateway map
    auto it = downstreamGateway.find(downstream);
    if (it != downstreamGateway.end() && it->second.gateway == gateway) {
        downstreamGateway.erase(it);
    }

    // Remove from gatewayDownstream map
    auto git = gatewayDownstream.find(gateway);
    if (git != gatewayDownstream.end()) {
        git->second.erase(downstream);
    }
#endif
}

void SignalRoutingModule::clearDownstreamFromAllGateways(NodeNum downstream)
{
    if (downstream == 0) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Remove from gatewayRelations where this is the downstream
    for (uint8_t i = 0; i < gatewayRelationCount; ) {
        if (gatewayRelations[i].downstream == downstream) {
            // Remove by shifting remaining elements
            for (uint8_t j = i; j < gatewayRelationCount - 1; j++) {
                gatewayRelations[j] = gatewayRelations[j + 1];
            }
            gatewayRelationCount--;
        } else {
            i++;
        }
    }

    // Remove from all gatewayDownstream sets
    for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
        GatewayDownstreamSet &set = gatewayDownstream[i];
        uint8_t writeIdx = 0;
        for (uint8_t readIdx = 0; readIdx < set.count; readIdx++) {
            if (set.downstream[readIdx] != downstream) {
                if (writeIdx != readIdx) {
                    set.downstream[writeIdx] = set.downstream[readIdx];
                }
                writeIdx++;
            }
        }
        set.count = writeIdx;
    }
#else
    // Remove from downstreamGateway
    downstreamGateway.erase(downstream);

    // Remove from ALL gatewayDownstream sets
    for (auto& pair : gatewayDownstream) {
        pair.second.erase(downstream);
    }
#endif

    LOG_DEBUG("[SR] Cleared downstream %08x from all gateway lists", downstream);
}

void SignalRoutingModule::recordGatewayRelation(NodeNum gateway, NodeNum downstream)
{
    if (gateway == 0 || downstream == 0 || gateway == downstream) return;

    uint32_t now = getTime();

#ifdef SIGNAL_ROUTING_LITE_MODE
    bool found = false;
    for (uint8_t i = 0; i < gatewayRelationCount; i++) {
        if (gatewayRelations[i].downstream == downstream) {
            gatewayRelations[i].gateway = gateway;
            gatewayRelations[i].lastSeen = now;
            found = true;
            break;
        }
    }
    if (!found && gatewayRelationCount < MAX_GATEWAY_RELATIONS) {
        gatewayRelations[gatewayRelationCount].gateway = gateway;
        gatewayRelations[gatewayRelationCount].downstream = downstream;
        gatewayRelations[gatewayRelationCount].lastSeen = now;
        gatewayRelationCount++;
    }

    GatewayDownstreamSet *set = nullptr;
    for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
        if (gatewayDownstream[i].gateway == gateway) {
            set = &gatewayDownstream[i];
            break;
        }
    }
    if (!set && gatewayDownstreamCount < MAX_GATEWAY_RELATIONS) {
        set = &gatewayDownstream[gatewayDownstreamCount++];
        set->gateway = gateway;
        set->count = 0;
        set->lastSeen = now;
    }
    if (set) {
        set->lastSeen = now;
        bool present = false;
        for (uint8_t i = 0; i < set->count; i++) {
            if (set->downstream[i] == downstream) {
                present = true;
                break;
            }
        }
        if (!present && set->count < MAX_GATEWAY_DOWNSTREAM) {
            set->downstream[set->count++] = downstream;
        }
    }
#else
    // Remove from old gateway's set before adding to new one
    auto oldIt = downstreamGateway.find(downstream);
    if (oldIt != downstreamGateway.end() && oldIt->second.gateway != gateway) {
        auto oldGwIt = gatewayDownstream.find(oldIt->second.gateway);
        if (oldGwIt != gatewayDownstream.end()) {
            oldGwIt->second.erase(downstream);
        }
    }
    downstreamGateway[downstream] = {gateway, now};
    gatewayDownstream[gateway].insert(downstream);
#endif

    LOG_DEBUG("[SR] Gateway inference: %08x is gateway for %08x", gateway, downstream);
}

NodeNum SignalRoutingModule::getGatewayFor(NodeNum downstream) const
{
#ifdef SIGNAL_ROUTING_LITE_MODE
    uint32_t now = getTime();
    for (uint8_t i = 0; i < gatewayRelationCount; i++) {
        if (gatewayRelations[i].downstream == downstream) {
            if ((now - gatewayRelations[i].lastSeen) < CAPABILITY_TTL_SECS) {
                return gatewayRelations[i].gateway;
            }
        }
    }
    return 0;
#else
    auto it = downstreamGateway.find(downstream);
    if (it == downstreamGateway.end()) return 0;
    return it->second.gateway;
#endif
}

size_t SignalRoutingModule::getGatewayDownstreamCount(NodeNum gateway) const
{
#ifdef SIGNAL_ROUTING_LITE_MODE
    uint32_t now = getTime();
    for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
        if (gatewayDownstream[i].gateway == gateway) {
            if ((now - gatewayDownstream[i].lastSeen) > CAPABILITY_TTL_SECS) {
                return 0;
            }
            return gatewayDownstream[i].count;
        }
    }
    return 0;
#else
    auto it = gatewayDownstream.find(gateway);
    if (it == gatewayDownstream.end()) return 0;
    return it->second.size();
#endif
}

void SignalRoutingModule::clearGatewayRelationsFor(NodeNum node)
{
    if (node == 0) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Remove gateway relations where this node is the gateway
    for (uint8_t i = 0; i < gatewayRelationCount; ) {
        if (gatewayRelations[i].gateway == node) {
            // Remove by shifting remaining elements
            for (uint8_t j = i; j < gatewayRelationCount - 1; j++) {
                gatewayRelations[j] = gatewayRelations[j + 1];
            }
            gatewayRelationCount--;
        } else {
            i++;
        }
    }

    // Remove downstream sets for this gateway
    for (uint8_t i = 0; i < gatewayDownstreamCount; ) {
        if (gatewayDownstream[i].gateway == node) {
            // Remove by shifting remaining elements
            for (uint8_t j = i; j < gatewayDownstreamCount - 1; j++) {
                gatewayDownstream[j] = gatewayDownstream[j + 1];
            }
            gatewayDownstreamCount--;
        } else {
            i++;
        }
    }
#else
    // In full mode, erase from maps
    gatewayDownstream.erase(node);
    // Also remove any relations where this node is the gateway
    for (auto it = downstreamGateway.begin(); it != downstreamGateway.end(); ) {
        if (it->second.gateway == node) {
            it = downstreamGateway.erase(it);
        } else {
            ++it;
        }
    }
#endif
}

uint32_t SignalRoutingModule::getNodeLastActivityTime(NodeNum nodeId) const
{
    uint32_t now = getTime();

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: linear search
    for (uint8_t i = 0; i < capabilityRecordCount; i++) {
        if (capabilityRecords[i].nodeId == nodeId) {
            if ((now - capabilityRecords[i].record.lastUpdated) > CAPABILITY_TTL_SECS) {
                return 0; // Too old, consider inactive
            }
            return capabilityRecords[i].record.lastUpdated;
        }
    }
    return 0;
#else
    auto it = capabilityRecords.find(nodeId);
    if (it == capabilityRecords.end()) {
        return 0;
    }

    if ((now - it->second.lastUpdated) > CAPABILITY_TTL_SECS) {
        return 0; // Too old, consider inactive
    }

    return it->second.lastUpdated;
#endif
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

    if (routingGraph && nodeDB) {
#ifdef SIGNAL_ROUTING_LITE_MODE
        const NodeEdgesLite *myEdges = static_cast<GraphLite*>(routingGraph)->getEdgesFrom(nodeDB->getNodeNum());
        if (myEdges) {
            for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                if ((myEdges->edges[i].to & 0xFF) == p->relay_node) {
                    return myEdges->edges[i].to;
                }
            }
        }
#else
        auto neighbors = routingGraph->getDirectNeighbors(nodeDB->getNodeNum());
        for (NodeNum neighbor : neighbors) {
            if ((neighbor & 0xFF) == p->relay_node) {
                return neighbor;
            }
        }
#endif
    }

    return sourceNode;
}

void SignalRoutingModule::processSpeculativeRetransmits(uint32_t nowMs)
{
#ifdef SIGNAL_ROUTING_LITE_MODE
    for (uint8_t i = 0; i < speculativeRetransmitCount;) {
        if (nowMs >= speculativeRetransmits[i].expiryMs) {
            if (speculativeRetransmits[i].packetCopy) {
                LOG_INFO("[SR] Speculative retransmit for packet %08x", speculativeRetransmits[i].packetId);
                service->sendToMesh(speculativeRetransmits[i].packetCopy);
                speculativeRetransmits[i].packetCopy = nullptr;
            }
            // Remove by swapping with last
            if (i < speculativeRetransmitCount - 1) {
                speculativeRetransmits[i] = speculativeRetransmits[speculativeRetransmitCount - 1];
            }
            speculativeRetransmitCount--;
        } else {
            i++;
        }
    }
#else
    for (auto it = speculativeRetransmits.begin(); it != speculativeRetransmits.end();) {
        if (nowMs >= it->second.expiryMs) {
            if (it->second.packetCopy) {
                LOG_INFO("[SR] Speculative retransmit for packet %08x", it->second.packetId);
                service->sendToMesh(it->second.packetCopy);
                it->second.packetCopy = nullptr;
            }
            it = speculativeRetransmits.erase(it);
        } else {
            ++it;
        }
    }
#endif
}

void SignalRoutingModule::cancelSpeculativeRetransmit(NodeNum origin, uint32_t packetId)
{
    uint64_t key = makeSpeculativeKey(origin, packetId);

#ifdef SIGNAL_ROUTING_LITE_MODE
    for (uint8_t i = 0; i < speculativeRetransmitCount; i++) {
        if (speculativeRetransmits[i].key == key) {
            if (speculativeRetransmits[i].packetCopy) {
                packetPool.release(speculativeRetransmits[i].packetCopy);
            }
            if (i < speculativeRetransmitCount - 1) {
                speculativeRetransmits[i] = speculativeRetransmits[speculativeRetransmitCount - 1];
            }
            speculativeRetransmitCount--;
            return;
        }
    }
#else
    auto it = speculativeRetransmits.find(key);
    if (it == speculativeRetransmits.end()) {
        return;
    }

    if (it->second.packetCopy) {
        packetPool.release(it->second.packetCopy);
    }
    speculativeRetransmits.erase(it);
#endif
}

uint64_t SignalRoutingModule::makeSpeculativeKey(NodeNum origin, uint32_t packetId)
{
    return (static_cast<uint64_t>(origin) << 32) | packetId;
}
