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
        if (nowMs - lastTopologyLog >= 60 * 1000) {
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

    // Process unicast relay contention windows
    processContentionWindows(nowMs);

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

    // Collect all neighbors we want to broadcast
    std::vector<meshtastic_SignalNeighbor> allNeighbors;
    collectNeighborsForBroadcast(allNeighbors);

    if (allNeighbors.empty()) {
        // Send empty broadcast to announce capability
        sendTopologyPacket(dest, std::vector<meshtastic_SignalNeighbor>());
        return;
    }

    // Split into chunks of MAX_SIGNAL_ROUTING_NEIGHBORS and send multiple packets
    // All packets in this broadcast batch will have the same topology_version
    uint8_t topologyVersion = currentTopologyVersion++;  // Increment version for this batch

    size_t totalNeighbors = allNeighbors.size();
    size_t packetsNeeded = (totalNeighbors + MAX_SIGNAL_ROUTING_NEIGHBORS - 1) / MAX_SIGNAL_ROUTING_NEIGHBORS;

    char ourName[64];
    getNodeDisplayName(nodeDB->getNodeNum(), ourName, sizeof(ourName));

    LOG_INFO("[SR] SENDING: Broadcasting %u neighbors in %u packet(s) from %s (version %u)",
             static_cast<unsigned int>(totalNeighbors), static_cast<unsigned int>(packetsNeeded), ourName, topologyVersion);

    // Send packets in chunks
    for (size_t packetIndex = 0; packetIndex < packetsNeeded; packetIndex++) {
        size_t startIdx = packetIndex * MAX_SIGNAL_ROUTING_NEIGHBORS;
        size_t endIdx = std::min(startIdx + MAX_SIGNAL_ROUTING_NEIGHBORS, totalNeighbors);

        std::vector<meshtastic_SignalNeighbor> packetNeighbors(
            allNeighbors.begin() + startIdx,
            allNeighbors.begin() + endIdx
        );

        sendTopologyPacket(dest, packetNeighbors, topologyVersion);
    }

    // Update our own capability after sending
    trackNodeCapability(nodeDB->getNodeNum(), isActiveRoutingRole() ? CapabilityStatus::SRactive : CapabilityStatus::SRinactive);
}
void SignalRoutingModule::collectNeighborsForBroadcast(std::vector<meshtastic_SignalNeighbor> &allNeighbors)
{
    if (!routingGraph || !nodeDB) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* nodeEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!nodeEdges || nodeEdges->edgeCount == 0) {
        return;
    }

    // Prefer reported edges (peer perspective) over mirrored estimates, then order by ETX
    std::vector<const EdgeLite*> allEdges;
    for (uint8_t i = 0; i < nodeEdges->edgeCount; i++) {
        const EdgeLite* e = &nodeEdges->edges[i];
        if (!isPlaceholderNode(e->to)) {  // Filter out placeholders
            allEdges.push_back(e);
        }
    }

    // Sort by quality (reported first, then by ETX)
    std::sort(allEdges.begin(), allEdges.end(), [](const EdgeLite* a, const EdgeLite* b) {
        if (a->source != b->source) {
            return a->source == EdgeLite::Source::Reported; // Reported edges first
        }
        return a->getEtx() < b->getEtx(); // Then by ETX
    });

    // Convert to SignalNeighbor format
    for (const EdgeLite* edge : allEdges) {
        meshtastic_SignalNeighbor neighbor = meshtastic_SignalNeighbor_init_zero;

        neighbor.node_id = edge->to;
        neighbor.position_variance = edge->variance;

        // Mark neighbor based on local knowledge of their SR capability
        CapabilityStatus neighborStatus = getCapabilityStatus(edge->to);
        neighbor.signal_routing_active = (neighborStatus == CapabilityStatus::SRactive);

        int32_t rssi32, snr32;
        GraphLite::etxToSignal(edge->getEtx(), rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));

        allNeighbors.push_back(neighbor);
    }
#else
    const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!edges || edges->empty()) {
        return;
    }

    // Prefer edges with reported quality, then fall back to mirrored estimates
    std::vector<const Edge*> allEdges;
    for (const Edge& e : *edges) {
        allEdges.push_back(&e);
    }

    // Sort by quality (reported first, then by ETX)
    std::sort(allEdges.begin(), allEdges.end(), [](const Edge* a, const Edge* b) {
        if (a->source != b->source) {
            return a->source == Edge::Source::Reported; // Reported edges first
        }
        return a->etx < b->etx; // Then by ETX
    });

    // Convert to SignalNeighbor format
    for (const Edge* edge : allEdges) {
        meshtastic_SignalNeighbor neighbor = meshtastic_SignalNeighbor_init_zero;

        neighbor.node_id = edge->to;
        neighbor.position_variance = edge->variance;

        // Mark neighbor based on local knowledge of their SR capability
        CapabilityStatus neighborStatus = getCapabilityStatus(edge->to);
        neighbor.signal_routing_active = (neighborStatus == CapabilityStatus::SRactive);

        int32_t rssi32, snr32;
        Graph::etxToSignal(edge->etx, rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));

        allNeighbors.push_back(neighbor);
    }
#endif
}

void SignalRoutingModule::sendTopologyPacket(NodeNum dest, const std::vector<meshtastic_SignalNeighbor> &neighbors, uint8_t topologyVersion)
{
    meshtastic_SignalRoutingInfo info = meshtastic_SignalRoutingInfo_init_zero;
    info.signal_routing_active = isActiveRoutingRole();
    info.routing_version = SIGNAL_ROUTING_VERSION;
    info.topology_version = topologyVersion;
    info.neighbors_count = neighbors.size();

    // Copy neighbors to info struct
    for (size_t i = 0; i < neighbors.size() && i < MAX_SIGNAL_ROUTING_NEIGHBORS; i++) {
        info.neighbors[i] = neighbors[i];
    }

    meshtastic_MeshPacket *p = allocDataProtobuf(info);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

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
    info.signal_routing_active = isActiveRoutingRole();
    info.routing_version = SIGNAL_ROUTING_VERSION;
    info.topology_version = currentTopologyVersion++;  // Increment version (0-255, wraps)
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
        neighbor.signal_routing_active = (neighborStatus == CapabilityStatus::SRactive);

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
        neighbor.signal_routing_active = (neighborStatus == CapabilityStatus::SRactive);

        int32_t rssi32, snr32;
        Graph::etxToSignal(edge.etx, rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));
    }
#endif
}

void SignalRoutingModule::updateGraphWithNeighbor(NodeNum sender, const meshtastic_SignalNeighbor &neighbor)
{
    // Add/update edge from sender to this neighbor
    if (routingGraph) {
        float etx = 1.0f; // Default ETX, will be updated with real measurements
        uint32_t currentTime = millis() / 1000;

        // For GraphLite
        routingGraph->updateEdge(sender, neighbor.node_id, etx, currentTime);

        LOG_DEBUG("[SR] Added edge %08x -> %08x from topology", sender, neighbor.node_id);
    }
}

