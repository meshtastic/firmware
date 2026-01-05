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

    trackNodeCapability(nodeDB->getNodeNum(), CapabilityStatus::SRactive);

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
    uint32_t nowSecs = millis() / 1000;  // Use monotonic time for aging

    pruneCapabilityCache(nowSecs);
    pruneGatewayRelations(nowSecs);
    pruneRelayIdentityCache(nowMs);

#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
    // Turn off heartbeat LED if duration expired (for operation feedback)
    if (heartbeatEndTime > 0 && nowMs >= heartbeatEndTime) {
        turnOffRgbLed();
        heartbeatEndTime = 0;
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

    // Track time until LED operation feedback should turn off
    uint32_t timeToLedOff = UINT32_MAX;
    if (heartbeatEndTime > nowMs) {
        timeToLedOff = heartbeatEndTime - nowMs;
    }

    uint32_t timeToBroadcast = SIGNAL_ROUTING_BROADCAST_SECS * 1000;
    if (nowMs - lastBroadcast < SIGNAL_ROUTING_BROADCAST_SECS * 1000) {
        timeToBroadcast = (SIGNAL_ROUTING_BROADCAST_SECS * 1000) - (nowMs - lastBroadcast);
    }

    // Turn off LED when RTOS task completes
    turnOffRgbLed();

    uint32_t nextDelay = std::min({timeToLedOff, timeToBroadcast});
    if (nextDelay < 20) {
        nextDelay = 20;
    }
    return static_cast<int32_t>(nextDelay);
}

void SignalRoutingModule::sendSignalRoutingInfo(NodeNum dest)
{
    // Allow mute nodes to broadcast their direct neighbors to help active SR nodes
    // make better unicast routing decisions, even though mute nodes don't participate in relaying
    if (!canSendTopology()) {
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

    // Update our own capability before sending
    trackNodeCapability(nodeDB->getNodeNum(), info.signal_based_capable ? CapabilityStatus::SRactive : CapabilityStatus::SRinactive);

    service->sendToMesh(p);
    lastBroadcast = millis();

    // Record our transmission for contention window tracking
    if (routingGraph) {
        uint32_t currentTime = millis() / 1000;  // Use monotonic time
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

    // Filter out placeholders before assigning to neighbors array
    const EdgeLite* filteredSelected[GRAPH_LITE_MAX_EDGES_PER_NODE];
    size_t filteredCount = 0;
    size_t placeholdersFiltered = 0;
    for (size_t i = 0; i < selectedCount; i++) {
        if (!isPlaceholderNode(selected[i]->to)) {
            filteredSelected[filteredCount++] = selected[i];
        } else {
            placeholdersFiltered++;
        }
    }
    if (placeholdersFiltered > 0) {
        LOG_DEBUG("[SR] Filtered %u placeholder nodes from topology broadcast", placeholdersFiltered);
    }

    info.neighbors_count = filteredCount;

    for (size_t i = 0; i < filteredCount; i++) {
        const EdgeLite& edge = *filteredSelected[i];
        meshtastic_SignalNeighbor& neighbor = info.neighbors[i];

        neighbor.node_id = edge.to;
        neighbor.position_variance = edge.variance; // Already uint8, 0-255 scaled
        // Mark neighbor based on local knowledge of their SR capability
        CapabilityStatus neighborStatus = getCapabilityStatus(edge.to);
        neighbor.signal_based_capable = (neighborStatus == CapabilityStatus::SRactive);

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

    // Filter out placeholders before assigning to neighbors array
    std::vector<const Edge*> filteredSelected;
    filteredSelected.reserve(selected.size());
    size_t placeholdersFiltered = 0;
    for (auto* e : selected) {
        if (!isPlaceholderNode(e->to)) {
            filteredSelected.push_back(e);
        } else {
            placeholdersFiltered++;
        }
    }
    if (placeholdersFiltered > 0) {
        LOG_DEBUG("[SR] Filtered %u placeholder nodes from topology broadcast", placeholdersFiltered);
    }

    info.neighbors_count = filteredSelected.size();

    for (size_t i = 0; i < filteredSelected.size(); i++) {
        const Edge& edge = *filteredSelected[i];
        meshtastic_SignalNeighbor& neighbor = info.neighbors[i];

        neighbor.node_id = edge.to;
        // Scale variance from uint32 (0-3000) to uint8 (0-255)
        uint32_t scaledVar = edge.variance / 12;
        neighbor.position_variance = scaledVar > 255 ? 255 : static_cast<uint8_t>(scaledVar);
        // Mark neighbor based on local knowledge of their SR capability
        CapabilityStatus neighborStatus = getCapabilityStatus(edge.to);
        neighbor.signal_based_capable = (neighborStatus == CapabilityStatus::SRactive);

        int32_t rssi32, snr32;
        Graph::etxToSignal(edge.etx, rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));
    }
#endif
}

void SignalRoutingModule::preProcessSignalRoutingPacket(const meshtastic_MeshPacket *p, uint32_t packetReceivedTimestamp)
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

    // Mark sender based on their claimed SR capability
    // signal_based_capable=true means they can be used for routing (SR-active)
    // signal_based_capable=false means they are SR-aware but not for routing
    CapabilityStatus senderStatus = info.signal_based_capable ? CapabilityStatus::SRactive : CapabilityStatus::SRinactive;
    LOG_DEBUG("[SR] Processing broadcast from %08x: signal_based_capable=%d, marking as %s",
              p->from, info.signal_based_capable,
              senderStatus == CapabilityStatus::SRactive ? "SR-active" : "SR-inactive");
    trackNodeCapability(p->from, senderStatus);

    char senderName[64];
    getNodeDisplayName(p->from, senderName, sizeof(senderName));
    LOG_DEBUG("[SR] Pre-processing %d neighbors from %s for relay decision",
              info.neighbors_count, senderName);

    // Add edges from each neighbor TO the sender
    // The RSSI/SNR describes how well the sender hears the neighbor,
    // which characterizes the neighbor→sender transmission quality
    // Use reliable time-from-boot passed from caller
    uint32_t rxTime = packetReceivedTimestamp;
    for (pb_size_t i = 0; i < info.neighbors_count; i++) {
        const meshtastic_SignalNeighbor& neighbor = info.neighbors[i];
        // Don't update neighbor capability based on broadcast reports
        // Only mark nodes as SR-capable when they send their own broadcasts
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
    // Inactive SR roles don't participate in routing decisions - skip processing topology broadcasts from others
    if (!isActiveRoutingRole()) {
        LOG_DEBUG("[SR] Inactive role: Skipping topology broadcast processing from %08x", mp.from);
        return false;
    }

    // Note: Graph may have already been updated by preProcessSignalRoutingPacket()
    // This is intentional - we want up-to-date data for relay decisions
    if (!routingGraph || !p) return false;

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    // Mark sender based on their claimed SR capability
    CapabilityStatus newStatus = p->signal_based_capable ? CapabilityStatus::SRactive : CapabilityStatus::SRinactive;
    CapabilityStatus oldStatus = getCapabilityStatus(mp.from);
    trackNodeCapability(mp.from, newStatus);

    if (oldStatus != newStatus) {
        LOG_INFO("[SR] Capability update: %s changed from %d to %d",
                senderName, (int)oldStatus, (int)newStatus);
    }

    if (p->neighbors_count == 0) {
        LOG_INFO("[SR] %s is online (SR v%d, %s) - no neighbors detected yet",
                 senderName, p->routing_version,
                 p->signal_based_capable ? "SR-active" : "SR-inactive");

        // Clear gateway relationships for SR-capable nodes with no neighbors - they can't be gateways
        if (p->signal_based_capable) {
            clearGatewayRelationsFor(mp.from);
        }

        return false;
    }

    LOG_INFO("[SR] RECEIVED: %s reports %d neighbors (SR v%d, %s)",
             senderName, p->neighbors_count, p->routing_version,
             p->signal_based_capable ? "SR-active" : "SR-inactive");

    // Set cyan for network topology update (operation start)
    setRgbLed(0, 255, 255);

    // For SR-inactive nodes (signal_based_capable = false), we still need to store their edges for direct connection checks
    // Active nodes use these edges to determine if a SR-inactive node has direct connections to destinations
    // However, routing algorithms must not consider paths through SR-inactive nodes since they don't relay
    if (!p->signal_based_capable) {
        LOG_DEBUG("[SR] Received topology from SR-inactive node %08x - storing edges for direct connection detection", mp.from);
    }

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
    // Use reliable time-from-boot for internal timing
    // (This may be redundant if preProcessSignalRoutingPacket already ran, but it's idempotent)
    uint32_t rxTime = millis() / 1000;
    for (pb_size_t i = 0; i < p->neighbors_count; i++) {
        const meshtastic_SignalNeighbor& neighbor = p->neighbors[i];

        char neighborName[64];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));

        // Don't update neighbor capability based on broadcast reports
        // Only mark nodes as SR-capable when they send their own broadcasts

        // Note: We intentionally do NOT resolve placeholders from topology merging
        // to avoid conflicts. Placeholders are only resolved from:
        // 1. Direct contact (nodes we hear directly)
        // 2. Traceroute packets (confirmed routing paths)

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

        // Note: Neighbor nodes from topology broadcasts are not automatically added to NodeDB
        // They remain "unknown" for unicast routing unless heard directly
        // This is by design to avoid filling NodeDB with remote nodes

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
                 neighbor.signal_based_capable ? "SR-active" : "SR-inactive",
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
// Placeholder node system for unknown relays
// Use high NodeNum values that won't conflict with real nodes
#define PLACEHOLDER_BASE 0xFF000000

bool SignalRoutingModule::isPlaceholderNode(NodeNum nodeId) const
{
    return (nodeId & PLACEHOLDER_BASE) == PLACEHOLDER_BASE;
}

NodeNum SignalRoutingModule::createPlaceholderNode(uint8_t relayId)
{
    NodeNum placeholderId = PLACEHOLDER_BASE | relayId;
    LOG_INFO("[SR] Created placeholder node %08x for unknown relay 0x%02x", placeholderId, relayId);
    return placeholderId;
}

bool SignalRoutingModule::resolvePlaceholder(NodeNum placeholderId, NodeNum realNodeId)
{
    if (!isPlaceholderNode(placeholderId)) {
        return false; // Not a placeholder
    }

    if (isPlaceholderNode(realNodeId)) {
        return false; // Can't resolve to another placeholder
    }

    // Check if this placeholder is already resolved to a different node
    uint8_t relayId = placeholderId & 0xFF;
    NodeNum alreadyResolved = resolveRelayIdentity(relayId);
    if (alreadyResolved != 0 && alreadyResolved != realNodeId) {
        LOG_WARN("[SR] Placeholder %08x already resolved to %08x, refusing to resolve to %08x",
                placeholderId, alreadyResolved, realNodeId);
        return false; // Already resolved to a different node
    }


    // Update relay identity cache - this ensures future relay resolutions work
    rememberRelayIdentity(realNodeId, relayId);

    // Update gateway relationships
    replaceGatewayNode(placeholderId, realNodeId);

    // Transfer graph edges from placeholder to real node and remove placeholder
    // This ensures topology continuity during resolution
    if (routingGraph) {
        NodeNum ourNode = nodeDB->getNodeNum();

#ifdef SIGNAL_ROUTING_LITE_MODE
        // Get all edges where placeholder is the source
        const NodeEdgesLite* placeholderEdges = routingGraph->getEdgesFrom(placeholderId);
        if (placeholderEdges) {
            for (uint8_t i = 0; i < placeholderEdges->edgeCount; i++) {
                NodeNum target = placeholderEdges->edges[i].to;
                float etx = placeholderEdges->edges[i].getEtx();
                // Create equivalent edge from real node to target
                routingGraph->updateEdge(realNodeId, target, etx, millis() / 1000);
                LOG_DEBUG("[SR] Transferred edge: %08x -> %08x (ETX=%.2f)",
                         realNodeId, target, etx);
            }
        }

        // Also check for reverse edges (where placeholder is the target)
        const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
        if (ourEdges) {
            for (uint8_t i = 0; i < ourEdges->edgeCount; i++) {
                if (ourEdges->edges[i].to == placeholderId) {
                    float etx = ourEdges->edges[i].getEtx();
                    // Create equivalent edge from our node to real node
                    routingGraph->updateEdge(ourNode, realNodeId, etx, millis() / 1000);
                    LOG_DEBUG("[SR] Transferred reverse edge: %08x -> %08x (ETX=%.2f)",
                             ourNode, realNodeId, etx);
                }
            }
        }
#else
        // Get all edges where placeholder is the source
        const std::vector<Edge>* placeholderEdges = routingGraph->getEdgesFrom(placeholderId);
        if (placeholderEdges) {
            for (const Edge& edge : *placeholderEdges) {
                // Create equivalent edge from real node to target
                routingGraph->updateEdge(realNodeId, edge.to, edge.etx, edge.lastUpdate, edge.variance);
                LOG_DEBUG("[SR] Transferred edge: %08x -> %08x (ETX=%.2f)",
                         realNodeId, edge.to, edge.etx);
            }
        }

        // Also check for reverse edges (where placeholder is the target)
        const std::vector<Edge>* ourEdges = routingGraph->getEdgesFrom(ourNode);
        if (ourEdges) {
            for (const Edge& edge : *ourEdges) {
                if (edge.to == placeholderId) {
                    // Create equivalent edge from our node to real node
                    routingGraph->updateEdge(ourNode, realNodeId, edge.etx, edge.lastUpdate, edge.variance);
                    LOG_DEBUG("[SR] Transferred reverse edge: %08x -> %08x (ETX=%.2f)",
                             ourNode, realNodeId, edge.etx);
                }
            }
        }
#endif

        // Remove the placeholder node from the graph now that edges are transferred
        routingGraph->removeNode(placeholderId);

        LOG_INFO("[SR] Resolved placeholder %08x -> real node %08x", placeholderId, realNodeId);
        LOG_DEBUG("[SR] Removed placeholder node %08x from graph", placeholderId);
    }

    return true;
}

NodeNum SignalRoutingModule::getPlaceholderForRelay(uint8_t relayId) const
{
    return PLACEHOLDER_BASE | relayId;
}

void SignalRoutingModule::replaceGatewayNode(NodeNum oldNode, NodeNum newNode)
{
    if (oldNode == newNode) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Update gatewayRelations array
    for (uint8_t i = 0; i < gatewayRelationCount; i++) {
        if (gatewayRelations[i].gateway == oldNode) {
            gatewayRelations[i].gateway = newNode;
        }
        if (gatewayRelations[i].downstream == oldNode) {
            gatewayRelations[i].downstream = newNode;
        }
    }

    // Update gatewayDownstream array
    for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
        if (gatewayDownstream[i].gateway == oldNode) {
            gatewayDownstream[i].gateway = newNode;
        }
        for (uint8_t j = 0; j < gatewayDownstream[i].count; j++) {
            if (gatewayDownstream[i].downstream[j] == oldNode) {
                gatewayDownstream[i].downstream[j] = newNode;
            }
        }
    }
#else
    // Full mode: update std::unordered_map structures
    // Update downstreamGateway
    auto it = downstreamGateway.find(oldNode);
    if (it != downstreamGateway.end()) {
        GatewayRelationEntry entry = it->second;
        downstreamGateway.erase(it);
        downstreamGateway[newNode] = entry;
    }

    // Update gatewayDownstream
    auto git = gatewayDownstream.find(oldNode);
    if (git != gatewayDownstream.end()) {
        auto downstreamSet = git->second;
        gatewayDownstream.erase(git);
        gatewayDownstream[newNode] = downstreamSet;
    }

    // Update any references within downstream sets
    for (auto& pair : gatewayDownstream) {
        auto& downstreamSet = pair.second;
        // Remove oldNode if it exists
        downstreamSet.erase(oldNode);
        // Note: we don't add newNode here as it's now the key
    }
#endif
}

bool SignalRoutingModule::isPlaceholderConnectedToUs(NodeNum placeholderId) const
{
    if (!routingGraph || !isPlaceholderNode(placeholderId)) {
        return false;
    }

    // Check if the placeholder has edges connected to our node
    NodeNum ourNode = nodeDB->getNodeNum();

#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* edges = routingGraph->getEdgesFrom(placeholderId);
    if (edges) {
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (edges->edges[i].to == ourNode) {
                return true;
            }
        }
    }
#else
    const std::vector<Edge>* edges = routingGraph->getEdgesFrom(placeholderId);
    if (edges) {
        for (const Edge& edge : *edges) {
            if (edge.to == ourNode) {
                return true;
            }
        }
    }
#endif

    return false;
}

