#include "SignalRoutingModule.h"
#include "graph/NeighborGraph.h"
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
    LOG_INFO("[SR] Using NeighborGraph");
    routingGraph = new NeighborGraph();

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

        // Periodic topology logging
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
    trackNodeCapability(nodeDB->getNodeNum(), isActiveRoutingRole() ? CapabilityStatus::SRactive : CapabilityStatus::Passive);
}
void SignalRoutingModule::collectNeighborsForBroadcast(std::vector<meshtastic_SignalNeighbor> &allNeighbors)
{
    if (!routingGraph || !nodeDB) return;

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
        NeighborGraph::etxToSignal(edge->getEtx(), rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));

        allNeighbors.push_back(neighbor);
    }
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

    const NodeEdgesLite* nodeEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!nodeEdges || nodeEdges->edgeCount == 0) {
        return;
    }

    // Prefer reported edges (peer perspective) over mirrored estimates, then order by ETX
    const EdgeLite* reported[NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE];
    const EdgeLite* mirrored[NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE];
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

    // For non-active nodes, only broadcast directly heard neighbors (Reported edges)
    // Active routing nodes can broadcast full topology including relayed connections
    bool isActive = isActiveRoutingRole();

    for (uint8_t i = 0; i < reportedCount && selectedCount < MAX_SIGNAL_ROUTING_NEIGHBORS; i++) {
        selected[selectedCount++] = reported[i];
    }

    // Only include mirrored edges for active nodes
    if (isActive) {
        for (uint8_t i = 0; i < mirroredCount && selectedCount < MAX_SIGNAL_ROUTING_NEIGHBORS; i++) {
            selected[selectedCount++] = mirrored[i];
        }
    }

    // Filter out placeholders before assigning to neighbors array
    const EdgeLite* filteredSelected[NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE];
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
        NeighborGraph::etxToSignal(edge.getEtx(), rssi32, snr32);
        neighbor.rssi = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, rssi32)));
        neighbor.snr = static_cast<int8_t>(std::max((int32_t)-128, std::min((int32_t)127, snr32)));
    }
}