void SignalRoutingModule::preProcessSignalRoutingPacket(const meshtastic_MeshPacket *p, uint32_t packetReceivedTimestamp)
{
    if (!routingGraph || !p) return;

    // Skip processing entirely for packets we sent (detected as rebroadcasts)
    // This prevents topology pollution from our own rebroadcasted packets
    NodeNum ourNode = nodeDB ? nodeDB->getNodeNum() : 0;
    uint32_t currentTime = millis() / 1000;
    if (routingGraph->hasNodeTransmitted(ourNode, p->id, currentTime)) {
        LOG_DEBUG("[SR] Skipping topology processing for rebroadcast of our packet %08x", p->id);
        return;
    }

    // Only process SignalRoutingInfo packets
    if (p->decoded.portnum != meshtastic_PortNum_SIGNAL_ROUTING_APP) {
        LOG_DEBUG("[SR] Skipping non-SR packet in preProcessSignalRoutingPacket (portnum=%d)", p->decoded.portnum);
        return;
    }
    // Reject packets from invalid node IDs (0 is invalid)
    if (p->from == 0) {
        LOG_DEBUG("[SR] Ignoring SR broadcast from invalid node ID 0");
        return;
    }

    // Decode the protobuf to get neighbor data
    meshtastic_SignalRoutingInfo info = meshtastic_SignalRoutingInfo_init_zero;
    if (!pb_decode_from_bytes(p->decoded.payload.bytes, p->decoded.payload.size,
                              &meshtastic_SignalRoutingInfo_msg, &info)) {
        LOG_WARN("[SR] Failed to decode SignalRoutingInfo from %08x (payload size=%u)", 
                 p->from, p->decoded.payload.size);
        return;
    }

    // Check version validity with wraparound logic
    uint8_t receivedVersion = info.topology_version;
    uint8_t lastProcessedVersion = lastTopologyVersion[p->from];

    bool accept = false;
    if (receivedVersion > lastProcessedVersion) {
        // Normal case: received is higher, accept
        accept = true;
    } else if (receivedVersion < lastProcessedVersion) {
        // Check if received is within 100 of the wraparound point
        uint8_t threshold = (lastProcessedVersion + 256 - 100) % 256;
        if (receivedVersion >= threshold || receivedVersion < 100) {
            accept = true;
        }
    } else if (receivedVersion == lastProcessedVersion) {
        // Same version - this could be a retransmission or multi-packet
        // Accept it to handle merging
        accept = true;
    }

    if (!accept) {
        LOG_DEBUG("[SR] Ignoring stale topology broadcast from %08x (version %u, last processed %u)",
                 p->from, receivedVersion, lastProcessedVersion);
        return;
    }

    // Check if this is a NEW topology version (not a continuation of multi-packet broadcast)
    bool isNewVersion = (receivedVersion != lastProcessedVersion);
    
    // Update version tracking
    lastTopologyVersion[p->from] = receivedVersion;

    // Update capability status for the sender (this is normally done in handleReceivedProtobuf)
    CapabilityStatus newStatus = info.signal_routing_active ? CapabilityStatus::SRactive : CapabilityStatus::SRinactive;
    CapabilityStatus oldStatus = getCapabilityStatus(p->from);
    trackNodeCapability(p->from, newStatus);

    if (oldStatus != newStatus) {
        char senderName[64];
        getNodeDisplayName(p->from, senderName, sizeof(senderName));
        LOG_INFO("[SR] Capability update: %s changed from %d to %d",
                senderName, (int)oldStatus, (int)newStatus);
    }

    // Process topology directly from the received packet - no intermediate storage
    char senderNameForTopo[48];
    getNodeDisplayName(p->from, senderNameForTopo, sizeof(senderNameForTopo));
    LOG_INFO("[SR] Processing topology from %s: %d neighbors (version %u, %s, relay=0x%02x)",
              senderNameForTopo, info.neighbors_count, receivedVersion, 
              isNewVersion ? "new version" : "continuation", p->relay_node);

    // Only clear existing edges when starting a NEW topology version
    // For multi-packet broadcasts (same version), we append to existing edges
    if (isNewVersion && routingGraph) {
        routingGraph->clearEdgesForNode(p->from);
        LOG_DEBUG("[SR] Cleared existing edges for node %08x (new version)", p->from);
    }

    // Process each neighbor directly from the received info - memory efficient
    for (pb_size_t i = 0; i < info.neighbors_count; i++) {
        const meshtastic_SignalNeighbor& neighbor = info.neighbors[i];

        // Reject neighbors with invalid node IDs (0 or placeholders)
        if (neighbor.node_id == 0 || isPlaceholderNode(neighbor.node_id)) {
            LOG_DEBUG("[SR] Skipping invalid neighbor node ID: %08x", neighbor.node_id);
            continue;
        }

        // Process this neighbor directly - no need for protobuf handler since we already validated the main packet
        // This is just for graph updates, capability status was already handled for the main sender
        updateGraphWithNeighbor(p->from, neighbor);
        
        // Create gateway relationship ONLY for nodes we cannot hear directly
        // This ensures remote nodes appear as downstream of the SR broadcaster node,
        // but nodes we can hear directly (like ourselves) are not incorrectly marked as downstream
        bool hasDirectConnection = false;
        NodeNum ourNode = nodeDB ? nodeDB->getNodeNum() : 0;
        
        // Never mark ourselves as downstream of anyone
        if (neighbor.node_id == ourNode) {
            hasDirectConnection = true;
        } else if (routingGraph) {
            // Check for edges indicating direct communication:
            // - neighbor → us with Reported source (we heard from them directly)
            // - us → neighbor with any source (we created this when we heard from them)
            // Note: us → neighbor is created as Mirrored (symmetric assumption) when we receive from neighbor
#ifdef SIGNAL_ROUTING_LITE_MODE
            // Check edges FROM neighbor TO us (Reported = we heard from them)
            const NodeEdgesLite* neighborEdges = routingGraph->getEdgesFrom(neighbor.node_id);
            if (neighborEdges) {
                for (uint8_t j = 0; j < neighborEdges->edgeCount; j++) {
                    if (neighborEdges->edges[j].to == ourNode && 
                        neighborEdges->edges[j].source == EdgeLite::Source::Reported) {
                        hasDirectConnection = true;
                        break;
                    }
                }
            }
            // Also check edges FROM us TO neighbor (any source - Mirrored means we heard from them)
            if (!hasDirectConnection) {
                const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
                if (ourEdges) {
                    for (uint8_t j = 0; j < ourEdges->edgeCount; j++) {
                        if (ourEdges->edges[j].to == neighbor.node_id) {
                            hasDirectConnection = true;
                            break;
                        }
                    }
                }
            }
#else
            // Check edges FROM neighbor TO us (Reported = we heard from them)
            const std::vector<Edge>* neighborEdges = routingGraph->getEdgesFrom(neighbor.node_id);
            if (neighborEdges) {
                for (const Edge& edge : *neighborEdges) {
                    if (edge.to == ourNode && edge.source == Edge::Source::Reported) {
                        hasDirectConnection = true;
                        break;
                    }
                }
            }
            // Also check edges FROM us TO neighbor (any source - Mirrored means we heard from them)
            if (!hasDirectConnection) {
                const std::vector<Edge>* ourEdges = routingGraph->getEdgesFrom(ourNode);
                if (ourEdges) {
                    for (const Edge& edge : *ourEdges) {
                        if (edge.to == neighbor.node_id) {
                            hasDirectConnection = true;
                            break;
                        }
                    }
                }
            }
#endif
        }
        
        // Only create gateway relationships if:
        // 1. We don't have direct connection to the neighbor
        // 2. The topology broadcast was received DIRECTLY from the sender (not relayed)
        // This prevents intermediate nodes (like piko/Nara) from incorrectly becoming gateways
        // when they relay topology from the actual gateway (angl)
        bool receivedDirectly = (p->relay_node == 0) ||
                                ((p->from & 0xFF) == p->relay_node);
        
        // Detailed logging for debugging topology processing
        char neighborName[48];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));
        
        if (!hasDirectConnection && receivedDirectly) {
            // This node is only reachable through the topology broadcaster - mark as downstream
            LOG_INFO("[SR]   -> %s: NO direct connection, marking as downstream of %s",
                    neighborName, senderNameForTopo);
            recordGatewayRelation(p->from, neighbor.node_id);
        } else if (hasDirectConnection) {
            LOG_DEBUG("[SR]   -> %s: HAS direct connection, NOT marking as downstream",
                    neighborName);
        } else {
            LOG_DEBUG("[SR]   -> %s: received via relay (not direct), NOT marking as downstream",
                    neighborName);
        }
    }

    // Update last processed version (minimal state tracking)
    lastTopologyVersion[p->from] = receivedVersion;

}
bool SignalRoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_SignalRoutingInfo *p)
{
    // Inactive SR roles don't participate in routing decisions - skip processing topology broadcasts from others
    if (!isActiveRoutingRole()) {
        LOG_DEBUG("[SR] Inactive role: Skipping topology broadcast processing from %08x", mp.from);
        return false;
    }
    // Reject packets from invalid node IDs (0 is invalid)
    if (mp.from == 0) {
        LOG_INFO("[SR] Ignoring SR broadcast from invalid node ID 0 in handleReceivedProtobuf");
        return false;
    }

    // Note: Graph may have already been updated by preProcessSignalRoutingPacket()
    // This is intentional - we want up-to-date data for relay decisions
    if (!routingGraph || !p) return false;

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    // Mark sender based on their claimed SR capability
    CapabilityStatus newStatus = p->signal_routing_active ? CapabilityStatus::SRactive : CapabilityStatus::SRinactive;
    CapabilityStatus oldStatus = getCapabilityStatus(mp.from);
    trackNodeCapability(mp.from, newStatus);

    if (oldStatus != newStatus) {
        LOG_INFO("[SR] Capability update: %s changed from %d to %d",
                senderName, (int)oldStatus, (int)newStatus);
    }

    if (p->neighbors_count == 0) {
        LOG_INFO("[SR] %s is online (SR v%d, %s) - no neighbors detected yet",
                 senderName, p->routing_version,
                 p->signal_routing_active ? "SR-active" : "SR-inactive");

        // Clear gateway relationships for SR-capable nodes with no neighbors - they can't be gateways
        if (p->signal_routing_active) {
            clearGatewayRelationsFor(mp.from);
        }

        return false;
    }

    LOG_INFO("[SR] RECEIVED: %s reports %d neighbors (SR v%d, %s)",
             senderName, p->neighbors_count, p->routing_version,
             p->signal_routing_active ? "SR-active" : "SR-inactive");

    // Set cyan for network topology update (operation start)
    setRgbLed(0, 255, 255);

    // For SR-inactive nodes (signal_routing_active = false), we still need to store their edges for direct connection checks
    // Active nodes use these edges to determine if a SR-inactive node has direct connections to destinations
    // However, routing algorithms must not consider paths through SR-inactive nodes since they don't relay
    if (!p->signal_routing_active) {
        LOG_DEBUG("[SR] Received topology from SR-inactive node %08x - storing edges for direct connection detection", mp.from);
    }

    // Clear all existing edges for this node before adding the new ones from the broadcast
    // This ensures our view of the sender's connectivity matches exactly what it reported
#ifdef SIGNAL_ROUTING_LITE_MODE
    routingGraph->clearEdgesForNode(mp.from);
    // Also clear any inferred connectivity edges pointing TO this node that were created
    // before we knew it was SR-capable
    routingGraph->clearInferredEdgesToNode(mp.from);
#else
    routingGraph->clearEdgesForNode(mp.from);
    // Also clear any inferred connectivity edges pointing TO this node that were created
    // before we knew it was SR-capable
    routingGraph->clearInferredEdgesToNode(mp.from);
#endif

    // Add edges from each neighbor TO the sender
    // The RSSI/SNR describes how well the sender hears the neighbor,
    // which characterizes the neighbor→sender transmission quality
    // Use reliable time-from-boot for internal timing
    // (This may be redundant if preProcessSignalRoutingPacket already ran, but it's idempotent)
    uint32_t rxTime = millis() / 1000;
    for (pb_size_t i = 0; i < p->neighbors_count; i++) {
        const meshtastic_SignalNeighbor& neighbor = p->neighbors[i];

        // Reject neighbors with invalid node IDs (0 or placeholders)
        if (neighbor.node_id == 0 || isPlaceholderNode(neighbor.node_id)) {
            LOG_DEBUG("[SR] Skipping invalid neighbor node ID: %08x", neighbor.node_id);
            continue;
        }

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
                 neighbor.signal_routing_active ? "SR-active" : "SR-inactive",
                 quality, etx,
                 neighbor.position_variance);

        // If the sender is SR-capable and reports this neighbor as directly reachable,
        // clear ALL gateway relationships for this neighbor - it's now reachable via the SR network
        if (p->signal_routing_active) {
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

    // Transfer ONLY legitimate graph edges from placeholder to real node
    // Placeholders are used for inferred connectivity and should not have edges that represent
    // actual neighbor relationships. Only transfer reverse edges from our node to the placeholder,
    // which represent actual radio connectivity that should be preserved.
    if (routingGraph) {
        NodeNum ourNode = nodeDB->getNodeNum();

#ifdef SIGNAL_ROUTING_LITE_MODE
        // Only transfer reverse edges (where our node had a direct link to the placeholder)
        // These represent actual radio connectivity that should be preserved
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
        // Only transfer reverse edges (where our node had a direct link to the placeholder)
        // These represent actual radio connectivity that should be preserved
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

        // Remove the placeholder node from the graph
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

    // Update gatewayDownstream array - need to handle carefully to avoid duplicates
    // First, collect all downstream nodes for the old gateway
    GatewayDownstreamSet oldGatewaySet = {};
    bool foundOldGateway = false;
    for (uint8_t i = 0; i < gatewayDownstreamCount; ) {
        if (gatewayDownstream[i].gateway == oldNode) {
            if (!foundOldGateway) {
                oldGatewaySet = gatewayDownstream[i];
                oldGatewaySet.gateway = newNode; // Change to new gateway
                foundOldGateway = true;
            }
            // Remove this entry by shifting
            for (uint8_t j = i; j < gatewayDownstreamCount - 1; j++) {
                gatewayDownstream[j] = gatewayDownstream[j + 1];
            }
            gatewayDownstreamCount--;
            // Don't increment i since we shifted elements
        } else {
            i++;
        }
    }

    // Replace any downstream references to oldNode with newNode across all gateway sets
    for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
        for (uint8_t j = 0; j < gatewayDownstream[i].count; j++) {
            if (gatewayDownstream[i].downstream[j] == oldNode) {
                gatewayDownstream[i].downstream[j] = newNode;
            }
        }
    }

    // If we found an old gateway entry, add it back with the new gateway ID
    if (foundOldGateway && gatewayDownstreamCount < MAX_GATEWAY_RELATIONS) {
        gatewayDownstream[gatewayDownstreamCount++] = oldGatewaySet;
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
        // Replace oldNode with newNode if it exists as a downstream
        if (downstreamSet.count(oldNode)) {
            downstreamSet.erase(oldNode);
            downstreamSet.insert(newNode);
        }
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
    // Only known SR-active/inactive nodes get shorter TTL (they send regular broadcasts)
    // Stock nodes, legacy routers, and unknown nodes need longer TTL since they
    // transmit less frequently and we don't want to age them out prematurely
    if (status == CapabilityStatus::SRactive || status == CapabilityStatus::SRinactive) {
        return ACTIVE_NODE_TTL_SECS;  // 5 minutes for known SR nodes
    }
    // Legacy, Unknown, and any other status get longer TTL
    return MUTE_NODE_TTL_SECS;  // 30 minutes for stock/unknown nodes
}

void SignalRoutingModule::logNetworkTopology()
{
    if (!routingGraph) return;

#ifdef SIGNAL_ROUTING_LITE_MODE
    // LITE mode: use fixed-size arrays only, no heap allocations
    NodeNum nodeBuf[GRAPH_LITE_MAX_NODES];
    size_t rawNodeCount = routingGraph->getAllNodeIds(nodeBuf, GRAPH_LITE_MAX_NODES);

    // Filter out downstream nodes - they should only appear under their gateways
    uint32_t now = millis() / 1000;
    size_t nodeCount = 0;
    for (size_t i = 0; i < rawNodeCount; i++) {
        NodeNum nodeId = nodeBuf[i];
        bool isDownstream = false;
        // Check if this node is downstream of any gateway
        for (uint8_t j = 0; j < gatewayDownstreamCount; j++) {
            const GatewayDownstreamSet &set = gatewayDownstream[j];
            if ((now - set.lastSeen) <= ACTIVE_NODE_TTL_SECS) {
                for (uint8_t k = 0; k < set.count; k++) {
                    if (set.downstream[k] == nodeId) {
                        isDownstream = true;
                        goto foundDownstream;
                    }
                }
            }
        }
        foundDownstream:
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

        // Get gateway downstreams for this node (using fixed iteration, no heap allocation)
        const GatewayDownstreamSet* downstreamSet = nullptr;
        uint32_t now = millis() / 1000;  // Use monotonic time
        for (uint8_t i = 0; i < gatewayDownstreamCount; i++) {
            const GatewayDownstreamSet &set = gatewayDownstream[i];
            if (set.gateway == nodeId && (now - set.lastSeen) <= ACTIVE_NODE_TTL_SECS) {
                downstreamSet = &set;
                break;
            }
        }

        // Helper to check if a node is a downstream of this gateway
        auto isDownstreamOfThisGateway = [downstreamSet](NodeNum node) -> bool {
            if (!downstreamSet) return false;
            for (uint8_t i = 0; i < downstreamSet->count; i++) {
                if (downstreamSet->downstream[i] == node) return true;
            }
            return false;
        };

        // Count direct neighbors (edges that are NOT to our own downstream nodes)
        // Downstream nodes will be shown separately in the gateway list
        uint8_t directNeighborCount = 0;
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (!isDownstreamOfThisGateway(edges->edges[i].to)) {
                directNeighborCount++;
            }
        }
        uint8_t downstreamCount = downstreamSet ? downstreamSet->count : 0;

        if (downstreamCount == 0) {
            LOG_INFO("[SR] +- %s%s: connected to %d nodes", prefix, nodeName, directNeighborCount);
        } else {
            LOG_INFO("[SR] +- %s%s: connected to %d nodes (gateway for %d nodes)", prefix, nodeName, directNeighborCount, downstreamCount);
        }

        // Display direct neighbors (skip downstream nodes - they're shown in gateway list)
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            const EdgeLite& edge = edges->edges[i];

            // Skip downstream nodes of this gateway - they're already counted in "gateway for X nodes"
            if (isDownstreamOfThisGateway(edge.to)) {
                continue;
            }

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

            int32_t age = computeAgeSecs(edge.lastUpdate, millis() / 1000);
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

    // Filter out downstream nodes - they should only appear under their gateways
    uint32_t now = millis() / 1000;  // Use monotonic time
    std::vector<NodeNum> topologyNodes;
    for (NodeNum nodeId : allNodes) {
        auto dgIt = downstreamGateway.find(nodeId);
        if (dgIt == downstreamGateway.end()) {
            // Node is not downstream of any gateway
            topologyNodes.push_back(nodeId);
        } else if ((now - dgIt->second.lastSeen) > ACTIVE_NODE_TTL_SECS) {
            // Downstream relationship has expired
            topologyNodes.push_back(nodeId);
        }
        // If relationship is active, exclude from topology (node appears under its gateway)
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

        // Get this node's downstream nodes
        std::vector<NodeNum> downstreams;
        appendGatewayDownstreams(nodeId, downstreams);
        std::sort(downstreams.begin(), downstreams.end());
        downstreams.erase(std::unique(downstreams.begin(), downstreams.end()), downstreams.end());
        
        // Helper to check if a node is a downstream of this gateway
        auto isDownstreamOfThisGateway = [&downstreams](NodeNum node) -> bool {
            return std::binary_search(downstreams.begin(), downstreams.end(), node);
        };

        // Count direct neighbors (edges that are NOT to our own downstream nodes)
        // Downstream nodes will be shown separately in the gateway list
        size_t directNeighborCount = 0;
        for (const Edge& edge : *edges) {
            if (!isDownstreamOfThisGateway(edge.to)) {
                directNeighborCount++;
            }
        }

        if (downstreams.empty()) {
            LOG_INFO("[SR] +- %s%s: connected to %d nodes", prefix, nodeName, directNeighborCount);
        } else {
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
            LOG_INFO("[SR] +- %s%s: connected to %d nodes (gateway for %u nodes: %s)", prefix, nodeName, directNeighborCount, static_cast<unsigned int>(downstreams.size()), buf);
        }

        // Sort edges by ETX for consistent output
        std::vector<Edge> sortedEdges = *edges;
        std::sort(sortedEdges.begin(), sortedEdges.end(),
                 [](const Edge& a, const Edge& b) { return a.etx < b.etx; });

        // Display direct neighbors (skip downstream nodes - they're shown in gateway list)
        for (size_t i = 0; i < sortedEdges.size(); i++) {
            const Edge& edge = sortedEdges[i];
            
            // Skip downstream nodes of this gateway - they're already counted in "gateway for X nodes"
            if (isDownstreamOfThisGateway(edge.to)) {
                continue;
            }
            
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
    // Sanity check: reject packets with obviously corrupted payload sizes
    // Max valid payload is ~237 bytes for LoRa; anything over 256 is definitely garbage
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp.decoded.payload.size > meshtastic_Constants_DATA_PAYLOAD_LEN) {
        LOG_WARN("[SR] Rejecting packet with invalid payload size: %u bytes (max %u)",
                 mp.decoded.payload.size, meshtastic_Constants_DATA_PAYLOAD_LEN);
        return ProcessMessage::CONTINUE;
    }

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

    // Handle ACK reception for unicast coordination
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.request_id != 0 &&
        mp.to == nodeDB->getNodeNum()) {
        // Clear unicast relay exclusions when ACK is received - coordination successful
        clearRelayExclusionsForPacket(mp.decoded.request_id);
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
    bool isDirectFromSender = (mp.relay_node == 0) || (mp.relay_node == fromLastByte);
    
    // Debug logging to understand why packets might not be tracked
    if (hasSignalData && notViaMqtt) {
        LOG_DEBUG("[SR] Packet from 0x%08x: relay=0x%02x, fromLastByte=0x%02x, direct=%d",
                  mp.from, mp.relay_node, fromLastByte, isDirectFromSender);
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
    } else if (notViaMqtt && !isDirectFromSender && mp.relay_node != 0) {
        // Process relayed packets to infer network topology (skip for inactive roles - they only track direct neighbors)
        if (!isActiveRoutingRole()) {
            LOG_DEBUG("[SR] Inactive role: Skipping relayed packet topology inference");
        } else {
            NodeNum inferredRelayer = resolveRelayIdentity(mp.relay_node);

        // If still not resolved, try known nodes (both direct neighbors and topology-known nodes)
        // We need to check ALL edges, not just Reported ones, because the relay might be
        // a node we only know through topology broadcasts (Mirrored edges)
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
                        LOG_DEBUG("[SR] Resolved relay 0x%02x to known node %08x",
                                 mp.relay_node, neighbor);
                        break;
                    }
                }
            }
#else
            // Check all edges from our node (both Reported and Mirrored)
            const std::vector<Edge>* myEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (myEdges) {
                for (const Edge& edge : *myEdges) {
                    if ((edge.to & 0xFF) == mp.relay_node) {
                        inferredRelayer = edge.to;
                        // Remember this mapping for future use
                        rememberRelayIdentity(edge.to, mp.relay_node);
                        LOG_DEBUG("[SR] Resolved relay 0x%02x to known node %08x",
                                 mp.relay_node, edge.to);
                        break;
                    }
                }
            }
#endif
        }

        // If we can't resolve the relay identity, create a placeholder node
        if (inferredRelayer == 0) {
            inferredRelayer = createPlaceholderNode(mp.relay_node);
            // Don't remember placeholders in relay identity cache - only remember real nodes
            // rememberRelayIdentity(inferredRelayer, mp.relay_node);
            LOG_DEBUG("[SR] Created placeholder %08x for unknown relay 0x%02x",
                     inferredRelayer, mp.relay_node);
        }

        if (inferredRelayer != 0 && inferredRelayer != mp.from) {
            // Remember this relay identity mapping for future use (only for real nodes, not placeholders)
            if (!isPlaceholderNode(inferredRelayer)) {
                rememberRelayIdentity(inferredRelayer, mp.relay_node);
            }

            // We know that inferredRelayer relayed a packet from mp.from
            // This establishes both a gateway relationship and direct connectivity inference
            LOG_DEBUG("[SR] Inferred gateway relationship: %08x relayed by %08x",
                     mp.from, inferredRelayer);

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

            // Infer direct connectivity between relayer and sender only for stock firmware nodes
            // SR-aware nodes broadcast their topology, so we don't need to infer connectivity for them
            if (getCapabilityStatus(inferredRelayer) == CapabilityStatus::Legacy) {
                // Since the relayer successfully relayed a packet from the sender,
                // we can assume they have direct connectivity
                LOG_DEBUG("[SR] Inferred direct connectivity: legacy node %08x can hear %08x directly",
                         inferredRelayer, mp.from);

                // Add edge between inferredRelayer and mp.from with default signal quality
                // Use Mirrored source since this is inferred, not directly measured
                uint32_t monotonicTimestamp = millis() / 1000;
                int32_t defaultRssi = -70; // default RSSI for inferred connectivity
                float defaultSnr = 5.0f;  // default SNR for inferred connectivity

#ifdef SIGNAL_ROUTING_LITE_MODE
                routingGraph->updateEdge(mp.from, inferredRelayer, GraphLite::calculateETX(defaultRssi, defaultSnr),
                                         monotonicTimestamp, 0, EdgeLite::Source::Mirrored);
                routingGraph->updateEdge(inferredRelayer, mp.from, GraphLite::calculateETX(defaultRssi, defaultSnr),
                                         monotonicTimestamp, 0, EdgeLite::Source::Mirrored);
#else
                routingGraph->updateEdge(mp.from, inferredRelayer, Graph::calculateETX(defaultRssi, defaultSnr),
                                         monotonicTimestamp, 0, Edge::Source::Mirrored);
                routingGraph->updateEdge(inferredRelayer, mp.from, Graph::calculateETX(defaultRssi, defaultSnr),
                                         monotonicTimestamp, 0, Edge::Source::Mirrored);
#endif
            } else {
                LOG_DEBUG("[SR] Skipping direct connectivity inference for SR-aware node %08x (capability: %d)",
                         inferredRelayer, (int)getCapabilityStatus(inferredRelayer));
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
            
            // Use capability-based TTL: SR-active nodes (5 min), stock/mute nodes (30 min)
            auto getTtlForNode = [this](NodeNum nodeId) -> uint32_t {
                CapabilityStatus status = getCapabilityStatus(nodeId);
                return getNodeTtlSeconds(status);
            };
            routingGraph->ageEdges(currentTime, getTtlForNode);
            
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

    char destName[64], heardFromName[64];
    getNodeDisplayName(destination, destName, sizeof(destName));
    getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));

    // UNICAST RELAY COORDINATION: Use broadcast-style algorithm
    // Calculate what my next hop would be to reach the destination
    NodeNum myNextHop = getNextHop(destination, sourceNode, heardFrom, false);

    if (myNextHop == 0) {
        // I don't have a route to the destination, so I can't be part of the relay chain
        LOG_DEBUG("[SR] UNICAST RELAY: No route to %s - cannot participate in relay coordination", destName);
        return false;
    }

    char nextHopName[64];
    getNodeDisplayName(myNextHop, nextHopName, sizeof(nextHopName));

    // If next hop IS the destination, it means destination is a direct neighbor.
    // We should deliver directly, not wait for destination to "relay to itself".
    if (myNextHop == destination) {
        LOG_INFO("[SR] UNICAST RELAY: Destination %s is direct neighbor - delivering directly", destName);
        return true;
    }

    // If I'm the next hop in the route, I should relay this unicast
    if (myNextHop == myNode) {
        LOG_INFO("[SR] UNICAST RELAY: I am the next hop to %s - relaying unicast", destName);
        return true;
    }

    // If we received the packet FROM the calculated next hop, the next hop already transmitted.
    // Since the next hop can reach the destination (that's why it's our next hop), the destination
    // should have received the packet directly from the next hop - no need for us to relay.
    if (heardFrom == myNextHop && heardFrom != 0) {
        LOG_DEBUG("[SR] UNICAST RELAY: Received from next hop %s who can reach %s - destination should have it",
                 nextHopName, destName);
        return false;  // Next hop already transmitted, destination should have received it
    }

    // Check if heardFrom is ALSO a gateway for the destination.
    // If we received the packet from ANY gateway that leads to the destination,
    // that gateway should deliver it - we don't need to relay.
    if (heardFrom != 0 && heardFrom != sourceNode) {
        NodeNum gatewayForDest = getGatewayFor(destination);
        if (gatewayForDest == heardFrom) {
            LOG_DEBUG("[SR] UNICAST RELAY: Received from gateway %s which leads to %s - no relay needed",
                     heardFromName, destName);
            return false;
        }
    }

    // Check if next hop can hear the transmitting node (heardFrom).
    // If we can't verify the next hop heard the transmission, we should relay ourselves
    // (especially for stock/placeholder gateways that don't report topology).
    bool nextHopCanHearTransmitter = false;
    if (heardFrom != 0) {
        // Check if heardFrom has an edge to myNextHop (heardFrom → myNextHop)
        nextHopCanHearTransmitter = hasDirectConnectivity(heardFrom, myNextHop);

        // Also check reverse direction (myNextHop → heardFrom)
        if (!nextHopCanHearTransmitter) {
            nextHopCanHearTransmitter = hasDirectConnectivity(myNextHop, heardFrom);
        }
    }

    // For stock/placeholder nodes, we can't verify connectivity - be conservative
    CapabilityStatus nextHopStatus = getCapabilityStatus(myNextHop);
    bool isStockOrPlaceholder = isPlaceholderNode(myNextHop) ||
                                nextHopStatus == CapabilityStatus::Legacy ||
                                nextHopStatus == CapabilityStatus::Unknown;

    if (!nextHopCanHearTransmitter && isStockOrPlaceholder) {
        // Can't verify stock/placeholder gateway heard the transmitter
        // If we heard directly from the source, we should relay ourselves
        if (heardFrom == sourceNode) {
            LOG_INFO("[SR] UNICAST RELAY: Cannot verify stock gateway %s heard source %s - relaying",
                    nextHopName, heardFromName);
            return true;
        }
        // If we heard from an intermediate relayer, still be conservative for stock gateways
        LOG_DEBUG("[SR] UNICAST RELAY: Stock gateway %s may not have heard transmitter %s - excluding from candidates",
                 nextHopName, heardFromName);
        excludeNodeFromRelay(myNextHop, p->id);
        // Fall through to try alternative route
    }

    // Check if the calculated next hop node has already been tried and failed
    // This implements the iterative candidate removal like broadcasts
    if (hasNodeBeenExcludedFromRelay(myNextHop, p->id)) {
        LOG_DEBUG("[SR] UNICAST RELAY: Next hop %s has been excluded for packet %08x - trying alternative route",
                 nextHopName, p->id);

        // Try opportunistic routing as fallback
        NodeNum opportunisticNextHop = getNextHop(destination, sourceNode, heardFrom, true);
        if (opportunisticNextHop != 0 && opportunisticNextHop != myNextHop) {
            if (opportunisticNextHop == myNode || opportunisticNextHop == destination) {
                LOG_INFO("[SR] UNICAST RELAY: Using opportunistic routing - delivering to %s", destName);
                return true;
            }
            // Check if opportunistic next hop can hear transmitter
            bool oppCanHear = hasDirectConnectivity(heardFrom, opportunisticNextHop) ||
                             hasDirectConnectivity(opportunisticNextHop, heardFrom);
            if (oppCanHear && !hasNodeBeenExcludedFromRelay(opportunisticNextHop, p->id)) {
                char oppName[64];
                getNodeDisplayName(opportunisticNextHop, oppName, sizeof(oppName));
                LOG_DEBUG("[SR] UNICAST RELAY: Waiting for opportunistic next hop %s", oppName);
                scheduleContentionWindowCheck(opportunisticNextHop, p->id, destination, p);
                return false;
            }
        }

        // No valid alternative found - we should relay ourselves as last resort
        LOG_INFO("[SR] UNICAST RELAY: No alternative candidates - relaying to %s ourselves", destName);
        return true;
    }

    // Verify next hop can actually hear the transmitting node before waiting
    if (!nextHopCanHearTransmitter && !isStockOrPlaceholder) {
        // For SR nodes without connectivity to transmitter, skip them immediately
        LOG_DEBUG("[SR] UNICAST RELAY: Next hop %s cannot hear transmitter %s - excluding",
                 nextHopName, heardFromName);
        excludeNodeFromRelay(myNextHop, p->id);
        // Recurse to try next candidate
        return shouldRelayUnicastForCoordination(p);
    }

    // I'm not the next hop, so I should wait for the next hop node to relay
    // This is the contention window waiting period like broadcasts
    LOG_DEBUG("[SR] UNICAST RELAY: Waiting for next hop %s to relay to %s", nextHopName, destName);

    // Set up contention window monitoring - if next hop doesn't relay within timeout, exclude it
    scheduleContentionWindowCheck(myNextHop, p->id, destination, p);

    return false; // Don't relay yet - wait for the proper next hop
}
bool SignalRoutingModule::shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p)
{
    // This function only checks if SR is available and operational.
    // All actual routing decisions are made in shouldRelay().

    if (!p || !signalBasedRoutingEnabled || !routingGraph || !nodeDB) {
        return false;
    }

    // Update SR graph timestamps for any packet we process
    updateNodeActivityForPacketAndRelay(p);

    // Don't use SR for packets addressed to us - let them be delivered normally
    if (!isBroadcast(p->to) && p->to == nodeDB->getNodeNum()) {
        return false;
    }

    // For broadcasts: check if we have enough SR neighbors
    if (isBroadcast(p->to)) {
        // Passive roles can still veto relays through shouldRelay
        if (!isActiveRoutingRole()) {
            return true;
        }
        return topologyHealthyForBroadcast();
    }

    // For unicasts: SR is available if we have any neighbors and are active role
    // shouldRelay() will handle unknown destinations and routing decisions
    if (!isActiveRoutingRole()) {
        return false;
    }

    // Use SR for unicasts if we have topology (at least one SR neighbor)
    // This allows shouldRelay() to make informed decisions about routing
    return topologyHealthyForBroadcast();
}