bool SignalRoutingModule::shouldRelayForStockNeighbors(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime)
{
    if (!routingGraph) {
        return false;
    }

    // Find stock firmware nodes that might need coverage: direct neighbors + downstream nodes
    std::vector<NodeNum> stockNeighbors;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Check our direct neighbors for stock firmware nodes
    const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
    if (myEdges) {
        for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
            NodeNum neighbor = myEdges->edges[i].to;
            if (getCapabilityStatus(neighbor) == CapabilityStatus::Legacy) {
                stockNeighbors.push_back(neighbor);
            }
        }
    }

    // Also check downstream nodes of direct stock relays
    if (myEdges) {
        for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
            NodeNum relay = myEdges->edges[i].to;
            // Check if this relay has downstream nodes
            for (uint8_t j = 0; j < gatewayRelationCount; j++) {
                if (gatewayRelations[j].gateway == relay) {
                    NodeNum downstream = gatewayRelations[j].downstream;
                    if (getCapabilityStatus(downstream) == CapabilityStatus::Legacy) {
                        stockNeighbors.push_back(downstream);
                    }
                }
            }
        }
    }
#else
    const std::vector<Edge>* myEdges = routingGraph->getEdgesFrom(myNode);
    if (myEdges) {
        for (const Edge& edge : *myEdges) {
            NodeNum neighbor = edge.to;
            if (getCapabilityStatus(neighbor) == CapabilityStatus::Legacy) {
                stockNeighbors.push_back(neighbor);
            }
        }
    }

    // Also check downstream nodes of direct stock relays
    if (myEdges) {
        for (const Edge& edge : *myEdges) {
            NodeNum relay = edge.to;
            // Check if this relay has downstream nodes
            auto it = gatewayDownstream.find(relay);
            if (it != gatewayDownstream.end()) {
                for (NodeNum downstream : it->second) {
                    if (getCapabilityStatus(downstream) == CapabilityStatus::Legacy) {
                        stockNeighbors.push_back(downstream);
                    }
                }
            }
        }
    }