void SignalRoutingModule::updateGraphWithNeighbor(NodeNum sender, const meshtastic_SignalNeighbor &neighbor)
{
    // Add/update edge from sender to this neighbor
    if (routingGraph) {
        float etx = 1.0f; // Default ETX, will be updated with real measurements
        uint32_t currentTime = millis() / 1000;

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
    
    // For passive nodes: only process SR broadcasts from direct neighbors
    // Active nodes: process all SR broadcasts for full topology
    if (!isActiveRoutingRole()) {
        // Passive node: check if SR broadcast is from direct sender
        if (p->hop_start != p->hop_limit) {
            LOG_DEBUG("[SR] Passive role: Ignoring SR broadcast from 0x%08x (not direct, hopStart=%d, hopLimit=%d)",
                     p->from, p->hop_start, p->hop_limit);
            return;
        }
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
    CapabilityStatus newStatus = info.signal_routing_active ? CapabilityStatus::SRactive : CapabilityStatus::Passive;
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

    // For new topology versions, clear only topology-learned (Mirrored) edges
    // Preserve direct radio (Reported) edges which represent actual connectivity
    if (isNewVersion && routingGraph) {
        routingGraph->clearEdgesForNode(p->from);
        LOG_DEBUG("[SR] Cleared topology-learned edges for node %08x (new version)", p->from);
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
        // but nodes we can hear directly are not incorrectly marked as downstream
        bool hasDirectConnection = false;
        NodeNum ourNode = nodeDB ? nodeDB->getNodeNum() : 0;
        
        // Never mark ourselves as downstream of anyone
        if (neighbor.node_id == ourNode) {
            hasDirectConnection = true;
        } else if (routingGraph) {
            // Check if we have a direct radio connection to this neighbor
            // A direct connection exists if we have a Reported edge FROM us TO the neighbor
            // This represents our actual reception of their signal with RSSI/SNR data
            // Check edges FROM us TO neighbor with Reported source (actual direct radio connection)
            const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
            if (ourEdges) {
                for (uint8_t j = 0; j < ourEdges->edgeCount; j++) {
                    if (ourEdges->edges[j].to == neighbor.node_id &&
                        ourEdges->edges[j].source == EdgeLite::Source::Reported) {
                        hasDirectConnection = true;
                        break;
                    }
                }
            }
        }
        
        // Topology broadcasts share graph connectivity (ETX/routing information) and establish hierarchy
        // Create topology-based gateway relationships for all neighbors in broadcasts. This creates
        // an information-source hierarchy where nodes appear under the SR node that provided
        // their topology information, regardless of relaying history.

        // Detailed logging for debugging topology processing
        char neighborName[48];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));

        if (!hasDirectConnection) {
            // Establish topology-based hierarchy: nodes learned through topology broadcasts
            // appear as downstream of the broadcasting node, creating a information-source hierarchy
            LOG_INFO("[SR]   -> %s: NO direct connection, marking as downstream of topology source %s",
                    neighborName, senderNameForTopo);
            float etxForDownstream = NeighborGraph::calculateETX(neighbor.rssi, neighbor.snr);
            routingGraph->updateDownstream(neighbor.node_id, p->from, etxForDownstream, millis() / 1000);
        } else {
            LOG_DEBUG("[SR]   -> %s: HAS direct connection, sender confirms reachability",
                    neighborName);
        }
    }

    // Update last processed version (minimal state tracking)
    lastTopologyVersion[p->from] = receivedVersion;

    // Record that this version was pre-processed so handleReceivedProtobuf can skip redundant work
    lastPreProcessedVersion[p->from] = receivedVersion;

}
bool SignalRoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_SignalRoutingInfo *p)
{
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
    // ALL nodes (active and passive) should track capability status of other SR nodes
    CapabilityStatus newStatus = p->signal_routing_active ? CapabilityStatus::SRactive : CapabilityStatus::Passive;
    CapabilityStatus oldStatus = getCapabilityStatus(mp.from);
    trackNodeCapability(mp.from, newStatus);

    if (oldStatus != newStatus) {
        LOG_INFO("[SR] Capability update: %s changed from %d to %d",
                senderName, (int)oldStatus, (int)newStatus);
    }

    // Inactive SR roles don't participate in routing decisions - skip topology learning from broadcasts
    // But they still tracked the sender's capability above
    if (!isActiveRoutingRole()) {
        LOG_DEBUG("[SR] Passive role: Tracking capability from %s but not processing topology (node count %d)",
                  senderName, p->neighbors_count);
        return false;
    }

    if (p->neighbors_count == 0) {
        LOG_INFO("[SR] %s is online (SR v%d, %s) - no neighbors detected yet",
                 senderName, p->routing_version,
                 p->signal_routing_active ? "SR-active" : "passive");

        // Clear downstream entries for SR-capable nodes with no neighbors - they can't be relays
        if (p->signal_routing_active) {
            routingGraph->clearDownstreamForRelay(mp.from);
        }

        return false;
    }

    LOG_INFO("[SR] RECEIVED: %s reports %d neighbors (SR v%d, %s)",
             senderName, p->neighbors_count, p->routing_version,
             p->signal_routing_active ? "SR-active" : "passive");

    // Set cyan for network topology update (operation start)
    setRgbLed(0, 255, 255);

    // For passive SR nodes (signal_routing_active = false), we still need to store their edges for direct connection checks
    // Active nodes use these edges to determine if a passive SR node has direct connections to destinations
    // However, routing algorithms must not consider paths through passive SR nodes since they don't relay
    if (!p->signal_routing_active) {
        LOG_DEBUG("[SR] Received topology from passive SR node %08x - storing edges for direct connection detection", mp.from);
    }

    // Check if preProcessSignalRoutingPacket already handled edge clearing and rebuilding
    // for this exact version — skip redundant work if so
    auto preProcessIt = lastPreProcessedVersion.find(mp.from);
    bool alreadyPreProcessed = (preProcessIt != lastPreProcessedVersion.end() &&
                                preProcessIt->second == p->routing_version);

    if (!alreadyPreProcessed) {
        // Clear all existing edges for this node before adding the new ones from the broadcast
        // This ensures our view of the sender's connectivity matches exactly what it reported
        routingGraph->clearEdgesForNode(mp.from);
        // Also clear any inferred connectivity edges pointing TO this node that were created
        // before we knew it was SR-capable
        routingGraph->clearInferredEdgesToNode(mp.from);

        // Add edges from each neighbor TO the sender
        uint32_t rxTime = millis() / 1000;
        for (pb_size_t i = 0; i < p->neighbors_count; i++) {
            const meshtastic_SignalNeighbor& neighbor = p->neighbors[i];

            if (neighbor.node_id == 0 || isPlaceholderNode(neighbor.node_id)) {
                continue;
            }

            float etx = NeighborGraph::calculateETX(neighbor.rssi, neighbor.snr);

            uint32_t scaledVariance = static_cast<uint32_t>(neighbor.position_variance) * 12;

            routingGraph->updateEdge(neighbor.node_id, mp.from, etx, rxTime, scaledVariance,
                                     EdgeLite::Source::Reported);
            routingGraph->updateEdge(mp.from, neighbor.node_id, etx, rxTime, scaledVariance,
                                     EdgeLite::Source::Mirrored);
        }
    } else {
        LOG_DEBUG("[SR] Skipping redundant edge rebuild for %s (already pre-processed version %u)",
                 senderName, p->routing_version);
    }

    // Always process gateway relations and logging (even if edges were already built)
    for (pb_size_t i = 0; i < p->neighbors_count; i++) {
        const meshtastic_SignalNeighbor& neighbor = p->neighbors[i];

        if (neighbor.node_id == 0 || isPlaceholderNode(neighbor.node_id)) {
            continue;
        }

        char neighborName[64];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));

        float etx = NeighborGraph::calculateETX(neighbor.rssi, neighbor.snr);

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
        // clear downstream entries for this neighbor - it's now reachable via the SR network
        if (p->signal_routing_active) {
            NodeNum relayForNeighbor = routingGraph->getDownstreamRelay(neighbor.node_id);
            if (relayForNeighbor != 0 && relayForNeighbor != mp.from) {
                char gwName[64];
                getNodeDisplayName(relayForNeighbor, gwName, sizeof(gwName));
                LOG_INFO("[SR] Clearing downstream for %s (now directly reachable via %s, was via %s)",
                         neighborName, senderName, gwName);
                routingGraph->clearDownstreamForDestination(neighbor.node_id);
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

        // Only transfer reverse edges (where our node had a direct link to the placeholder)
        // These represent actual radio connectivity that should be preserved
        const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
        if (ourEdges) {
            for (uint8_t i = 0; i < ourEdges->edgeCount; i++) {
                if (ourEdges->edges[i].to == placeholderId) {
                    float etx = ourEdges->edges[i].getEtx();
                    // Create equivalent edge from our node to real node
                    routingGraph->updateEdge(ourNode, realNodeId, etx, millis() / 1000);
                }
            }
        }

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

    // The downstream table in NeighborGraph is private, so we can't directly iterate it.
    // Clear downstream entries for the old node - the table will self-correct
    // via aging and new topology broadcasts.
    if (routingGraph) {
        routingGraph->clearDownstreamForRelay(oldNode);
        routingGraph->clearDownstreamForDestination(oldNode);
    }
}

bool SignalRoutingModule::isPlaceholderConnectedToUs(NodeNum placeholderId) const
{
    if (!routingGraph || !isPlaceholderNode(placeholderId)) {
        return false;
    }

    // Check if the placeholder has edges connected to our node
    NodeNum ourNode = nodeDB->getNodeNum();

    const NodeEdgesLite* edges = routingGraph->getEdgesFrom(placeholderId);
    if (edges) {
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (edges->edges[i].to == ourNode) {
                return true;
            }
        }
    }

    return false;
}