bool SignalRoutingModule::shouldRelay(const meshtastic_MeshPacket *p)
{
    if (!routingGraph || !nodeDB) {
        return true;  // Default to relay if SR unavailable
    }

    // For broadcasts, use the existing broadcast relay logic
    if (isBroadcast(p->to)) {
        return shouldRelayBroadcast(p);
    }

    // === UNICAST RELAY DECISION ===
    // All unicast routing logic is now consolidated here

    char destName[64], senderName[64];
    getNodeDisplayName(p->to, destName, sizeof(destName));
    getNodeDisplayName(p->from, senderName, sizeof(senderName));
    LOG_DEBUG("[SR] Considering unicast relay from %s to %s (hop_limit=%d)",
             senderName, destName, p->hop_limit);

    // Check if destination is known - don't relay to unknown destinations
    if (!topologyHealthyForUnicast(p->to)) {
        LOG_DEBUG("[SR] Not relaying unicast to unknown destination %s", destName);
        return false;
    }

    NodeNum sourceNode = p->from;
    NodeNum heardFrom = resolveHeardFrom(p, sourceNode);

    // If we heard directly from source, check if next hop already has the packet
    if (heardFrom == sourceNode) {
        NodeNum nextHop = getNextHop(p->to, sourceNode, heardFrom, false);
        if (nextHop != 0 && nextHop != nodeDB->getNodeNum()) {
            // Check if source and next hop are neighbors
            bool nextHopHeardFromSource = false;
#ifdef SIGNAL_ROUTING_LITE_MODE
            const NodeEdgesLite* sourceEdges = routingGraph->getEdgesFrom(sourceNode);
            if (sourceEdges) {
                for (uint8_t i = 0; i < sourceEdges->edgeCount; i++) {
                    if (sourceEdges->edges[i].to == nextHop) {
                        nextHopHeardFromSource = true;
                        break;
                    }
                }
            }
            if (!nextHopHeardFromSource) {
                const NodeEdgesLite* nextHopEdges = routingGraph->getEdgesFrom(nextHop);
                if (nextHopEdges) {
                    for (uint8_t i = 0; i < nextHopEdges->edgeCount; i++) {
                        if (nextHopEdges->edges[i].to == sourceNode) {
                            nextHopHeardFromSource = true;
                            break;
                        }
                    }
                }
            }
#else
            auto sourceEdges = routingGraph->getEdgesFrom(sourceNode);
            if (sourceEdges) {
                for (const Edge& e : *sourceEdges) {
                    if (e.to == nextHop) {
                        nextHopHeardFromSource = true;
                        break;
                    }
                }
            }
            if (!nextHopHeardFromSource) {
                auto nextHopEdges = routingGraph->getEdgesFrom(nextHop);
                if (nextHopEdges) {
                    for (const Edge& e : *nextHopEdges) {
                        if (e.to == sourceNode) {
                            nextHopHeardFromSource = true;
                            break;
                        }
                    }
                }
            }
#endif
            if (nextHopHeardFromSource) {
                char nextHopName[64];
                getNodeDisplayName(nextHop, nextHopName, sizeof(nextHopName));
                LOG_DEBUG("[SR] Unicast: next hop %s already heard from source %s - no relay needed",
                         nextHopName, senderName);
                return false;
            }
        }
    }

    // Use the contention-based unicast relay coordination
    return shouldRelayUnicastForCoordination(p);
}