#endif

    if (stockNeighbors.empty()) {
        return false; // No stock neighbors to worry about
    }

    LOG_INFO("[SR] Checking broadcast coverage for %u stock neighbors", static_cast<unsigned int>(stockNeighbors.size()));

    // Check if any stock neighbor needs this packet
    // A stock neighbor needs the packet if they didn't hear it directly from the source
    bool hasUncoveredStockNeighbor = false;
    NodeNum bestStockNeighbor = 0;
    float bestStockCost = std::numeric_limits<float>::max();

    for (NodeNum stockNeighbor : stockNeighbors) {
        // Check if stock neighbor heard the transmission directly
        // If source can reach stock neighbor directly, they already heard it
        // Also check if heardFrom (relaying SR node) can reach them directly
        bool heardDirectly = false;

        // Check if original source can reach stock neighbor
#ifdef SIGNAL_ROUTING_LITE_MODE
        const NodeEdgesLite* sourceEdges = routingGraph->getEdgesFrom(sourceNode);
        if (sourceEdges) {
            for (uint8_t i = 0; i < sourceEdges->edgeCount; i++) {
                if (sourceEdges->edges[i].to == stockNeighbor) {
                    heardDirectly = true;
                    break;
                }
            }
        }
#else
        const std::vector<Edge>* sourceEdges = routingGraph->getEdgesFrom(sourceNode);
        if (sourceEdges) {
            for (const Edge& edge : *sourceEdges) {
                if (edge.to == stockNeighbor) {
                    heardDirectly = true;
                    break;
                }
            }
        }
#endif

        // If not heard from source, check if heard from relaying SR node
        if (!heardDirectly) {
#ifdef SIGNAL_ROUTING_LITE_MODE
            const NodeEdgesLite* heardFromEdges = routingGraph->getEdgesFrom(heardFrom);
            if (heardFromEdges) {
                for (uint8_t i = 0; i < heardFromEdges->edgeCount; i++) {
                    if (heardFromEdges->edges[i].to == stockNeighbor) {
                        heardDirectly = true;
                        LOG_DEBUG("[SR] Stock neighbor %08x already covered by relaying SR node %08x",
                                 stockNeighbor, heardFrom);
                        break;
                    }
                }
            }
#else
            const std::vector<Edge>* heardFromEdges = routingGraph->getEdgesFrom(heardFrom);
            if (heardFromEdges) {
                for (const Edge& edge : *heardFromEdges) {
                    if (edge.to == stockNeighbor) {
                        heardDirectly = true;
                        LOG_DEBUG("[SR] Stock neighbor %08x already covered by relaying SR node %08x",
                                 stockNeighbor, heardFrom);
                        break;
                    }
                }
            }
#endif
        }

        if (!heardDirectly) {
            hasUncoveredStockNeighbor = true;
            LOG_DEBUG("[SR] Stock neighbor %08x did not hear transmission directly", stockNeighbor);

            // Check if we're the best positioned to reach this stock neighbor
#ifdef SIGNAL_ROUTING_LITE_MODE
            const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
            if (myEdges) {
                for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                    if (myEdges->edges[i].to == stockNeighbor) {
                        float cost = myEdges->edges[i].getEtx();
                        if (cost < bestStockCost) {
                            bestStockCost = cost;
                            bestStockNeighbor = stockNeighbor;
                        }
                        break;
                    }
                }
            }
#else
            const std::vector<Edge>* myEdges = routingGraph->getEdgesFrom(myNode);
            if (myEdges) {
                for (const Edge& edge : *myEdges) {
                    if (edge.to == stockNeighbor) {
                        float cost = edge.etx;
                        if (cost < bestStockCost) {
                            bestStockCost = cost;
                            bestStockNeighbor = stockNeighbor;
                        }
                        break;
                    }
                }
            }
#endif
        }
    }

    if (hasUncoveredStockNeighbor && bestStockNeighbor != 0) {
        LOG_INFO("[SR] STOCK COVERAGE: Relaying broadcast for uncovered stock neighbor %08x (ETX=%.2f)",
                 bestStockNeighbor, bestStockCost);
        return true;
    }

    if (hasUncoveredStockNeighbor) {
        LOG_DEBUG("[SR] STOCK COVERAGE: Found %u uncovered stock neighbors but no valid relay path from this node", static_cast<unsigned int>(stockNeighbors.size()));
    }

    return false;
}

bool SignalRoutingModule::isDownstreamOfHeardRelay(NodeNum destination, NodeNum myNode)
{
    if (!routingGraph) {
        return false;
    }

    // Check if destination is downstream of any relay we can hear directly
#ifdef SIGNAL_ROUTING_LITE_MODE
    // Check our direct neighbors for relays that have this destination as downstream
    const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
    if (myEdges) {
        for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
            NodeNum neighbor = myEdges->edges[i].to;
            // Check if this neighbor has destination as downstream
            for (uint8_t j = 0; j < gatewayRelationCount; j++) {
                if (gatewayRelations[j].gateway == neighbor && gatewayRelations[j].downstream == destination) {
                    LOG_INFO("[SR] Found downstream: %08x is downstream of relay %08x (direct neighbor)", destination, neighbor);
                    return true;
                }
            }
        }
    }
#else
    // Check our direct neighbors for relays that have this destination as downstream
    auto myEdges = routingGraph->getEdgesFrom(myNode);
    if (myEdges) {
        for (const Edge& edge : *myEdges) {
            NodeNum neighbor = edge.to;
            // Check if this neighbor has destination as downstream
            auto it = gatewayDownstream.find(neighbor);
            if (it != gatewayDownstream.end()) {
                for (NodeNum downstream : it->second) {
                    if (downstream == destination) {
                        LOG_INFO("[SR] Found downstream: %08x is downstream of relay %08x (direct neighbor)", destination, neighbor);
                        return true;
                    }
                }
            }
        }
    }
#endif

    return false;
}

uint32_t SignalRoutingModule::getNodeTtlSeconds(CapabilityStatus status) const
{
    // Mute/inactive nodes (Legacy status) get longer TTL
    if (status == CapabilityStatus::Legacy) {
        return MUTE_NODE_TTL_SECS;
    }
    // Active nodes (Capable, Unknown) get shorter TTL
    return ACTIVE_NODE_TTL_SECS;
}

void SignalRoutingModule::logNetworkTopology()
{
    if (!routingGraph) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // LITE mode: use fixed-size arrays only, no heap allocations
    NodeNum nodeBuf[GRAPH_LITE_MAX_NODES];
    size_t rawNodeCount = routingGraph->getAllNodeIds(nodeBuf, GRAPH_LITE_MAX_NODES);

    // Filter out downstream nodes
    size_t nodeCount = 0;
    for (size_t i = 0; i < rawNodeCount; i++) {
        NodeNum nodeId = nodeBuf[i];
        bool isDownstream = false;
        for (uint8_t j = 0; j < gatewayRelationCount; j++) {
            if (gatewayRelations[j].downstream == nodeId) {
                isDownstream = true;
                break;
            }
        }
        if (!isDownstream) {
            nodeBuf[nodeCount++] = nodeId;
        }
    }

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

        CapabilityStatus status = getCapabilityStatus(nodeId);
        const char* prefix = (status == CapabilityStatus::SRactive || status == CapabilityStatus::SRinactive) ? "[SR] " : "";

        const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeId);
        if (!edges || edges->edgeCount == 0) {
            const char* statusStr = (status == CapabilityStatus::SRactive) ? "SR-active" :
                                   (status == CapabilityStatus::SRinactive) ? "SR-inactive" :
                                   (status == CapabilityStatus::Legacy) ? "legacy" : "unknown";
            LOG_INFO("[SR] +- %s%s: no neighbors (%s)", prefix, nodeName, statusStr);
            continue;
        }

        // Count gateway downstreams using fixed iteration (no heap allocation)
        uint8_t downstreamCount = 0;
        uint32_t now = millis() / 1000;  // Use monotonic time
        for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
            const GatewayDownstreamSet &set = gatewayDownstream[i];
            if (set.gateway == nodeId && (now - set.lastSeen) <= ACTIVE_NODE_TTL_SECS) {
                downstreamCount = set.count;
                break;
            }
        }

        if (downstreamCount == 0) {
            LOG_INFO("[SR] +- %s%s: connected to %d nodes", prefix, nodeName, edges->edgeCount);
        } else {
            LOG_INFO("[SR] +- %s%s: connected to %d nodes (gateway for %d nodes)", prefix, nodeName, edges->edgeCount, downstreamCount);
        }

        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            const EdgeLite& edge = edges->edges[i];
            char neighborName[48]; // Reduced buffer size for stack safety
            getNodeDisplayName(edge.to, neighborName, sizeof(neighborName));

            CapabilityStatus neighborStatus = getCapabilityStatus(edge.to);
            const char* prefix = (neighborStatus == CapabilityStatus::SRactive || neighborStatus == CapabilityStatus::SRinactive) ? "[SR] " : "";

            float etx = edge.getEtx();
            const char* quality;
            if (etx < 2.0f) quality = "excellent";
            else if (etx < 4.0f) quality = "good";
            else if (etx < 8.0f) quality = "fair";
            else quality = "poor";

            int32_t age = computeAgeSecs(edges->lastFullUpdate, millis() / 1000);
            char ageBuf[16];
            if (age < 0) {
                snprintf(ageBuf, sizeof(ageBuf), "-");
            } else {
                snprintf(ageBuf, sizeof(ageBuf), "%d", age);
            }

            LOG_INFO("[SR] |  +- %s%s: %s link (ETX=%.1f, %s sec ago)",
                    prefix, neighborName, quality, etx, ageBuf);
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

    // Filter out downstream nodes for topology display
    std::vector<NodeNum> topologyNodes;
    for (NodeNum nodeId : allNodes) {
        bool isDownstream = false;
        for (const auto& relation : downstreamGateway) {
            if (relation.first == nodeId) {
                isDownstream = true;
                break;
            }
        }
        if (!isDownstream) {
            topologyNodes.push_back(nodeId);
        }
    }

    LOG_INFO("[SR] Network Topology: %d nodes total", topologyNodes.size());
    // Sort nodes for consistent output
    std::sort(topologyNodes.begin(), topologyNodes.end());

    for (NodeNum nodeId : topologyNodes) {
        char nodeName[64];
        getNodeDisplayName(nodeId, nodeName, sizeof(nodeName));

        CapabilityStatus status = getCapabilityStatus(nodeId);
        const char* prefix = (status == CapabilityStatus::SRactive || status == CapabilityStatus::SRinactive) ? "[SR] " : "";

        const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeId);
        if (!edges || edges->empty()) {
            const char* statusStr = (status == CapabilityStatus::SRactive) ? "SR-active" :
                                   (status == CapabilityStatus::SRinactive) ? "SR-inactive" :
                                   (status == CapabilityStatus::Legacy) ? "legacy" : "unknown";
            LOG_INFO("[SR] +- %s%s: no neighbors (%s)", prefix, nodeName, statusStr);
            continue;
        }

        std::vector<NodeNum> downstreams;
        appendGatewayDownstreams(nodeId, downstreams);

        if (downstreams.empty()) {
            LOG_INFO("[SR] +- %s%s: connected to %d nodes", prefix, nodeName, edges->size());
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
                snprintf(buf + pos, sizeof(buf) - pos, ", +%u", static_cast<unsigned int>(downstreams.size() - maxList));
            }
            LOG_INFO("[SR] +- %s%s: connected to %d nodes (gateway for %u nodes: %s)", prefix, nodeName, edges->size(), static_cast<unsigned int>(downstreams.size()), buf);
        }

        // Sort edges by ETX for consistent output
        std::vector<Edge> sortedEdges = *edges;
        std::sort(sortedEdges.begin(), sortedEdges.end(),
                 [](const Edge& a, const Edge& b) { return a.etx < b.etx; });

        for (size_t i = 0; i < sortedEdges.size(); i++) {
            const Edge& edge = sortedEdges[i];
            char neighborName[64];
            getNodeDisplayName(edge.to, neighborName, sizeof(neighborName));

            CapabilityStatus neighborStatus = getCapabilityStatus(edge.to);
            const char* prefix = (neighborStatus == CapabilityStatus::SRactive || neighborStatus == CapabilityStatus::SRinactive) ? "[SR] " : "";

            const char* quality;
            if (edge.etx < 2.0f) quality = "excellent";
            else if (edge.etx < 4.0f) quality = "good";
            else if (edge.etx < 8.0f) quality = "fair";
            else quality = "poor";

            int32_t age = computeAgeSecs(edge.lastUpdate, millis() / 1000);
            char ageBuf[16];
            if (age < 0) {
                snprintf(ageBuf, sizeof(ageBuf), "-");
            } else {
                snprintf(ageBuf, sizeof(ageBuf), "%d", age);
            }

            LOG_INFO("[SR] |  +- %s%s: %s link (ETX=%.1f, %s sec ago)",
                    prefix, neighborName, quality, edge.etx, ageBuf);
        }
    }

    // Add legend explaining ETX to signal quality mapping
    LOG_INFO("[SR] ETX to signal mapping: ETX=1.0~RSSI=-60dB/SNR=10dB, ETX=2.0~RSSI=-90dB/SNR=0dB, ETX=4.0~RSSI=-110dB/SNR=-5dB");
    LOG_DEBUG("[SR] Topology logging complete");