bool SignalRoutingModule::shouldRelayForStockNeighbors(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime)
{
    if (!routingGraph) {
        return false;
    }

    // Find stock firmware nodes that might need coverage: direct neighbors + downstream nodes
    std::vector<NodeNum> stockNeighbors;

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
        const NodeEdgesLite* sourceEdges = routingGraph->getEdgesFrom(sourceNode);
        if (sourceEdges) {
            for (uint8_t i = 0; i < sourceEdges->edgeCount; i++) {
                if (sourceEdges->edges[i].to == stockNeighbor) {
                    heardDirectly = true;
                    break;
                }
            }
        }

        // If not heard from source, check if heard from relaying SR node
        if (!heardDirectly) {
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
        }

        if (!heardDirectly) {
            hasUncoveredStockNeighbor = true;
            LOG_DEBUG("[SR] Stock neighbor %08x did not hear transmission directly", stockNeighbor);

            // Check if we're the best positioned to reach this stock neighbor
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
    NodeNum relay = routingGraph->getDownstreamRelay(destination);
    if (relay != 0) {
        // Check if we have a direct connection to this relay
        const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
        if (myEdges) {
            for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                if (myEdges->edges[i].to == relay) {
                    LOG_INFO("[SR] Found downstream: %08x is downstream of relay %08x (direct neighbor)", destination, relay);
                    return true;
                }
            }
        }
    }

    return false;
}

uint32_t SignalRoutingModule::getNodeTtlSeconds(CapabilityStatus status) const
{
    // Only known SR-active/inactive nodes get shorter TTL (they send regular broadcasts)
    // Stock nodes, legacy routers, and unknown nodes need longer TTL since they
    // transmit less frequently and we don't want to age them out prematurely
    if (status == CapabilityStatus::SRactive || status == CapabilityStatus::Passive) {
        return ACTIVE_NODE_TTL_SECS;  // 5 minutes for known SR nodes
    }
    // Legacy, Unknown, and any other status get longer TTL
    return MUTE_NODE_TTL_SECS;  // 30 minutes for stock/unknown nodes
}