bool SignalRoutingModule::shouldRelayBroadcast(const meshtastic_MeshPacket *p)
{
    if (!routingGraph || !nodeDB) {
        return true;
    }

    // Skip SR processing entirely for packets we sent (detected as rebroadcasts)
    // This prevents processing our own packets that come back to us
    NodeNum ourNode = nodeDB->getNodeNum();
    uint32_t currentTime = millis() / 1000;
    if (routingGraph->hasNodeTransmitted(ourNode, p->id, currentTime)) {
        LOG_DEBUG("[SR] Skipping relay decision for rebroadcast of our packet %08x", p->id);
        return false; // Don't relay our own rebroadcasted packets
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

    // Compute packet received timestamp once for all SignalRouting operations
    uint32_t packetReceivedTimestamp = millis() / 1000;

    // Only access decoded fields if packet is actually decoded
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        p->decoded.portnum == meshtastic_PortNum_SIGNAL_ROUTING_APP) {
        preProcessSignalRoutingPacket(p, packetReceivedTimestamp);
    }

    // Now check if our topology is healthy for making relay decisions
    // If not healthy, default to relay (conservative behavior)
    if (!topologyHealthyForBroadcast()) {
        return true;
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

    uint32_t relayDecisionTime = packetReceivedTimestamp; // Use the packet received timestamp computed above

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
    bool shouldRelay = routingGraph->shouldRelayEnhanced(myNode, sourceNode, heardFrom, relayDecisionTime, p->id, packetReceivedTimestamp);

    // Apply conservative logic only when NOT required for branch coverage
    if (shouldRelay && hasStockGateways && !mustRelayForBranchCoverage) {
        LOG_DEBUG("[SR] Applying conservative relay logic (stock gateways present, not from gateway)");
        shouldRelay = routingGraph->shouldRelayEnhancedConservative(myNode, sourceNode, heardFrom, relayDecisionTime, p->id, packetReceivedTimestamp);
        if (!shouldRelay) {
            LOG_DEBUG("[SR] Suppressed SR relay - stock gateway can handle external transmission");
        } else {
            LOG_DEBUG("[SR] SR relay proceeding despite conservative logic");
        }
    }
#else
    bool shouldRelay = routingGraph->shouldRelayEnhanced(myNode, sourceNode, heardFrom, relayDecisionTime, p->id, packetReceivedTimestamp);

    // Apply conservative logic only when NOT required for branch coverage
    if (shouldRelay && hasStockGateways && !mustRelayForBranchCoverage) {
        LOG_DEBUG("[SR] Applying conservative relay logic (stock gateways present, not from gateway)");
        shouldRelay = routingGraph->shouldRelayEnhancedConservative(myNode, sourceNode, heardFrom, relayDecisionTime, p->id, packetReceivedTimestamp);
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
        bool stockCoverageNeeded = shouldRelayForStockNeighbors(myNode, sourceNode, heardFrom, relayDecisionTime);
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

        // CRITICAL: Verify the next hop can hear the transmitting node (heardFrom)
        // If heardFrom is known and next hop didn't hear the transmission, it can't relay
        bool nextHopCanHearTransmitter = true;  // Assume true if we can't verify
        bool connectivityUnknown = false;
        
        if (heardFrom != 0 && route.nextHop != heardFrom) {
            // Use enhanced connectivity check that handles stock firmware nodes
            nextHopCanHearTransmitter = hasVerifiedConnectivity(heardFrom, route.nextHop, &connectivityUnknown);
            
            if (!nextHopCanHearTransmitter) {
                char heardFromName[64];
                getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));
                
                if (connectivityUnknown) {
                    // Stock node involved - we can't verify connectivity
                    // Be conservative: don't trust unverified relays, relay ourselves
                    LOG_DEBUG("[SR] Route via %s rejected - cannot verify connectivity to %s (stock/unknown node)",
                             nextHopName, heardFromName);
                } else {
                    // Both are SR-active but no edge exists - they likely can't hear each other
                    LOG_DEBUG("[SR] Route via %s rejected - no connectivity to transmitter %s",
                             nextHopName, heardFromName);
                }
                // Don't return this next hop - fall through to try alternatives
            }
        }

        if (nextHopCanHearTransmitter) {
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
        
        // Next hop can't hear transmitter - try to find alternative through opportunistic routing
        if (allowOpportunistic) {
            NodeNum betterNeighbor = findBetterPositionedNeighbor(destination, sourceNode, heardFrom, 
                                                                  std::numeric_limits<float>::infinity(), currentTime);
            if (betterNeighbor != 0) {
                char altName[64];
                getNodeDisplayName(betterNeighbor, altName, sizeof(altName));
                LOG_DEBUG("[SR] Using alternative next hop %s (can hear transmitter)", altName);
                return betterNeighbor;
            }
        }
        
        // No alternative found - indicate we should relay ourselves
        // by returning our own node number
        NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
        if (myNode != 0) {
            LOG_DEBUG("[SR] No next hop can hear transmitter - we should relay ourselves");
            return myNode;
        }
    }

    // Fallback 1: if we know a gateway for this destination, and we have a direct link to it, forward there
    // But only if the gateway can hear the transmitter (heardFrom)
    NodeNum gatewayForDest = getGatewayFor(destination);
    if (gatewayForDest != 0 && nodeDB) {
        // Verify gateway can hear transmitter before using it
        bool gatewayCanHearTransmitter = true;
        bool connectivityUnknown = false;
        if (heardFrom != 0 && gatewayForDest != heardFrom) {
            gatewayCanHearTransmitter = hasVerifiedConnectivity(heardFrom, gatewayForDest, &connectivityUnknown);
        }
        
        // Only use gateway if we can verify connectivity (be conservative with stock nodes)
        if (gatewayCanHearTransmitter && !connectivityUnknown) {
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
        } else {
            char gwName[64], heardFromName[64];
            getNodeDisplayName(gatewayForDest, gwName, sizeof(gwName));
            getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));
            if (connectivityUnknown) {
                LOG_DEBUG("[SR] Gateway %s connectivity to %s unknown (stock node) - skipping", gwName, heardFromName);
            } else {
                LOG_DEBUG("[SR] Gateway %s cannot hear transmitter %s - skipping", gwName, heardFromName);
            }
        }
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

        // CRITICAL: Only consider neighbors that can hear the transmitting node (heardFrom)
        // If they didn't hear the transmission, they can't relay it
        if (heardFrom != 0) {
            bool connectivityUnknown = false;
            bool canHearTransmitter = hasVerifiedConnectivity(heardFrom, neighbor, &connectivityUnknown);
            if (!canHearTransmitter) {
                // Skip if no connectivity, or if connectivity is unknown (stock node - be conservative)
                continue;
            }
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

        // CRITICAL: Only consider neighbors that can hear the transmitting node (heardFrom)
        // If they didn't hear the transmission, they can't relay it
        if (heardFrom != 0) {
            bool connectivityUnknown = false;
            bool canHearTransmitter = hasVerifiedConnectivity(heardFrom, neighbor, &connectivityUnknown);
            if (!canHearTransmitter) {
                // Skip if no connectivity, or if connectivity is unknown (stock node - be conservative)
                continue;
            }
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

    // IMPORTANT: Use monotonic time (seconds since boot) for edge timestamps, not RTC time
    uint32_t monotonicTimestamp = millis() / 1000;
    (void)lastRxTime;  // Unused - we use monotonic time instead

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
        routingGraph->updateEdge(nodeId, myNode, etx, monotonicTimestamp, variance, EdgeLite::Source::Reported);
#else
        routingGraph->updateEdge(nodeId, myNode, etx, monotonicTimestamp, variance, Edge::Source::Reported);
#endif

    // Also store reverse edge: us → nodeId (assuming approximately symmetric link) if we don't yet have a better
    // (reported) estimate of how nodeId hears us. This serves as a fallback until we receive their SR info.
    routingGraph->updateEdge(myNode, nodeId, etx, monotonicTimestamp, variance
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
            // We now have a DIRECT connection to this node - clear any gateway relationships
            // that were created based on topology broadcasts before we heard from them directly
            clearDownstreamFromAllGateways(nodeId);

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


float SignalRoutingModule::getDirectNeighborsSignalActivePercentage() const
{
    if (!routingGraph || !nodeDB) {
        return 0.0f;
    }

    // Get our direct neighbors from the graph
    size_t totalNeighbors = 0;
    size_t activeNeighbors = 0;

#ifdef SIGNAL_ROUTING_LITE_MODE
    const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (edges) {
        totalNeighbors = edges->edgeCount;
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (getCapabilityStatus(edges->edges[i].to) == CapabilityStatus::SRactive) {
                activeNeighbors++;
            }
        }
    }
#else
    auto edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (edges) {
        totalNeighbors = edges->size();
        for (const Edge& edge : *edges) {
            if (getCapabilityStatus(edge.to) == CapabilityStatus::SRactive) {
                activeNeighbors++;
            }
        }
    }
#endif

    if (totalNeighbors == 0) {
        return 0.0f;
    }

    float percentage = (static_cast<float>(activeNeighbors) * 100.0f) / static_cast<float>(totalNeighbors);
    LOG_DEBUG("[SR] Direct neighbor capability: %d/%d = %.1f%%", activeNeighbors, totalNeighbors, percentage);
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
            if (age > ttl) {
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

    // Check if this is a known mute node (signal_routing_active = false)
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

    // Don't return placeholders - they should be resolved to real nodes
    if (isPlaceholderNode(bestNode)) {
        return 0;
    }
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

    // Check ALL known nodes (both Reported and Mirrored edges), not just direct neighbors,
    // because the relay might be a node we only know through topology broadcasts
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
        const std::vector<Edge>* myEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
        if (myEdges) {
            for (const Edge& edge : *myEdges) {
                if ((edge.to & 0xFF) == p->relay_node) {
                    // Remember this mapping for future use
                    const_cast<SignalRoutingModule*>(this)->rememberRelayIdentity(edge.to, p->relay_node);
                    return edge.to;
                }
            }
        }
#endif
    }

    return sourceNode;
}

uint64_t SignalRoutingModule::makeSpeculativeKey(NodeNum origin, uint32_t packetId)
{
    return (static_cast<uint64_t>(origin) << 32) | packetId;
}

bool SignalRoutingModule::hasNodeBeenExcludedFromRelay(NodeNum nodeId, PacketId packetId)
{
    uint64_t packetKey = makeSpeculativeKey(0, packetId); // Use 0 as origin since we just need packet ID

#ifdef SIGNAL_ROUTING_LITE_MODE
    for (uint8_t i = 0; i < relayExclusionCount; i++) {
        if (relayExclusions[i].packetKey == packetKey) {
            for (uint8_t j = 0; j < relayExclusions[i].exclusionCount; j++) {
                if (relayExclusions[i].excludedNodes[j] == nodeId) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
#else
    auto it = relayExclusions.find(packetKey);
    if (it != relayExclusions.end()) {
        return it->second.find(nodeId) != it->second.end();
    }
    return false;
#endif
}

void SignalRoutingModule::scheduleContentionWindowCheck(NodeNum expectedRelay, PacketId packetId, NodeNum destination, const meshtastic_MeshPacket *packet)
{
    if (!packet) return;

    // Calculate dynamic contention window based on radio factors (similar to retransmission timeout)
    uint32_t contentionWindowMs = router->getRadioInterface()->getContentionWindowMsec(packet);

    // Store packet info needed for re-evaluation and potential relay
    NodeNum sourceNode = packet->from;
    NodeNum heardFrom = resolveHeardFrom(packet, sourceNode);

#ifdef SIGNAL_ROUTING_LITE_MODE
    if (contentionCheckCount >= 4) {
        return; // No room for more checks
    }

    ContentionCheck& check = contentionChecks[contentionCheckCount++];
    check.expectedRelay = expectedRelay;
    check.packetId = packetId;
    check.destination = destination;
    check.sourceNode = sourceNode;
    check.heardFrom = heardFrom;
    check.hopLimit = packet->hop_limit;
    check.hopStart = packet->hop_start;
    check.expiryMs = millis() + contentionWindowMs;
    check.needsRelay = false;
#else
    ContentionCheck check;
    check.expectedRelay = expectedRelay;
    check.packetId = packetId;
    check.destination = destination;
    check.sourceNode = sourceNode;
    check.heardFrom = heardFrom;
    check.hopLimit = packet->hop_limit;
    check.hopStart = packet->hop_start;
    check.expiryMs = millis() + contentionWindowMs;
    check.needsRelay = false;
    contentionChecks.push_back(check);
#endif
}

void SignalRoutingModule::excludeNodeFromRelay(NodeNum nodeId, PacketId packetId)
{
    uint64_t packetKey = makeSpeculativeKey(0, packetId);

#ifdef SIGNAL_ROUTING_LITE_MODE
    // Find existing exclusion record or create new one
    for (uint8_t i = 0; i < relayExclusionCount; i++) {
        if (relayExclusions[i].packetKey == packetKey) {
            if (relayExclusions[i].exclusionCount < 4) {
                relayExclusions[i].excludedNodes[relayExclusions[i].exclusionCount++] = nodeId;
            }
            return;
        }
    }

    // Create new exclusion record
    if (relayExclusionCount < 4) {
        RelayExclusion& exclusion = relayExclusions[relayExclusionCount++];
        exclusion.packetKey = packetKey;
        exclusion.excludedNodes[0] = nodeId;
        exclusion.exclusionCount = 1;
    }
#else
    relayExclusions[packetKey].insert(nodeId);
#endif
}

void SignalRoutingModule::clearRelayExclusionsForPacket(PacketId packetId)
{
    uint64_t packetKey = makeSpeculativeKey(0, packetId);

#ifdef SIGNAL_ROUTING_LITE_MODE
    for (uint8_t i = 0; i < relayExclusionCount; i++) {
        if (relayExclusions[i].packetKey == packetKey) {
            // Remove this exclusion by shifting the rest
            if (i < relayExclusionCount - 1) {
                relayExclusions[i] = relayExclusions[relayExclusions[i].exclusionCount - 1];
            }
            relayExclusionCount--;
            return;
        }
    }
#else
    relayExclusions.erase(packetKey);
#endif
}

void SignalRoutingModule::processContentionWindows(uint32_t nowMs)
{
    if (!routingGraph || !nodeDB) return;

    NodeNum myNode = nodeDB->getNodeNum();

#ifdef SIGNAL_ROUTING_LITE_MODE
    for (uint8_t i = 0; i < contentionCheckCount;) {
        if (nowMs >= contentionChecks[i].expiryMs) {
            ContentionCheck& check = contentionChecks[i];

            // Contention window expired - exclude the expected relay from future attempts
            excludeNodeFromRelay(check.expectedRelay, check.packetId);

            char destName[64], relayName[64];
            getNodeDisplayName(check.destination, destName, sizeof(destName));
            getNodeDisplayName(check.expectedRelay, relayName, sizeof(relayName));

            LOG_DEBUG("[SR] Contention window expired for relay %s on packet %08x to %s - excluding from future attempts",
                     relayName, check.packetId, destName);

            // Re-evaluate: find next best candidate or relay ourselves
            bool shouldRelayNow = evaluateContentionExpiry(check, myNode);

            if (shouldRelayNow && check.hopLimit > 0) {
                LOG_INFO("[SR] Contention re-evaluation: relaying packet %08x to %s ourselves", check.packetId, destName);
                queueUnicastRelay(check);
            }

            // Remove this check by shifting the rest
            if (i < contentionCheckCount - 1) {
                contentionChecks[i] = contentionChecks[contentionCheckCount - 1];
            }
            contentionCheckCount--;
        } else {
            i++;
        }
    }
#else
    // Process expired contention checks
    for (auto it = contentionChecks.begin(); it != contentionChecks.end();) {
        if (nowMs >= it->expiryMs) {
            // Contention window expired - exclude the expected relay from future attempts
            excludeNodeFromRelay(it->expectedRelay, it->packetId);

            char destName[64], relayName[64];
            getNodeDisplayName(it->destination, destName, sizeof(destName));
            getNodeDisplayName(it->expectedRelay, relayName, sizeof(relayName));

            LOG_DEBUG("[SR] Contention window expired for relay %s on packet %08x to %s - excluding from future attempts",
                     relayName, it->packetId, destName);

            // Re-evaluate: find next best candidate or relay ourselves
            bool shouldRelayNow = evaluateContentionExpiry(*it, myNode);

            if (shouldRelayNow && it->hopLimit > 0) {
                LOG_INFO("[SR] Contention re-evaluation: relaying packet %08x to %s ourselves", it->packetId, destName);
                queueUnicastRelay(*it);
            }

            it = contentionChecks.erase(it);
        } else {
            ++it;
        }
    }
#endif
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

// Enhanced connectivity check that considers stock firmware limitations
// Returns: true = verified connectivity, false = no connectivity or unknown
// The unknownOut parameter indicates if we couldn't verify (stock node involved)
bool SignalRoutingModule::hasVerifiedConnectivity(NodeNum transmitter, NodeNum receiver, bool* unknownOut)
{
    if (!routingGraph || !nodeDB) {
        if (unknownOut) *unknownOut = true;
        return false;
    }

    // Get capability status of both nodes
    CapabilityStatus txStatus = getCapabilityStatus(transmitter);
    CapabilityStatus rxStatus = getCapabilityStatus(receiver);
    
    bool txIsStock = isPlaceholderNode(transmitter) || 
                     txStatus == CapabilityStatus::Legacy || 
                     txStatus == CapabilityStatus::Unknown;
    bool rxIsStock = isPlaceholderNode(receiver) || 
                     rxStatus == CapabilityStatus::Legacy || 
                     rxStatus == CapabilityStatus::Unknown;

    // If both are stock, we have no topology data - unknown
    if (txIsStock && rxIsStock) {
        if (unknownOut) *unknownOut = true;
        return false;
    }

    // Check both directions for edges:
    // 1. transmitter → receiver: transmitter reported hearing receiver
    // 2. receiver → transmitter: receiver reported hearing transmitter
    
    bool foundEdge = false;
    
    // Check transmitter → receiver (only useful if transmitter is SR-active)
    if (!txIsStock) {
        if (hasDirectConnectivity(transmitter, receiver)) {
            foundEdge = true;
        }
    }
    
    // Check receiver → transmitter (only useful if receiver is SR-active)
    if (!foundEdge && !rxIsStock) {
        if (hasDirectConnectivity(receiver, transmitter)) {
            foundEdge = true;
        }
    }
    
    if (foundEdge) {
        if (unknownOut) *unknownOut = false;
        return true;
    }
    
    // No edge found. Determine if this is "unknown" or "no connectivity"
    // If the SR-active node has edges but none to the stock node, it's likely no connectivity
    // But if the SR-active node has very few edges or the stock node is new, it could be unknown
    
    if (txIsStock || rxIsStock) {
        // One node is stock - we can't be certain, mark as unknown
        if (unknownOut) *unknownOut = true;
    } else {
        // Both are SR-active and no edge exists - they likely can't hear each other
        if (unknownOut) *unknownOut = false;
    }
    
    return false;
}

bool SignalRoutingModule::evaluateContentionExpiry(const ContentionCheck& check, NodeNum myNode)
{
    if (!routingGraph || !nodeDB) return false;

    // Try to find an alternative next hop that can hear the transmitter
    NodeNum nextHop = getNextHop(check.destination, check.sourceNode, check.heardFrom, true);

    // If no route found, we should relay ourselves
    if (nextHop == 0) {
        LOG_DEBUG("[SR] Contention re-eval: No route to %08x - will relay ourselves", check.destination);
        return true;
    }

    // If we are the next hop, relay
    if (nextHop == myNode || nextHop == check.destination) {
        return true;
    }

    // If the new next hop is also excluded, relay ourselves
    if (hasNodeBeenExcludedFromRelay(nextHop, check.packetId)) {
        LOG_DEBUG("[SR] Contention re-eval: Alternative next hop %08x also excluded - will relay ourselves", nextHop);
        return true;
    }

    // Check if the new next hop can hear the transmitter
    bool canHear = hasDirectConnectivity(check.heardFrom, nextHop) ||
                   hasDirectConnectivity(nextHop, check.heardFrom);

    if (!canHear) {
        // New candidate also can't hear transmitter
        CapabilityStatus status = getCapabilityStatus(nextHop);
        if (isPlaceholderNode(nextHop) || status == CapabilityStatus::Legacy || status == CapabilityStatus::Unknown) {
            // Stock/placeholder - can't verify, relay ourselves
            LOG_DEBUG("[SR] Contention re-eval: Alternative %08x is stock/unknown and can't verify hearing - relay ourselves", nextHop);
            return true;
        }
    }

    // We found a valid alternative candidate - schedule another contention check for them
    // But we don't have the original packet anymore, so we can't reschedule properly
    // For now, just relay ourselves if we've exhausted primary candidates
    LOG_DEBUG("[SR] Contention re-eval: Found alternative candidate %08x but cannot reschedule - relay ourselves", nextHop);
    return true;
}

void SignalRoutingModule::queueUnicastRelay(const ContentionCheck& check)
{
    if (!router || !nodeDB) return;

    // Create a minimal packet for relay
    // Note: We don't have the full payload, so we can only attempt to trigger
    // a "delayed relay" by notifying the router. However, the original packet
    // may have already been delivered/discarded.

    // For now, log that we would have relayed - the original packet is no longer available
    // This is a limitation: once we decide not to relay, we can't recover the packet.

    // The real fix is that the initial decision in shouldRelayUnicastForCoordination
    // should be more conservative and not wait for unreliable candidates.
    // The contention window expiry is a fallback for when we did wait but shouldn't have.

    char destName[64];
    getNodeDisplayName(check.destination, destName, sizeof(destName));
    LOG_WARN("[SR] queueUnicastRelay: Packet %08x to %s - original packet no longer available for relay",
             check.packetId, destName);

    // Clear exclusions for this packet since we've handled it
    clearRelayExclusionsForPacket(check.packetId);
}