#endif
}

ProcessMessage SignalRoutingModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Turn on LED to indicate SR processing active
    // We'll turn it off when this RTOS task completes
    // For now, use a neutral color - will be overridden by specific operations
    setRgbLed(255, 255, 255);  // White for SR active

    // Update node activity for ANY packet reception to keep nodes in graph
    updateNodeActivityForPacketAndRelay(&mp);

    // Update NodeDB with packet information like FloodingRouter does
    if (nodeDB) {
        nodeDB->updateFrom(mp);
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
    
    // Update node activity for ANY packet reception to keep nodes in graph
    if (routingGraph && notViaMqtt) {
        if (hasSignalData && isDirectFromSender) {
            // Real signal data available - use it
            updateNeighborInfo(mp.from, mp.rx_rssi, mp.rx_snr, mp.rx_time);
        } else {
            // Relayed packet - just update node activity timestamp
            routingGraph->updateNodeActivity(mp.from, millis() / 1000);
        }
    }

    if (hasSignalData && notViaMqtt && isDirectFromSender) {
        // Check if this sender matches a known placeholder
        NodeNum placeholderId = getPlaceholderForRelay(fromLastByte);
        if (isPlaceholderNode(placeholderId)) {
            // This sender matches a placeholder - resolve it
            if (resolvePlaceholder(placeholderId, mp.from)) {
                LOG_INFO("[SR] Direct contact: resolved placeholder %08x with node %08x", placeholderId, mp.from);
            }
        }

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

        // Remove this node from ALL gateway relationships since we can hear it directly
        clearDownstreamFromAllGateways(mp.from);

        LOG_INFO("[SR] Direct neighbor %s: RSSI=%d, SNR=%.1f, ETX=%.2f",
                 senderName, mp.rx_rssi, mp.rx_snr, etx);

        // Purple for direct packet received (operation start)
        setRgbLed(128, 0, 128);

        // Record that this node transmitted (for contention window tracking)
        if (routingGraph) {
            uint32_t currentTime = millis() / 1000;  // Use monotonic time
            routingGraph->recordNodeTransmission(mp.from, mp.id, currentTime);
        }

        LOG_DEBUG("[SR] Direct neighbor %s detected (RSSI=%d, SNR=%.1f)",
                 senderName, mp.rx_rssi, mp.rx_snr);
    } else if (notViaMqtt && !isDirectFromSender && mp.relay_node != 0) {
        // Process relayed packets to infer network topology (skip for inactive roles - they only track direct neighbors)
        if (!isActiveRoutingRole()) {
            LOG_DEBUG("[SR] Inactive role: Skipping relayed packet topology inference");
        } else {
            NodeNum inferredRelayer = resolveRelayIdentity(mp.relay_node);

        // If still not resolved, try direct neighbors
        if (inferredRelayer == 0 && routingGraph && nodeDB) {
#ifdef SIGNAL_ROUTING_LITE_MODE
            const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (myEdges) {
                for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                    NodeNum neighbor = myEdges->edges[i].to;
                    if ((neighbor & 0xFF) == mp.relay_node) {
                        inferredRelayer = neighbor;
                        // Remember this mapping for future use
                        rememberRelayIdentity(neighbor, mp.relay_node);
                        LOG_DEBUG("[SR] Resolved relay 0x%02x to direct neighbor %08x",
                                 mp.relay_node, neighbor);
                        break;
                    }
                }
            }
#else
            auto neighbors = routingGraph->getDirectNeighbors(nodeDB->getNodeNum());
            for (NodeNum neighbor : neighbors) {
                if ((neighbor & 0xFF) == mp.relay_node) {
                    inferredRelayer = neighbor;
                    // Remember this mapping for future use
                    rememberRelayIdentity(neighbor, mp.relay_node);
                    LOG_DEBUG("[SR] Resolved relay 0x%02x to direct neighbor %08x",
                             mp.relay_node, neighbor);
                    break;
                }
            }
#endif
        }

        // If we can't resolve the relay identity, create a placeholder node
        if (inferredRelayer == 0) {
            inferredRelayer = createPlaceholderNode(mp.relay_node);
            // Remember this placeholder for future reference
            rememberRelayIdentity(inferredRelayer, mp.relay_node);
            LOG_DEBUG("[SR] Created placeholder %08x for unknown relay 0x%02x",
                     inferredRelayer, mp.relay_node);
        }

        if (inferredRelayer != 0 && inferredRelayer != mp.from) {
            // Remember this relay identity mapping for future use
            rememberRelayIdentity(inferredRelayer, mp.relay_node);

            // We know that inferredRelayer relayed a packet from mp.from
            // This suggests connectivity between mp.from and inferredRelayer
            LOG_DEBUG("[SR] Inferred connectivity: %08x -> %08x (relayed via %02x)",
                     mp.from, inferredRelayer, mp.relay_node);

            // Add a synthetic graph edge to show inferred connectivity through relay
            // Use ETX=2.0 for inferred connections (corresponds to poor connectivity)
            routingGraph->updateEdge(mp.from, inferredRelayer, 2.0f, millis() / 1000);
            LOG_DEBUG("[SR] Added synthetic edge: %08x -> %08x (ETX=2.0%s)",
                     mp.from, inferredRelayer, isPlaceholderNode(inferredRelayer) ? ", placeholder" : "");

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
                uint32_t currentTime = millis() / 1000;  // Use monotonic time
                routingGraph->recordNodeTransmission(mp.from, mp.id, currentTime);
                routingGraph->recordNodeTransmission(inferredRelayer, mp.id, currentTime);
            }
        }  // End of else block for active routing roles relayed packet processing
    }
    }

    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        handleSniffedPayload(mp, isDirectFromSender);
    }

    // Periodic graph maintenance with stability safeguards (CLIENT_MUTE maintains direct neighbors, others do full maintenance)
    if (routingGraph && canSendTopology()) {
        // Use monotonic time (seconds since boot) for aging to avoid RTC sync issues
        uint32_t currentTime = millis() / 1000;
        if (currentTime - lastGraphUpdate > GRAPH_UPDATE_INTERVAL_SECS) {
            uint32_t nodeCountBefore = routingGraph->getNodeCount();
            routingGraph->ageEdges(currentTime);
            uint32_t nodeCountAfter = routingGraph->getNodeCount();
            lastGraphUpdate = currentTime;

            if (nodeCountBefore != nodeCountAfter) {
                LOG_INFO("[SR] Graph aged: %u -> %u nodes", nodeCountBefore, nodeCountAfter);
            } else {
                LOG_DEBUG("[SR] Graph aged (no node count change)");
            }

            // Safety check: ensure we still have our own node
            if (!routingGraph->getEdgesFrom(nodeDB->getNodeNum())) {
                LOG_WARN("[SR] Graph aging removed local node edges - topology unstable");
            }
        }
    }

    return ProcessMessage::CONTINUE;
}