void SignalRoutingModule::logNetworkTopology()
{
    if (!routingGraph) return;

    // Use fixed-size arrays only, no heap allocations
    NodeNum nodeBuf[NEIGHBOR_GRAPH_MAX_NEIGHBORS];
    size_t rawNodeCount = routingGraph->getAllNodeIds(nodeBuf, NEIGHBOR_GRAPH_MAX_NEIGHBORS);

    // For passive nodes: only show nodes that have edges (direct neighbors)
    // For active nodes: show all nodes in graph
    size_t nodeCount = rawNodeCount;
    if (!isActiveRoutingRole()) {
        // Filter to only nodes with edges
        size_t filteredCount = 0;
        for (size_t i = 0; i < rawNodeCount; i++) {
            const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeBuf[i]);
            if (edges && edges->edgeCount > 0) {
                nodeBuf[filteredCount++] = nodeBuf[i];
            }
        }
        nodeCount = filteredCount;
    }

    if (nodeCount == 0) {
        LOG_INFO("[SR] Network Topology: No nodes in graph yet");
        return;
    }
    LOG_INFO("[SR] Network Topology: %d nodes total", nodeCount);

    // Sort in place using fixed array (avoid std::vector heap allocation)
    std::sort(nodeBuf, nodeBuf + nodeCount);

    // Display each node and its neighbors
    for (size_t nodeIdx = 0; nodeIdx < nodeCount; nodeIdx++) {
        NodeNum nodeId = nodeBuf[nodeIdx];
        char nodeName[48];
        getNodeDisplayName(nodeId, nodeName, sizeof(nodeName));

        CapabilityStatus status = getCapabilityStatus(nodeId);
        const char* prefix = "";
        const char* statusStr = "unknown";
        if (status == CapabilityStatus::SRactive) {
            prefix = "[SR-active] ";
            statusStr = "SR-active";
        } else if (status == CapabilityStatus::Passive) {
            prefix = "[SR-passive] ";
            statusStr = "passive";
        } else if (status == CapabilityStatus::Legacy) {
            statusStr = "legacy";
        }

        const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeId);
        if (!edges || edges->edgeCount == 0) {
            LOG_INFO("[SR] +- %s%s: no neighbors (%s)", prefix, nodeName, statusStr);
            continue;
        }

        // Count direct radio connections and downstream count for this relay
        uint8_t directConnectionCount = 0;
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (edges->edges[i].source == EdgeLite::Source::Reported) {
                directConnectionCount++;
            }
        }
        size_t dsCount = routingGraph->getDownstreamCountForRelay(nodeId);

        if (dsCount > 0) {
            LOG_INFO("[SR] +- %s%s (%u edges, relay for %u downstream)", prefix, nodeName,
                     edges->edgeCount, static_cast<unsigned int>(dsCount));
        } else {
            LOG_INFO("[SR] +- %s%s (%u edges)", prefix, nodeName, edges->edgeCount);
        }

        // Display direct connections (Reported source edges)
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            const EdgeLite& edge = edges->edges[i];
            if (edge.source != EdgeLite::Source::Reported) {
                continue;
            }

            char neighborName[48];
            getNodeDisplayName(edge.to, neighborName, sizeof(neighborName));

            CapabilityStatus neighborStatus = getCapabilityStatus(edge.to);
            const char* nprefix = "";
            if (neighborStatus == CapabilityStatus::SRactive) {
                nprefix = "[SR-active] ";
            } else if (neighborStatus == CapabilityStatus::Passive) {
                nprefix = "[SR-passive] ";
            }

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

            LOG_INFO("[SR] |    +- %s%s: %s link (ETX=%.1f, %s sec ago)",
                    nprefix, neighborName, quality, etx, ageBuf);
        }
    }

    // Add legend explaining ETX to signal quality mapping
    LOG_INFO("[SR] ETX to signal mapping: ETX=1.0~RSSI=-60dB/SNR=10dB, ETX=2.0~RSSI=-90dB/SNR=0dB, ETX=4.0~RSSI=-110dB/SNR=-5dB");
    LOG_DEBUG("[SR] Topology logging complete");
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

    // Update node activity for packet reception and relay tracking
    // For active nodes: track all packets (needed for full topology and routing)
    // For passive nodes: only track direct packets (only need direct neighbors)
    // We'll check this after determining if packet is direct (below)
    bool shouldUpdateNodeActivity = false;

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
    // 3. Direct detection using hopStart and hopLimit:
    //    - hopStart == hopLimit: packet hasn't been relayed (direct transmission)
    //    - In SR, we keep hopLimit >= 1 even for passive nodes, so direct means no decrement from original
    //    
    // relay_node can be ambiguous when multiple nodes share the same last byte,
    // so hopStart/hopLimit is more reliable for detecting direct neighbors.
    
    bool hasSignalData = (mp.rx_rssi != 0 || mp.rx_snr != 0);
    bool notViaMqtt = !mp.via_mqtt;
    
    // Check if packet has been relayed by comparing hopStart to hopLimit
    // If they match, the sender transmitted directly (not relayed yet)
    bool isDirectFromSender = (mp.hop_start == mp.hop_limit);
    
    uint8_t fromLastByte = mp.from & 0xFF;
    
    // Debug logging to understand packet reception and relay state
    if (hasSignalData && notViaMqtt) {
        LOG_DEBUG("[SR] Packet from 0x%08x: relay=0x%02x, hopStart=%d, hopLimit=%d, direct=%d",
                  mp.from, mp.relay_node, mp.hop_start, mp.hop_limit, isDirectFromSender);
    }
    
    // Update node activity only when appropriate:
    // - Active nodes: all packets (need full topology)
    // - Passive nodes: only direct packets (only need direct neighbors)
    if (shouldUpdateNodeActivity || isActiveRoutingRole() || (hasSignalData && notViaMqtt && isDirectFromSender)) {
        updateNodeActivityForPacketAndRelay(&mp);
    }
    
    // Update SignalRouting graph for directly-heard nodes
    // Active routing nodes: track all nodes for topology-based routing
    // Passive nodes: only track directly-heard nodes (don't relay, so no point in tracking relayed nodes)
    if (routingGraph && notViaMqtt) {
        if (hasSignalData && isDirectFromSender) {
            // Direct reception - always add to graph
            updateNeighborInfo(mp.from, mp.rx_rssi, mp.rx_snr, mp.rx_time);
        } else if (isActiveRoutingRole() && !isDirectFromSender && mp.relay_node != 0) {
            // Relayed packet from active routing node - update activity for topology
            routingGraph->updateNodeActivity(mp.from, millis() / 1000);
        }
        // Passive nodes skip relayed packets entirely
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
            NeighborGraph::calculateETX(mp.rx_rssi, mp.rx_snr);

        // NOTE: We used to clear downstream relationships when a node becomes directly reachable,
        // but this is wrong. A node can be both a direct neighbor AND a gateway for other nodes.
        // Only clear downstream relationships through aging or when relationships become invalid.

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
            // But only if:
            // 1. We have a direct connection to inferredRelayer (can hear them directly)
            // 2. We don't have a direct connection to mp.from ourselves
            bool hasDirectConnectionToRelay = false;
            bool hasDirectConnectionToSender = false;
            const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (edges) {
                for (uint8_t i = 0; i < edges->edgeCount; i++) {
                    if (edges->edges[i].to == inferredRelayer && 
                        edges->edges[i].source == EdgeLite::Source::Reported) {
                        hasDirectConnectionToRelay = true;
                    }
                    if (edges->edges[i].to == mp.from && 
                        edges->edges[i].source == EdgeLite::Source::Reported) {
                        hasDirectConnectionToSender = true;
                    }
                }
            }
            // Only record downstream relationship if we hear the relay directly and don't hear sender directly
            if (hasDirectConnectionToRelay && !hasDirectConnectionToSender) {
                float inferredEtx = NeighborGraph::calculateETX(-70, 5.0f); // Default for inferred
                routingGraph->updateDownstream(mp.from, inferredRelayer, inferredEtx, millis() / 1000);
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

                routingGraph->updateEdge(mp.from, inferredRelayer, NeighborGraph::calculateETX(defaultRssi, defaultSnr),
                                         monotonicTimestamp, 0, EdgeLite::Source::Mirrored);
                routingGraph->updateEdge(inferredRelayer, mp.from, NeighborGraph::calculateETX(defaultRssi, defaultSnr),
                                         monotonicTimestamp, 0, EdgeLite::Source::Mirrored);
            } else {
                LOG_DEBUG("[SR] Skipping direct connectivity inference for SR-aware node %08x (capability: %d)",
                         inferredRelayer, (int)getCapabilityStatus(inferredRelayer));
            }

            // Update relay node's edge in the graph since it's actively relaying
            if (hasSignalData) {
                updateNeighborInfo(inferredRelayer, mp.rx_rssi, mp.rx_snr, mp.rx_time);
            } else {
                // No direct signal data available - preserve existing edge or create with defaults
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
                            NeighborGraph::etxToSignal(existingEtx, approxRssi, existingSnr);
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

    // Check if heardFrom is ALSO a relay for the destination.
    // If we received the packet from ANY relay that leads to the destination,
    // that relay should deliver it - we don't need to relay.
    if (heardFrom != 0 && heardFrom != sourceNode) {
        NodeNum relayForDest = routingGraph->getDownstreamRelay(destination);
        if (relayForDest == heardFrom) {
            LOG_DEBUG("[SR] UNICAST RELAY: Received from relay %s which leads to %s - no relay needed",
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

    // Check if destination is reachable through SR topology
    if (!topologyHealthyForUnicast(p->to)) {
        // If the node exists in NodeDB, fall back to broadcast-style relay
        // This handles legacy/stock nodes not in the SR graph
        if (nodeDB->getMeshNode(p->to)) {
            LOG_DEBUG("[SR] Unicast to %s not routable via SR, falling back to broadcast relay", destName);
            return shouldRelayBroadcast(p);
        }
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

    // Downstream awareness: check if WE are the recorded relay for the source or destination
    NodeNum relayForSource = routingGraph->getDownstreamRelay(sourceNode);
    NodeNum relayForDest = routingGraph->getDownstreamRelay(p->to);
    bool weAreRelayForSource = (relayForSource != 0 && relayForSource == myNode);
    bool weAreRelayForDest = (relayForDest != 0 && relayForDest == myNode);
    size_t downstreamCount = (weAreRelayForSource || weAreRelayForDest) ? routingGraph->getDownstreamCountForRelay(myNode) : 0;

    uint32_t relayDecisionTime = packetReceivedTimestamp; // Use the packet received timestamp computed above

    // Check for stock gateway nodes that can be heard directly
    // If we have stock nodes that could serve as gateways, be conservative with SR relaying
    bool hasStockGateways = false;
    bool heardFromStockGateway = false;
    if (routingGraph && nodeDB) {
        // In lite mode, check capability records for legacy nodes
        for (uint8_t i = 0; i < capabilityRecordCount; i++) {
            if (capabilityRecords[i].record.status == CapabilityStatus::Legacy) {
                hasStockGateways = true;
                if (capabilityRecords[i].nodeId == heardFrom) {
                    heardFromStockGateway = true;
                }
            }
        }
    }

    // Key insight: If packet comes from a stock gateway, we MUST relay it within the branch
    // to ensure all local nodes receive packets from outside the branch
    bool mustRelayForBranchCoverage = heardFromStockGateway;

    if (heardFromStockGateway) {
        LOG_DEBUG("[SR] Packet from stock gateway %08x - prioritizing branch distribution", heardFrom);
    }

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

    // Relay override: force relay if we are the recorded relay for source OR destination
    if (!shouldRelay && (weAreRelayForSource || weAreRelayForDest)) {
        NodeNum forcedFor = weAreRelayForSource ? sourceNode : p->to;
        LOG_INFO("[SR] We are relay for %08x (downstream=%u) -> force relay", forcedFor, static_cast<unsigned int>(downstreamCount));
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

    RouteLite route = routingGraph->calculateRoute(destination, currentTime,
                        [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });

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

    // Fallback 1: if we know a relay for this destination, and we have a direct link to it, forward there
    // But only if the relay can hear the transmitter (heardFrom)
    NodeNum relayForDest = routingGraph->getDownstreamRelay(destination);
    if (relayForDest != 0 && nodeDB) {
        // Verify relay can hear transmitter before using it
        bool relayCanHearTransmitter = true;
        bool connectivityUnknown = false;
        if (heardFrom != 0 && relayForDest != heardFrom) {
            relayCanHearTransmitter = hasVerifiedConnectivity(heardFrom, relayForDest, &connectivityUnknown);
        }

        // Only use relay if we can verify connectivity (be conservative with stock nodes)
        if (relayCanHearTransmitter && !connectivityUnknown) {
            const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
            if (myEdges) {
                for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                    if (myEdges->edges[i].to == relayForDest) {
                        char gwName[64];
                        getNodeDisplayName(relayForDest, gwName, sizeof(gwName));
                        LOG_DEBUG("[SR] No direct route to %s, but forwarding to relay %s", destName, gwName);
                        return relayForDest;
                    }
                }
            }
        } else {
            char gwName[64], heardFromName[64];
            getNodeDisplayName(relayForDest, gwName, sizeof(gwName));
            getNodeDisplayName(heardFrom, heardFromName, sizeof(heardFromName));
            if (connectivityUnknown) {
                LOG_DEBUG("[SR] Relay %s connectivity to %s unknown (stock node) - skipping", gwName, heardFromName);
            } else {
                LOG_DEBUG("[SR] Relay %s cannot hear transmitter %s - skipping", gwName, heardFromName);
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

        if (isDirectNeighbor) {
            LOG_DEBUG("[SR] Delivering unicast to direct neighbor %s (ETX=%.2f) since destination didn't hear transmission",
                     destName, directEtx);
            return destination; // Deliver directly to our neighbor
        }
    }

    // Fallback 4: if we are recorded as the relay for this destination, we can deliver directly
    // This handles true relay scenarios where we have unique connectivity that other SR nodes don't
    if (routingGraph->getDownstreamRelay(destination) == myNode) {
        LOG_INFO("[SR] We are the designated relay for %s - delivering directly", destName);
        // Refresh the downstream entry since we're actively using it
        routingGraph->updateDownstream(destination, myNode, 1.0f, millis() / 1000);
        return destination; // We are the relay, deliver directly
    }

    // Fallback 5: if the destination only has us as a neighbor (effective relay scenario),
    // we should try to deliver directly even without formal relay designation
    // This handles cases like FMC6 where a node only connects through us
    if (routingGraph && nodeDB) {
        const NodeEdgesLite *destEdges = routingGraph->getEdgesFrom(destination);
        if (destEdges && destEdges->edgeCount == 1 && destEdges->edges[0].to == myNode) {
            LOG_INFO("[SR] %s only connects through us (effective relay) - delivering directly", destName);
            // Record ourselves as relay for this destination since we're the only connection
            routingGraph->updateDownstream(destination, myNode, 1.0f, millis() / 1000);
            return destination; // We are the effective relay, deliver directly
        }
    }

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
        NeighborGraph::calculateETX(rssi, snr);

    // Store edge: nodeId → us (the direction of the transmission we measured)
    // This is used for routing decisions when traffic needs to reach us
    int changeType =
        routingGraph->updateEdge(nodeId, myNode, etx, monotonicTimestamp, variance, EdgeLite::Source::Reported);

    // Also store reverse edge: us → nodeId (assuming approximately symmetric link)
    // Since we directly measured the link quality (even if in the opposite direction),
    // mark this as Reported source, not Mirrored
    routingGraph->updateEdge(myNode, nodeId, etx, monotonicTimestamp, variance
                             , EdgeLite::Source::Reported
                             );

    // If significant change, consider sending an update sooner
    if (changeType != EDGE_NO_CHANGE) {
        char neighborName[64];
        getNodeDisplayName(nodeId, neighborName, sizeof(neighborName));

        if (changeType == EDGE_NEW) {
            // We now have a DIRECT connection to this node - clear any downstream entries
            // that were created based on topology broadcasts before we heard from them directly
            routingGraph->clearDownstreamForDestination(nodeId);

            // Set green for new neighbor (operation start)
            setRgbLed(0, 255, 0);
            LOG_INFO("[SR] Topology changed: new neighbor %s (total nodes: %u)", neighborName, static_cast<unsigned int>(routingGraph->getNodeCount()));
            logNetworkTopology();
        } else if (changeType == EDGE_SIGNIFICANT_CHANGE) {
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

    const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (edges) {
        totalNeighbors = edges->edgeCount;
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (getCapabilityStatus(edges->edges[i].to) == CapabilityStatus::SRactive) {
                activeNeighbors++;
            }
        }
    }

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
    if (currentStatus != CapabilityStatus::SRactive && currentStatus != CapabilityStatus::Passive) {
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
    
    // Only resolve placeholders if the routing packet itself is from a direct sender
    bool isDirectRoutingPacket = (mp.hop_start == mp.hop_limit);

    switch (routing.which_variant) {
    case meshtastic_Routing_route_request_tag:
        LOG_DEBUG("[SR] Routing request from %s with %u hops recorded", senderName,
                  routing.route_request.route_count);

        // Check for placeholder resolution in route_request hops
        // Only resolve if: 1) routing packet is from direct sender, and 2) hop node is direct neighbor of ours
        if (isDirectRoutingPacket) {
            for (size_t i = 0; i < routing.route_request.route_count; i++) {
                NodeNum hopNode = routing.route_request.route[i];
                uint8_t hopLastByte = hopNode & 0xFF;
                NodeNum placeholderId = getPlaceholderForRelay(hopLastByte);
                if (isPlaceholderNode(placeholderId) && isPlaceholderConnectedToUs(placeholderId)) {
                    // Additional check: the hop node must be a direct neighbor of ours (Reported edge)
                    bool isDirectNeighbor = false;
                    NodeNum ourNode = nodeDB->getNodeNum();
                    const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
                    if (ourEdges) {
                        for (uint8_t j = 0; j < ourEdges->edgeCount; j++) {
                            if (ourEdges->edges[j].to == hopNode && ourEdges->edges[j].source == EdgeLite::Source::Reported) {
                                isDirectNeighbor = true;
                                break;
                            }
                        }
                    }
                    if (isDirectNeighbor) {
                        LOG_INFO("[SR] Traceroute resolution: placeholder %08x -> %08x (direct neighbor in route_request)", placeholderId, hopNode);
                        resolvePlaceholder(placeholderId, hopNode);
                    } else {
                        LOG_DEBUG("[SR] Skipping traceroute resolution: %08x is not a direct neighbor", hopNode);
                    }
                }
            }
        } else {
            LOG_DEBUG("[SR] Skipping placeholder resolution for route_request: packet is relayed (not from direct sender)");
        }
        break;
    case meshtastic_Routing_route_reply_tag:
        LOG_DEBUG("[SR] Routing reply from %s for %u hops", senderName, routing.route_reply.route_back_count);

        // Check for placeholder resolution in route_reply hops
        // Only resolve if: 1) routing packet is from direct sender, and 2) hop node is direct neighbor of ours
        if (isDirectRoutingPacket) {
            for (size_t i = 0; i < routing.route_reply.route_back_count; i++) {
                NodeNum hopNode = routing.route_reply.route_back[i];
                uint8_t hopLastByte = hopNode & 0xFF;
                NodeNum placeholderId = getPlaceholderForRelay(hopLastByte);
                if (isPlaceholderNode(placeholderId) && isPlaceholderConnectedToUs(placeholderId)) {
                    // Additional check: the hop node must be a direct neighbor of ours (Reported edge)
                    bool isDirectNeighbor = false;
                    NodeNum ourNode = nodeDB->getNodeNum();
                    const NodeEdgesLite* ourEdges = routingGraph->getEdgesFrom(ourNode);
                    if (ourEdges) {
                        for (uint8_t j = 0; j < ourEdges->edgeCount; j++) {
                            if (ourEdges->edges[j].to == hopNode && ourEdges->edges[j].source == EdgeLite::Source::Reported) {
                                isDirectNeighbor = true;
                                break;
                            }
                        }
                    }
                    if (isDirectNeighbor) {
                        LOG_INFO("[SR] Traceroute resolution: placeholder %08x -> %08x (direct neighbor in route_reply)", placeholderId, hopNode);
                        resolvePlaceholder(placeholderId, hopNode);
                    } else {
                        LOG_DEBUG("[SR] Skipping traceroute resolution: %08x is not a direct neighbor", hopNode);
                    }
                }
            }
        } else {
            LOG_DEBUG("[SR] Skipping placeholder resolution for route_reply: packet is relayed (not from direct sender)");
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
    // This includes:
    // - Active routing roles (ROUTER, REPEATER, CLIENT, etc.)
    // - Passive roles (CLIENT_MUTE, TRACKER, SENSOR, TAK, etc.) to announce themselves as SR-aware
    switch (config.device.role) {
    // Active routing roles
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
    case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
    case meshtastic_Config_DeviceConfig_Role_REPEATER:
    case meshtastic_Config_DeviceConfig_Role_CLIENT:
    case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE:
    // Passive roles that participate in SR
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
    case meshtastic_Config_DeviceConfig_Role_TRACKER:
    case meshtastic_Config_DeviceConfig_Role_SENSOR:
    case meshtastic_Config_DeviceConfig_Role_TAK:
    case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:
    case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN:
        return true;
    default:
        return false;
    }
}

SignalRoutingModule::CapabilityStatus SignalRoutingModule::capabilityFromRole(
    meshtastic_Config_DeviceConfig_Role role) const
{
    // Passive roles that participate in SR (can send topology broadcasts)
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
    case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN:
    case meshtastic_Config_DeviceConfig_Role_TRACKER:
    case meshtastic_Config_DeviceConfig_Role_SENSOR:
    case meshtastic_Config_DeviceConfig_Role_TAK:
    case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:
        return CapabilityStatus::Passive;
    // Fully mute roles don't participate in SR
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

    // Lite mode: linear search in fixed array
    for (uint8_t i = 0; i < capabilityRecordCount; i++) {
        if (capabilityRecords[i].nodeId == nodeId) {
            capabilityRecords[i].record.lastUpdated = now;
            if (status == CapabilityStatus::SRactive || status == CapabilityStatus::Passive) {
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
}

void SignalRoutingModule::pruneCapabilityCache(uint32_t nowSecs)
{
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;

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
}

// Gateway pruning now handled by NeighborGraph's downstream table aging

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
        } else if (canSendTopology()) {
            return CapabilityStatus::Passive;  // Can send topology (passive roles)
        } else {
            return CapabilityStatus::Legacy;  // Can't send topology, doesn't participate in SR
        }
    }

    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;

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

    RouteLite route = routingGraph->calculateRoute(destination, millis() / 1000,
        [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });

    if (route.nextHop != 0) {
        LOG_DEBUG("[SR] Node %08x is reachable through topology (nextHop=%08x, cost=%.2f)",
                 destination, route.nextHop, route.getCost());
        return true;
    }

    // Fallback: Check if destination is reachable via a downstream relay
    NodeNum relay = routingGraph->getDownstreamRelay(destination);
    if (relay != 0) {
        // Check if we can reach the relay (relay must be routable)
        RouteLite relayRoute = routingGraph->calculateRoute(relay, millis() / 1000,
            [this](NodeNum nodeId) { return isNodeRoutable(nodeId); });
        if (relayRoute.nextHop != 0) {
            LOG_DEBUG("[SR] Node %08x is reachable via relay %08x (nextHop=%08x, cost=%.2f)",
                     destination, relay, relayRoute.nextHop, relayRoute.getCost());
            return true;
        }
    }

    return false;
}

void SignalRoutingModule::rememberRelayIdentity(NodeNum nodeId, uint8_t relayId)
{
    if (relayId == 0 || nodeId == 0) {
        return;
    }

    uint32_t nowMs = millis();

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
}

void SignalRoutingModule::pruneRelayIdentityCache(uint32_t nowMs)
{
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
}

NodeNum SignalRoutingModule::resolveRelayIdentity(uint8_t relayId) const
{
    uint32_t nowMs = millis();
    NodeNum bestNode = 0;
    uint32_t newest = 0;

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

    // Don't return placeholders - they should be resolved to real nodes
    if (isPlaceholderNode(bestNode)) {
        return 0;
    }
    return bestNode;
}


uint32_t SignalRoutingModule::getNodeLastActivityTime(NodeNum nodeId) const
{
    uint32_t now = millis() / 1000;  // Use monotonic time for TTL calculations

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
        const NodeEdgesLite *myEdges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
        if (myEdges) {
            for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                if ((myEdges->edges[i].to & 0xFF) == p->relay_node) {
                    // Remember this mapping for future use
                    const_cast<SignalRoutingModule*>(this)->rememberRelayIdentity(myEdges->edges[i].to, p->relay_node);
                    return myEdges->edges[i].to;
                }
            }
        }
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
}

void SignalRoutingModule::scheduleContentionWindowCheck(NodeNum expectedRelay, PacketId packetId, NodeNum destination, const meshtastic_MeshPacket *packet)
{
    if (!packet) return;

    // Calculate dynamic contention window based on radio factors (similar to retransmission timeout)
    uint32_t contentionWindowMs = router->getRadioInterface()->getContentionWindowMsec(packet);

    // Store packet info needed for re-evaluation and potential relay
    NodeNum sourceNode = packet->from;
    NodeNum heardFrom = resolveHeardFrom(packet, sourceNode);

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
}

void SignalRoutingModule::excludeNodeFromRelay(NodeNum nodeId, PacketId packetId)
{
    uint64_t packetKey = makeSpeculativeKey(0, packetId);

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
}

void SignalRoutingModule::clearRelayExclusionsForPacket(PacketId packetId)
{
    uint64_t packetKey = makeSpeculativeKey(0, packetId);

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
}

void SignalRoutingModule::processContentionWindows(uint32_t nowMs)
{
    if (!routingGraph || !nodeDB) return;

    NodeNum myNode = nodeDB->getNodeNum();

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
}

bool SignalRoutingModule::hasDirectConnectivity(NodeNum nodeA, NodeNum nodeB)
{
    if (!routingGraph || !nodeDB) {
        return false;
    }

    // Check if nodeA has a direct edge to nodeB
    const NodeEdgesLite* edges = routingGraph->getEdgesFrom(nodeA);
    if (edges) {
        for (uint8_t i = 0; i < edges->edgeCount; i++) {
            if (edges->edges[i].to == nodeB) {
                return true;
            }
        }
    }

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