bool SignalRoutingModule::shouldRelayUnicastForCoordination(const meshtastic_MeshPacket *p)
{
    if (!routingGraph || !nodeDB) {
        return false;
    }

    NodeNum myNode = nodeDB->getNodeNum();
    NodeNum destination = p->to;
    NodeNum sourceNode = p->from;
    NodeNum heardFrom = resolveHeardFrom(p, sourceNode);

    char destName[64];
    getNodeDisplayName(destination, destName, sizeof(destName));

    // GATEWAY OVERRIDE: If we are the gateway for the destination, ALWAYS relay
    // This ensures downstream nodes that are exclusively connected to us can be reached
    NodeNum gatewayForDest = getGatewayFor(destination);
    if (gatewayForDest == myNode) {
        LOG_INFO("[SR] UNICAST RELAY: We are gateway for %s - ALWAYS relay to ensure reachability", destName);
        return true;
    }

    // Check if the broadcasting node has direct connectivity to our calculated next hop or the target
    // If so, the broadcasting node should have sent directly instead of broadcasting for coordination
    NodeNum nextHop = getNextHop(destination, sourceNode, heardFrom, false);
    if (nextHop != 0) {
        // Check if broadcasting node has direct connectivity to our next hop
        if (hasDirectConnectivity(heardFrom, nextHop)) {
            char broadcastingName[64], nextHopName[64];
            getNodeDisplayName(heardFrom, broadcastingName, sizeof(broadcastingName));
            getNodeDisplayName(nextHop, nextHopName, sizeof(nextHopName));
            LOG_DEBUG("[SR] UNICAST RELAY: Broadcasting node %s has direct connection to next hop %s - broadcasting node should have sent directly",
                     broadcastingName, nextHopName);
            return false; // Don't relay - broadcasting node should have handled this directly
        }

        // Check if broadcasting node has direct connectivity to the target
        if (hasDirectConnectivity(heardFrom, destination)) {
            char broadcastingName[64], destName[64];
            getNodeDisplayName(heardFrom, broadcastingName, sizeof(broadcastingName));
            getNodeDisplayName(destination, destName, sizeof(destName));
            LOG_DEBUG("[SR] UNICAST RELAY: Broadcasting node %s has direct connection to target %s - broadcasting node should have sent directly",
                     broadcastingName, destName);
            return false; // Don't relay - broadcasting node should have handled this directly
        }
    }

    // Check if the sending node (heardFrom) has a direct connection to the destination
    // If so, the destination should have already received this packet directly, so don't relay
#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* senderEdges = routingGraph->getEdgesFrom(heardFrom);
    if (senderEdges) {
        for (uint8_t i = 0; i < senderEdges->edgeCount; i++) {
            if (senderEdges->edges[i].to == destination) {
                LOG_DEBUG("[SR] UNICAST RELAY: Sender %08x has direct connection to %s (ETX=%.2f) - destination already received directly",
                         heardFrom, destName, senderEdges->edges[i].getEtx());
                return false; // Don't relay - destination should have received it directly
            }
        }
    }
#else
    const std::vector<Edge>* senderEdges = routingGraph->getEdgesFrom(heardFrom);
    if (senderEdges) {
        for (const Edge& edge : *senderEdges) {
            if (edge.to == destination) {
                LOG_DEBUG("[SR] UNICAST RELAY: Sender %08x has direct connection to %s (ETX=%.2f) - destination already received directly",
                         heardFrom, destName, edge.etx);
                return false; // Don't relay - destination should have received it directly
            }
        }
    }
#endif

    // Check if destination is a downstream node of relays we can hear directly
    // If so, broadcast the unicast for relay coordination to ensure delivery
    if (isDownstreamOfHeardRelay(destination, myNode)) {
        LOG_INFO("[SR] UNICAST RELAY: Broadcasting unicast to %s - downstream of relay we can hear", destName);
        return true; // Broadcast for coordination
    }

    // Check if any legacy routers that heard this packet should relay instead
    // Legacy routers/repeaters get priority as they are meant to always rebroadcast
#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* transmittingEdges = routingGraph->getEdgesFrom(heardFrom);
    if (transmittingEdges) {
        for (uint8_t i = 0; i < transmittingEdges->edgeCount; i++) {
            NodeNum candidate = transmittingEdges->edges[i].to;
            if (candidate != myNode && isLegacyRouter(candidate)) {
                // Check if this legacy router can reach the destination
                RouteLite route = routingGraph->calculateRoute(destination, millis() / 1000,
                    [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
                if (route.nextHop != 0) {
                    char legacyName[64];
                    getNodeDisplayName(candidate, legacyName, sizeof(legacyName));
                    LOG_DEBUG("[SR] Legacy router %s should relay unicast to %s instead of us", legacyName, destName);
                    return false; // Let the legacy router handle it
                }
            }
        }
    }
#else
    auto transmittingEdges = routingGraph->getEdgesFrom(heardFrom);
    if (transmittingEdges) {
        for (const Edge& edge : *transmittingEdges) {
            NodeNum candidate = edge.to;
            if (candidate != myNode && isLegacyRouter(candidate)) {
                // Check if this legacy router can reach the destination
                Route route = routingGraph->calculateRoute(destination, millis() / 1000,
                    [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
                if (route.nextHop != 0) {
                    char legacyName[64];
                    getNodeDisplayName(candidate, legacyName, sizeof(legacyName));
                    LOG_DEBUG("[SR] Legacy router %s should relay unicast to %s instead of us", legacyName, destName);
                    return false; // Let the legacy router handle it
                }
            }
        }
    }
#endif

    // Calculate our route cost to the destination
    float ourRouteCost = 0.0f;
    bool haveRoute = false;

#ifdef SIGNAL_ROUTING_LITE_MODE
    RouteLite ourRoute = routingGraph->calculateRoute(destination, millis() / 1000,
                        [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
    haveRoute = ourRoute.nextHop != 0;
    ourRouteCost = ourRoute.getCost();
#else
    Route ourRoute = routingGraph->calculateRoute(destination, millis() / 1000,
                      [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
    haveRoute = ourRoute.nextHop != 0;
    ourRouteCost = ourRoute.cost;
#endif

    if (!haveRoute) {
        LOG_DEBUG("[SR] No route to unicast destination %s - not relaying", destName);
        return false;
    }

    LOG_DEBUG("[SR] Our route cost to %s: %.2f", destName, ourRouteCost);

    // Check if the node we heard this from has a better route to the destination
    // If they do, they should relay instead of us
    if (heardFrom != sourceNode && heardFrom != myNode) {
        // Check if heardFrom has a direct connection to destination with better ETX
        bool heardFromHasBetterDirect = false;
        float heardFromDirectEtx = 1e9f;

#ifdef SIGNAL_ROUTING_LITE_MODE
        const NodeEdgesLite* heardFromEdges = routingGraph->getEdgesFrom(heardFrom);
        if (heardFromEdges) {
            for (uint8_t i = 0; i < heardFromEdges->edgeCount; i++) {
                if (heardFromEdges->edges[i].to == destination) {
                    heardFromDirectEtx = heardFromEdges->edges[i].getEtx();
                    if (heardFromDirectEtx < ourRouteCost - 1.0f) {
                        heardFromHasBetterDirect = true;
                    }
                    break;
                }
            }
        }
#else
        auto heardFromEdges = routingGraph->getEdgesFrom(heardFrom);
        if (heardFromEdges) {
            for (const Edge& e : *heardFromEdges) {
                if (e.to == destination) {
                    heardFromDirectEtx = e.etx;
                    if (heardFromDirectEtx < ourRouteCost - 1.0f) {
                        heardFromHasBetterDirect = true;
                    }
                    break;
                }
            }
        }
#endif

        if (heardFromHasBetterDirect) {
            char heardFromName[64];
            getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));
            LOG_DEBUG("[SR] Node %s has better direct connection (ETX=%.2f) to %s than our route (%.2f) - not relaying",
                     heardFromName, heardFromDirectEtx, destName, ourRouteCost);
            return false;
        }
    }

    // We're the best positioned node, so we should relay
    LOG_DEBUG("[SR] We are best positioned for unicast to %s - relaying", destName);

    // Turn off LED when RTOS task completes
    turnOffRgbLed();
    return true;
}

bool SignalRoutingModule::shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p)
{
    if (!p || !signalBasedRoutingEnabled || !routingGraph || !nodeDB) {
        LOG_DEBUG("[SR] SR disabled or unavailable (enabled=%d, graph=%p, nodeDB=%p)",
                 signalBasedRoutingEnabled, routingGraph, nodeDB);
        return false;
    }

    // Update SR graph timestamps for any packet we process (including duplicates)
    updateNodeActivityForPacketAndRelay(p);

    // If the packet wasn't decrypted, still consider SR but note we are routing opaque payload.
    // This can happen for duplicates that weren't decrypted by the Router
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        LOG_INFO("[SR] Packet not decrypted - routing header only analysis");
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
                                status == CapabilityStatus::SRactive ? "SR-active" :
                                status == CapabilityStatus::SRinactive ? "SR-inactive" :
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

    // Check if we have any route to the destination - if so, use SR coordination for unicast relay
    // SR's coordinated delivery algorithm will determine the best relay candidates
    float routeCost = 0.0f;
    bool haveAnyRoute = false;

#ifdef SIGNAL_ROUTING_LITE_MODE
    RouteLite route = routingGraph ? routingGraph->calculateRoute(p->to, millis() / 1000,
                        [this](NodeNum nodeId) { return isNodeRoutable(nodeId); }) : RouteLite();
#else
    Route route = routingGraph ? routingGraph->calculateRoute(p->to, millis() / 1000,
                      [this](NodeNum nodeId) { return isNodeRoutable(nodeId); }) : Route();
#endif

    if (route.nextHop != 0) {
        routeCost = route.getCost();
        haveAnyRoute = true;
    }

    if (haveAnyRoute) {
        LOG_DEBUG("[SR] We have route to %s (cost: %.2f) - using SR coordination for unicast relay", destName, routeCost);
        return true; // Use SR coordination for unicast relay to destination
    }

    bool topologyHealthy = topologyHealthyForUnicast(p->to);
    LOG_DEBUG("[SR] Unicast topology %s for destination",
             topologyHealthy ? "HEALTHY" : "unhealthy");

    if (!topologyHealthy) {
        LOG_DEBUG("[SR] Destination node not known - not relaying unicast packet to avoid network flooding");
        LOG_DEBUG("[SR] Unknown destination - discovery occurs via broadcasts, direct packets, or relayed packet inference");

        // Don't broadcast unicast packets for unknown destinations to prevent flooding
        // The destination node should exist and broadcast its presence for us to learn about it
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
    if (!routingGraph || !nodeDB) {
        return true;
    }

    // Special handling for unicast packets being relayed with SR coordination
    if (!isBroadcast(p->to)) {
        // This is a unicast packet being relayed with SR coordination
        // Relay decision should be based on our ability to reach the destination
        return shouldRelayUnicastForCoordination(p);
    }

    if (!isActiveRoutingRole()) {
        return false;
    }

    if (!topologyHealthyForBroadcast()) {
        return true;
    }

    // Compute packet received timestamp once for all SignalRouting operations
    uint32_t packetReceivedTimestamp = millis() / 1000;

    // Only access decoded fields if packet is actually decoded
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        p->decoded.portnum == meshtastic_PortNum_SIGNAL_ROUTING_APP) {
        preProcessSignalRoutingPacket(p, packetReceivedTimestamp);
    }

    NodeNum myNode = nodeDB->getNodeNum();
    NodeNum sourceNode = p->from;
    NodeNum heardFrom = resolveHeardFrom(p, sourceNode);

    // Gateway awareness: check if WE are the recorded gateway for the source or destination
    NodeNum gatewayForSource = getGatewayFor(sourceNode);
    NodeNum gatewayForDest = getGatewayFor(p->to);
    bool weAreGatewayForSource = (gatewayForSource != 0 && gatewayForSource == myNode);
    bool weAreGatewayForDest = (gatewayForDest != 0 && gatewayForDest == myNode);
    size_t downstreamCount = (weAreGatewayForSource || weAreGatewayForDest) ? getGatewayDownstreamCount(myNode) : 0;

    uint32_t currentTime = packetReceivedTimestamp; // Use the packet received timestamp computed above

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
    bool shouldRelay = routingGraph->shouldRelayEnhanced(myNode, sourceNode, heardFrom, currentTime, p->id, packetReceivedTimestamp);

    // Apply conservative logic only when NOT required for branch coverage
    if (shouldRelay && hasStockGateways && !mustRelayForBranchCoverage) {
        LOG_DEBUG("[SR] Applying conservative relay logic (stock gateways present, not from gateway)");
        shouldRelay = routingGraph->shouldRelayEnhancedConservative(myNode, sourceNode, heardFrom, currentTime, p->id, packetReceivedTimestamp);
        if (!shouldRelay) {
            LOG_DEBUG("[SR] Suppressed SR relay - stock gateway can handle external transmission");
        } else {
            LOG_DEBUG("[SR] SR relay proceeding despite conservative logic");
        }
    }
#else
    bool shouldRelay = routingGraph->shouldRelayEnhanced(myNode, sourceNode, heardFrom, currentTime, p->id, packetReceivedTimestamp);

    // Apply conservative logic only when NOT required for branch coverage
    if (shouldRelay && hasStockGateways && !mustRelayForBranchCoverage) {
        LOG_DEBUG("[SR] Applying conservative relay logic (stock gateways present, not from gateway)");
        shouldRelay = routingGraph->shouldRelayEnhancedConservative(myNode, sourceNode, heardFrom, currentTime, p->id, packetReceivedTimestamp);
        if (!shouldRelay) {
            LOG_DEBUG("[SR] Suppressed SR relay - stock gateway provides better external coverage");
        } else {
            LOG_DEBUG("[SR] SR relay proceeding despite conservative logic");
        }
    }
#endif

    // Gateway override: force relay if we are gateway for source OR destination
    if (!shouldRelay && (weAreGatewayForSource || weAreGatewayForDest)) {
        NodeNum forcedFor = weAreGatewayForSource ? sourceNode : p->to;
        LOG_INFO("[SR] We are gateway for %08x (downstream=%u) -> force relay", forcedFor, static_cast<unsigned int>(downstreamCount));
        shouldRelay = true;
    }

    // Unified Coverage Logic: Ensure both SR coordination and stock coverage requirements are met
    // SR coordination provides efficient unique coverage between SR nodes
    // Stock coverage provides guaranteed coverage for stock firmware nodes

    bool srCoordinationRequiresRelay = shouldRelay;

    if (!srCoordinationRequiresRelay) {
        // Check if stock nodes need guaranteed coverage
        bool stockCoverageNeeded = shouldRelayForStockNeighbors(myNode, sourceNode, heardFrom, currentTime);
        if (stockCoverageNeeded) {
            LOG_INFO("[SR] UNIFIED COVERAGE: SR coordination declined relay, but stock nodes require guaranteed coverage");
            shouldRelay = true;
        } else {
            LOG_DEBUG("[SR] UNIFIED COVERAGE: No relay needed - SR coordination satisfied and stock nodes covered");
        }
    } else {
        LOG_DEBUG("[SR] UNIFIED COVERAGE: Relay required by SR coordination");
    }

    char myName[64], sourceName[64], heardFromName[64];
    getNodeDisplayName(myNode, myName, sizeof(myName));
    getNodeDisplayName(sourceNode, sourceName, sizeof(sourceName));
    getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));

    const char* decisionReason = srCoordinationRequiresRelay ?
        "SR coordination requires relay" :
        "Stock coverage requires relay";

    LOG_INFO("[SR] Broadcast from %s (heard via %s): %s relay (%s)",
             sourceName, heardFromName, shouldRelay ? "SHOULD" : "should NOT",
             shouldRelay ? decisionReason : "No relay needed");

    if (shouldRelay) {
        routingGraph->recordNodeTransmission(myNode, p->id, currentTime);
        setRgbLed(255, 128, 0);  // Orange for relaying
    } else {
        setRgbLed(255, 0, 0);    // Red for not relaying
    }

    return shouldRelay;
}

NodeNum SignalRoutingModule::getNextHop(NodeNum destination, NodeNum sourceNode, NodeNum heardFrom, bool allowOpportunistic)
{
    if (!routingGraph) {
        LOG_DEBUG("[SR] No graph available for routing");
        return 0;
    }

    uint32_t currentTime = millis() / 1000;  // Use monotonic time for consistency

    char destName[64];
    getNodeDisplayName(destination, destName, sizeof(destName));

#ifdef SIGNAL_ROUTING_LITE_MODE
    RouteLite route = routingGraph->calculateRoute(destination, currentTime,
                        [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
#else
    Route route = routingGraph->calculateRoute(destination, currentTime,
                      [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
#endif

    float routeCost = route.getCost();

    if (route.nextHop != 0) {
        char nextHopName[64];
        getNodeDisplayName(route.nextHop, nextHopName, sizeof(nextHopName));

        LOG_DEBUG("[SR] Route to %s via %s (cost: %.2f)",
                 destName, nextHopName, routeCost);

        if (routeCost > 10.0f) {
            LOG_WARN("[SR] High-cost route to %s (%.2f) - poor link quality expected",
                    destName, routeCost);
        }

        // Even if we have a route, check if any neighbor has a significantly better route
        // This ensures unicasts are forwarded to better-positioned nodes
        if (allowOpportunistic && routeCost > 2.0f) { // Only check if our route is not excellent
            NodeNum betterNeighbor = findBetterPositionedNeighbor(destination, sourceNode, heardFrom, routeCost, currentTime);
            if (betterNeighbor != 0) {
                return betterNeighbor;
            }
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

    // Fallback 2: opportunistic forward — find neighbor with better position for destination
    // Only do this if opportunistic forwarding is allowed
    if (allowOpportunistic) {
        NodeNum betterNeighbor = findBetterPositionedNeighbor(destination, sourceNode, heardFrom,
                                                            std::numeric_limits<float>::infinity(), currentTime);
        if (betterNeighbor != 0) {
            return betterNeighbor;
        }
    }

    // Fallback 3: Special case for unicast packets to direct neighbors that didn't hear the transmission
    // If we received this as a relayed packet (heardFrom != sourceNode) and destination is our direct neighbor,
    // we should deliver it directly since the destination didn't hear the original transmission
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    if (routingGraph && nodeDB && myNode != 0 && heardFrom != sourceNode) {
        bool isDirectNeighbor = false;
        float directEtx = 1e9f;

#ifdef SIGNAL_ROUTING_LITE_MODE
        const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
        if (myEdges) {
            for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                if (myEdges->edges[i].to == destination) {
                    isDirectNeighbor = true;
                    directEtx = myEdges->edges[i].getEtx();
                    break;
                }
            }
        }
#else
        auto myEdges = routingGraph->getEdgesFrom(myNode);
        if (myEdges) {
            for (const Edge& e : *myEdges) {
                if (e.to == destination) {
                    isDirectNeighbor = true;
                    directEtx = e.etx;
                    break;
                }
            }
        }
#endif

        if (isDirectNeighbor) {
            LOG_DEBUG("[SR] Delivering unicast to direct neighbor %s (ETX=%.2f) since destination didn't hear transmission",
                     destName, directEtx);
            return destination; // Deliver directly to our neighbor
        }
    }

    // Fallback 4: if we are recorded as a gateway for this destination, we can deliver directly
    // This handles true gateway scenarios where we have unique connectivity that other SR nodes don't
    if (getGatewayFor(destination) == myNode) {
        LOG_INFO("[SR] We are the designated gateway for %s - delivering directly", destName);
        // Refresh the gateway relationship since we're actively using it
        recordGatewayRelation(myNode, destination);
        return destination; // We are the gateway, deliver directly
    }

    // Fallback 5: if the destination only has us as a neighbor (effective gateway scenario),
    // we should try to deliver directly even without formal gateway designation
    // This handles cases like FMC6 where a node only connects through us
#ifdef SIGNAL_ROUTING_LITE_MODE
    if (routingGraph && nodeDB) {
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

NodeNum SignalRoutingModule::findBetterPositionedNeighbor(NodeNum destination, NodeNum sourceNode, NodeNum heardFrom,
                                                         float ourRouteCost, uint32_t currentTime) {
    if (!routingGraph || !nodeDB) {
        return 0;
    }

    NodeNum myNode = nodeDB->getNodeNum();
    NodeNum bestNeighbor = 0;
    float bestNeighborRouteCost = ourRouteCost;

#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
    if (!myEdges) {
        return 0;
    }

    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        NodeNum neighbor = myEdges->edges[i].to;

        // Don't forward back to source or heardFrom nodes
        if (neighbor == sourceNode || neighbor == heardFrom) {
            continue;
        }

        // Check if this neighbor has a direct connection to the destination
        const NodeEdgesLite* neighborEdges = routingGraph->getEdgesFrom(neighbor);
        if (neighborEdges) {
            for (uint8_t j = 0; j < neighborEdges->edgeCount; j++) {
                if (neighborEdges->edges[j].to == destination) {
                    float directEtx = neighborEdges->edges[j].getEtx();
                    // If neighbor has a direct connection that's significantly better than our route cost,
                    // forward to them. Direct connection ETX should be much better than multi-hop route cost.
                    if (directEtx < ourRouteCost - 1.0f && directEtx < bestNeighborRouteCost) {
                        bestNeighbor = neighbor;
                        bestNeighborRouteCost = directEtx;
                    }
                    break;
                }
            }
        }
    }
#else
    auto myEdges = routingGraph->getEdgesFrom(myNode);
    if (!myEdges) {
        return 0;
    }

    for (const Edge& myEdge : *myEdges) {
        NodeNum neighbor = myEdge.to;

        // Don't forward back to source or heardFrom nodes
        if (neighbor == sourceNode || neighbor == heardFrom) {
            continue;
        }

        // Check if this neighbor has a direct connection to the destination
        auto neighborEdges = routingGraph->getEdgesFrom(neighbor);
        if (neighborEdges) {
            for (const Edge& neighborEdge : *neighborEdges) {
                if (neighborEdge.to == destination) {
                    // If neighbor has a direct connection that's significantly better than our route cost,
                    // forward to them. Direct connection ETX should be much better than multi-hop route cost.
                    if (neighborEdge.etx < ourRouteCost - 1.0f && neighborEdge.etx < bestNeighborRouteCost) {
                        bestNeighbor = neighbor;
                        bestNeighborRouteCost = neighborEdge.etx;
                    }
                    break;
                }
            }
        }
    }
#endif

    if (bestNeighbor != 0) {
        char nhName[64], destName[64];
        getNodeDisplayName(bestNeighbor, nhName, sizeof(nhName));
        getNodeDisplayName(destination, destName, sizeof(destName));
        LOG_DEBUG("[SR] Found better positioned neighbor %s for %s (our cost: %.2f, neighbor direct ETX: %.2f)",
                 nhName, destName, ourRouteCost, bestNeighborRouteCost);
    }

    return bestNeighbor;
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
            // Set green for new neighbor (operation start)
            setRgbLed(0, 255, 0);
            LOG_INFO("[SR] Topology changed: new neighbor %s (total nodes: %u)", neighborName, static_cast<unsigned int>(routingGraph->getNodeCount()));
            logNetworkTopology();
        } else if (changeType == Graph::EDGE_SIGNIFICANT_CHANGE) {
            // Set blue for signal quality change (operation start)
            setRgbLed(0, 0, 255);
            LOG_INFO("[SR] Topology changed: ETX change for %s (total nodes: %u)", neighborName, static_cast<unsigned int>(routingGraph->getNodeCount()));
            logNetworkTopology();
        }

        // Trigger early broadcast if we haven't sent recently (rate limit: 60s)
        uint32_t now = millis();
        if (now - lastBroadcast > 60 * 1000) {
            setIntervalFromNow(EARLY_BROADCAST_DELAY_MS); // Send update soon (configurable)
        }
    }
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
    return status == CapabilityStatus::SRactive;
}

void SignalRoutingModule::updateNodeActivityForPacket(NodeNum nodeId)
{
    if (routingGraph) {
        routingGraph->updateNodeActivity(nodeId, millis() / 1000);
    }
}

void SignalRoutingModule::updateNodeActivityForPacketAndRelay(const meshtastic_MeshPacket *p)
{
    if (!routingGraph || !nodeDB) return;

    uint32_t currentTime = millis() / 1000;
    NodeNum ourNodeId = nodeDB->getNodeNum();

    // Update original sender activity
    routingGraph->updateNodeActivity(p->from, currentTime);

    // Update relay node activity if this is a relayed packet
    // Only update if relay node is not us and not the sender (safety checks)
    if (p->relay_node != 0) {
        NodeNum relayNodeId = resolveRelayIdentity(p->relay_node);
        if (relayNodeId != 0 && relayNodeId != ourNodeId && relayNodeId != p->from) {
            routingGraph->updateNodeActivity(relayNodeId, currentTime);
        }
    }
}

float SignalRoutingModule::getSignalBasedCapablePercentage() const
{
    if (!nodeDB) {
        return 0.0f;
    }

    uint32_t now = millis() / 1000;
    size_t total = 1;   // include ourselves
    size_t capable = 1; // we are always capable

    size_t nodeCount = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < nodeCount; ++i) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == nodeDB->getNodeNum()) {
            continue;
        }
        if (node->last_heard == 0 || (now - node->last_heard) > ACTIVE_NODE_TTL_SECS) {
            continue;
        }
        total++;
        if (getCapabilityStatus(node->num) == CapabilityStatus::SRactive) {
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
void SignalRoutingModule::setRgbLed(uint8_t r, uint8_t g, uint8_t b)
{
#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
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
#endif
}

void SignalRoutingModule::turnOffRgbLed()
{
#if defined(RGBLED_RED) && defined(RGBLED_GREEN) && defined(RGBLED_BLUE)
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
#endif
}

void SignalRoutingModule::handleNodeInfoPacket(const meshtastic_MeshPacket &mp)
{
    meshtastic_User user = meshtastic_User_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &user)) {
        return;
    }

    // Only update capability status if the node is not already known to be SR-capable
    // NodeInfo packets don't contain SR capability info, so don't downgrade from SR-active/inactive to Unknown
    CapabilityStatus currentStatus = getCapabilityStatus(mp.from);
    if (currentStatus != CapabilityStatus::SRactive && currentStatus != CapabilityStatus::SRinactive) {
        CapabilityStatus status = capabilityFromRole(user.role);
        if (status != CapabilityStatus::Unknown) {
            trackNodeCapability(mp.from, status);
        }
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
    if (currentStatus != CapabilityStatus::Unknown) {
        // Only refresh timestamp for nodes with known capability status
        // Unknown nodes stay unknown until they prove their capability via SR packets
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

        // Check for placeholder resolution in route_request hops
        // Only resolve if the hop node is a direct neighbor of ours
        for (size_t i = 0; i < routing.route_request.route_count; i++) {
            NodeNum hopNode = routing.route_request.route[i];
            uint8_t hopLastByte = hopNode & 0xFF;
            NodeNum placeholderId = getPlaceholderForRelay(hopLastByte);
            if (isPlaceholderNode(placeholderId) && isPlaceholderConnectedToUs(placeholderId)) {
                // Additional check: the hop node must be a direct neighbor of ours
                bool isDirectNeighbor = false;
                NodeNum ourNode = nodeDB->getNodeNum();
#ifdef SIGNAL_ROUTING_LITE_MODE
                const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
                if (ourEdges) {
                    for (uint8_t j = 0; j < ourEdges->edgeCount; j++) {
                        if (ourEdges->edges[j].to == hopNode) {
                            isDirectNeighbor = true;
                            break;
                        }
                    }
                }
#else
                const std::vector<Edge>* ourEdges = routingGraph->getEdgesFrom(ourNode);
                if (ourEdges) {
                    for (const Edge& edge : *ourEdges) {
                        if (edge.to == hopNode) {
                            isDirectNeighbor = true;
                            break;
                        }
                    }
                }
#endif
                if (isDirectNeighbor) {
                    LOG_INFO("[SR] Traceroute resolution: placeholder %08x -> %08x (direct neighbor in route_request)", placeholderId, hopNode);
                    resolvePlaceholder(placeholderId, hopNode);
                } else {
                    LOG_DEBUG("[SR] Skipping traceroute resolution: %08x is not a direct neighbor", hopNode);
                }
            }
        }
        break;
    case meshtastic_Routing_route_reply_tag:
        LOG_DEBUG("[SR] Routing reply from %s for %u hops", senderName, routing.route_reply.route_back_count);

        // Check for placeholder resolution in route_reply hops
        // Only resolve if the hop node is a direct neighbor of ours
        for (size_t i = 0; i < routing.route_reply.route_back_count; i++) {
            NodeNum hopNode = routing.route_reply.route_back[i];
            uint8_t hopLastByte = hopNode & 0xFF;
            NodeNum placeholderId = getPlaceholderForRelay(hopLastByte);
            if (isPlaceholderNode(placeholderId) && isPlaceholderConnectedToUs(placeholderId)) {
                // Additional check: the hop node must be a direct neighbor of ours
                bool isDirectNeighbor = false;
                NodeNum ourNode = nodeDB->getNodeNum();
#ifdef SIGNAL_ROUTING_LITE_MODE
                const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
                if (ourEdges) {
                    for (uint8_t j = 0; j < ourEdges->edgeCount; j++) {
                        if (ourEdges->edges[j].to == hopNode) {
                            isDirectNeighbor = true;
                            break;
                        }
                    }
                }
#else
                const std::vector<Edge>* ourEdges = routingGraph->getEdgesFrom(ourNode);
                if (ourEdges) {
                    for (const Edge& edge : *ourEdges) {
                        if (edge.to == hopNode) {
                            isDirectNeighbor = true;
                            break;
                        }
                    }
                }
#endif
                if (isDirectNeighbor) {
                    LOG_INFO("[SR] Traceroute resolution: placeholder %08x -> %08x (direct neighbor in route_reply)", placeholderId, hopNode);
                    resolvePlaceholder(placeholderId, hopNode);
                } else {
                    LOG_DEBUG("[SR] Skipping traceroute resolution: %08x is not a direct neighbor", hopNode);
                }
            }
        }
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

bool SignalRoutingModule::canSendTopology() const
{
    // Returns true if this node can send topology broadcasts
    // This includes active routing roles plus CLIENT_MUTE
    return isActiveRoutingRole() || config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
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

    uint32_t now = millis() / 1000;  // Use monotonic time for TTL calculations

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: linear search in fixed array
    for (uint8_t i = 0; i < capabilityRecordCount; i++) {
        if (capabilityRecords[i].nodeId == nodeId) {
            capabilityRecords[i].record.lastUpdated = now;
            if (status == CapabilityStatus::SRactive || status == CapabilityStatus::SRinactive) {
                capabilityRecords[i].record.status = status;
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

    if (status == CapabilityStatus::SRactive || status == CapabilityStatus::SRinactive) {
        record.status = status;
    } else if (status == CapabilityStatus::Legacy) {
        record.status = CapabilityStatus::Legacy;
    } else if (record.status == CapabilityStatus::Unknown) {
        record.status = status;
    }
#endif
}

void SignalRoutingModule::pruneCapabilityCache(uint32_t nowSecs)
{
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: remove stale entries by swapping with last
    for (uint8_t i = 0; i < capabilityRecordCount;) {
        // Never prune our own node's capability record
        if (capabilityRecords[i].nodeId == myNode) {
            i++;
            continue;
        }

        uint32_t ttl = getNodeTtlSeconds(capabilityRecords[i].record.status);
        if ((nowSecs - capabilityRecords[i].record.lastUpdated) > ttl) {
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
        // Never prune our own node's capability record
        if (it->first == myNode) {
            ++it;
            continue;
        }

        uint32_t ttl = getNodeTtlSeconds(it->second.status);
        if ((nowSecs - it->second.lastUpdated) > ttl) {
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
        if ((nowSecs - gatewayRelations[i].lastSeen) > ACTIVE_NODE_TTL_SECS) {
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
        if ((nowSecs - gatewayDownstream[i].lastSeen) > ACTIVE_NODE_TTL_SECS) {
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
    // Full mode: remove gateway relations when gateways or downstream nodes are removed from graph
    // When removing a gateway, only remove downstream relations if no alternative gateway exists
    for (auto it = downstreamGateway.begin(); it != downstreamGateway.end();) {
        NodeNum gatewayId = it->second.gateway;
        NodeNum downstreamId = it->first;

        bool gatewayExists = routingGraph && routingGraph->getAllNodes().count(gatewayId) > 0;
        bool downstreamExists = routingGraph && routingGraph->getAllNodes().count(downstreamId) > 0;

        bool shouldRemove = false;

        if (!downstreamExists) {
            // Downstream node is gone - always remove the relation
            shouldRemove = true;
        } else if (!gatewayExists) {
            // Gateway is gone - check if downstream has alternative gateways
            bool hasAlternativeGateway = false;

            // Check if this downstream node has any other gateway relations
            for (const auto& other : downstreamGateway) {
                if (other.first == downstreamId && other.second.gateway != gatewayId) {
                    // Check if this alternative gateway still exists
                    if (routingGraph->getAllNodes().count(other.second.gateway) > 0) {
                        hasAlternativeGateway = true;
                        break;
                    }
                }
            }

            if (!hasAlternativeGateway) {
                // No alternative gateway available - remove the relation
                shouldRemove = true;
            }
            // If hasAlternativeGateway is true, keep the relation
        }

        if (shouldRemove) {
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
            LOG_DEBUG("[SR] Pruned gateway relation (%s is gateway for %s) - %s",
                     gatewayName, downstreamName,
                     !gatewayExists ? "gateway removed from graph" : "downstream removed from graph");
            continue;
        }
        ++it;
    }
#endif
}

SignalRoutingModule::CapabilityStatus SignalRoutingModule::getCapabilityStatus(NodeNum nodeId) const
{
    uint32_t now = millis() / 1000;  // Use monotonic time for TTL calculations

    // Special case: local node capability based on its role
    if (nodeDB && nodeId == nodeDB->getNodeNum()) {
        if (!signalBasedRoutingEnabled) {
            return CapabilityStatus::Legacy;  // SR module disabled
        }

        if (isActiveRoutingRole()) {
            return CapabilityStatus::SRactive;  // Active routing roles are SR-active
        } else if (config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE) {
            return CapabilityStatus::SRinactive;  // CLIENT_MUTE is SR-aware but doesn't relay
        } else {
            return CapabilityStatus::Legacy;  // Fully mute roles don't participate in SR
        }
    }

    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: linear search
    for (uint8_t i = 0; i < capabilityRecordCount; i++) {
        if (capabilityRecords[i].nodeId == nodeId) {
            // Never return Unknown for our own node
            if (capabilityRecords[i].nodeId == myNode) {
                return capabilityRecords[i].record.status;
            }

            uint32_t ttl = getNodeTtlSeconds(capabilityRecords[i].record.status);
            uint32_t age = now - capabilityRecords[i].record.lastUpdated;
            LOG_DEBUG("[SR] Node %08x record found: status=%d, age=%u, ttl=%u",
                      nodeId, (int)capabilityRecords[i].record.status, age, ttl);
            if (age > ttl) {
                LOG_DEBUG("[SR] Node %08x record expired", nodeId);
                return CapabilityStatus::Unknown;
            }
            return capabilityRecords[i].record.status;
        }
    }
    LOG_DEBUG("[SR] Node %08x record not found", nodeId);
    return CapabilityStatus::Unknown;
#else
    auto it = capabilityRecords.find(nodeId);
    if (it == capabilityRecords.end()) {
        return CapabilityStatus::Unknown;
    }

    // Never return Unknown for our own node
    if (it->first == myNode) {
        return it->second.status;
    }

    uint32_t ttl = getNodeTtlSeconds(it->second.status);
    if ((now - it->second.lastUpdated) > ttl) {
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

/**
 * Check if a node is routable (can be used as intermediate hop for routing)
 * Mute nodes are not routable since they don't relay packets
 */
bool SignalRoutingModule::isNodeRoutable(NodeNum nodeId) const {
    // Mute nodes don't relay, so they cannot be used as intermediate routing hops
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
        nodeId == nodeDB->getNodeNum()) {
        // Local mute node - not routable
        return false;
    }

    // Check if this is a known mute node (signal_based_capable = false)
    // For remote nodes, we use the capability tracking
    CapabilityStatus status = getCapabilityStatus(nodeId);
    if (status == CapabilityStatus::Legacy) {
        // Legacy nodes participate in SR routing only if they are routers/repeaters
        // that will actually relay packets
        return isLegacyRouter(nodeId);
    }

    return true;
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
        if (status == CapabilityStatus::SRactive || status == CapabilityStatus::Unknown) {
            capableNeighbors++;
        } else if (isLegacyRouter(to)) {
            capableNeighbors++;
        }
    }
#else
    const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!edges || edges->empty()) {
        LOG_DEBUG("[SR] No edges found for local node, topology not ready");
        return false; // No direct neighbors at all
    }

    // Count how many direct neighbors are SR-capable or potentially capable (unknown status)
    LOG_DEBUG("[SR] Counting capable neighbors");
    size_t capableNeighbors = 0;
    for (const Edge& edge : *edges) {
        CapabilityStatus status = getCapabilityStatus(edge.to);
        if (status == CapabilityStatus::SRactive || status == CapabilityStatus::Unknown) {
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

    // For unicast, we need to know that the destination is reachable through the topology graph
    // The NodeDB check is unreliable due to RTC sync issues (last_heard may be 0)
    // If we can find ANY route to the destination, consider it reachable

    NodeNum myNode = nodeDB->getNodeNum();
    if (myNode == 0) {
        return false;
    }

#ifdef SIGNAL_ROUTING_LITE_MODE
    RouteLite route = routingGraph->calculateRoute(destination, millis() / 1000,
        [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
#else
    Route route = routingGraph->calculateRoute(destination, millis() / 1000,
        [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
#endif

    if (route.nextHop != 0) {
        LOG_DEBUG("[SR] Node %08x is reachable through topology (nextHop=%08x, cost=%.2f)",
                 destination, route.nextHop, route.getCost());
        return true;
    }

    // Fallback: Check if destination is reachable via a gateway
    NodeNum gateway = getGatewayFor(destination);
    if (gateway != 0) {
        // Check if we can reach the gateway (gateway must be routable)
#ifdef SIGNAL_ROUTING_LITE_MODE
        RouteLite gatewayRoute = routingGraph->calculateRoute(gateway, millis() / 1000,
            [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
        if (gatewayRoute.nextHop != 0) {
            LOG_DEBUG("[SR] Node %08x is reachable via gateway %08x (nextHop=%08x, cost=%.2f)",
                     destination, gateway, gatewayRoute.nextHop, gatewayRoute.getCost());
            return true;
        }
#else
        Route gatewayRoute = routingGraph->calculateRoute(gateway, millis() / 1000,
            [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
        if (gatewayRoute.nextHop != 0) {
            LOG_DEBUG("[SR] Node %08x is reachable via gateway %08x (nextHop=%08x, cost=%.2f)",
                     destination, gateway, gatewayRoute.nextHop, gatewayRoute.cost);
            return true;
        }
#endif
    }

    return false;
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

    uint32_t now = millis() / 1000;  // Use monotonic time

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
    uint32_t now = millis() / 1000;  // Use monotonic time
    for (uint8_t i = 0; i < gatewayRelationCount; i++) {
        if (gatewayRelations[i].downstream == downstream) {
            if ((now - gatewayRelations[i].lastSeen) < ACTIVE_NODE_TTL_SECS) {
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
    uint32_t now = millis() / 1000;  // Use monotonic time
    for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
        if (gatewayDownstream[i].gateway == gateway) {
            if ((now - gatewayDownstream[i].lastSeen) > ACTIVE_NODE_TTL_SECS) {
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
    uint32_t now = millis() / 1000;  // Use monotonic time for TTL calculations

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Lite mode: linear search
    for (uint8_t i = 0; i < capabilityRecordCount; i++) {
        if (capabilityRecords[i].nodeId == nodeId) {
            uint32_t ttl = getNodeTtlSeconds(capabilityRecords[i].record.status);
            if ((now - capabilityRecords[i].record.lastUpdated) > ttl) {
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

    uint32_t ttl = getNodeTtlSeconds(it->second.status);
    if ((now - it->second.lastUpdated) > ttl) {
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
                    // Remember this mapping for future use
                    const_cast<SignalRoutingModule*>(this)->rememberRelayIdentity(myEdges->edges[i].to, p->relay_node);
                    return myEdges->edges[i].to;
                }
            }
        }
#else
        auto neighbors = routingGraph->getDirectNeighbors(nodeDB->getNodeNum());
        for (NodeNum neighbor : neighbors) {
            if ((neighbor & 0xFF) == p->relay_node) {
                // Remember this mapping for future use
                const_cast<SignalRoutingModule*>(this)->rememberRelayIdentity(neighbor, p->relay_node);
                return neighbor;
            }
        }
#endif
    }

    return sourceNode;
}

bool SignalRoutingModule::hasDirectConnectivity(NodeNum nodeA, NodeNum nodeB)
{
    if (!routingGraph || !nodeDB) {
        return false;
    }

    // Check if nodeA has a direct edge to nodeB
#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeA);
    if (edges) {
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (edges->edges[i].to == nodeB) {
                return true;
            }
        }
    }
#else
    auto edges = routingGraph->getEdgesFrom(nodeA);
    if (edges) {
        for (const auto& edge : *edges) {
            if (edge.to == nodeB) {
                return true;
            }
        }
    }
#endif

    return false;
}

