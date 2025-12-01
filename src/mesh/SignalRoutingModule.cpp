#include "SignalRoutingModule.h"
#include "graph/Graph.h"
#include "NodeDB.h"
#include "Router.h"
#include "RTC.h"
#include "configuration.h"
#include "memGet.h"
#include "MeshService.h"

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

    // We want to see all packets for signal quality updates
    isPromiscuous = true;

    // Set initial broadcast delay (30 seconds after startup)
    setIntervalFromNow(30 * 1000);

    LOG_INFO("SignalRouting: Module initialized (version %d)", SIGNAL_ROUTING_VERSION);
}

int32_t SignalRoutingModule::runOnce()
{
    if (!routingGraph || !signalBasedRoutingEnabled) {
        return disable();
    }

    // Send our signal routing info periodically
    sendSignalRoutingInfo();

    return SIGNAL_ROUTING_BROADCAST_SECS * 1000;
}

void SignalRoutingModule::sendSignalRoutingInfo(NodeNum dest)
{
    meshtastic_SignalRoutingInfo info = meshtastic_SignalRoutingInfo_init_zero;
    buildSignalRoutingInfo(info);

    // Only send if we have neighbors to report
    if (info.neighbors_count > 0) {
        meshtastic_MeshPacket *p = allocDataProtobuf(info);
        p->to = dest;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

        char ourName[64];
        getNodeDisplayName(nodeDB->getNodeNum(), ourName, sizeof(ourName));
        LOG_INFO("SignalRouting: Broadcasting %d neighbors from %s", info.neighbors_count, ourName);

        service->sendToMesh(p);
        lastBroadcast = millis();
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

bool SignalRoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_SignalRoutingInfo *p)
{
    if (!routingGraph || !p) return false;

    char senderName[64];
    getNodeDisplayName(mp.from, senderName, sizeof(senderName));

    if (p->neighbors_count == 0) {
        LOG_DEBUG("SignalRouting: %s has no neighbors (version %d)", senderName, p->routing_version);
        return false;
    }

    LOG_INFO("SignalRouting: Received %d neighbors from %s (version %d, capable=%s)",
             p->neighbors_count, senderName, p->routing_version,
             p->signal_based_capable ? "true" : "false");

    // Add edges from the sender to each of their neighbors
    for (pb_size_t i = 0; i < p->neighbors_count; i++) {
        const meshtastic_NeighborLink& neighbor = p->neighbors[i];

        char neighborName[64];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));

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
    // Update neighbor information from any received packet's signal quality
    if (mp.rx_rssi != 0 || mp.rx_snr != 0) {
        char senderName[64];
        getNodeDisplayName(mp.from, senderName, sizeof(senderName));

        float etx = Graph::calculateETX(mp.rx_rssi, mp.rx_snr);
        LOG_DEBUG("SignalRouting: Heard %s: RSSI=%d, SNR=%d, ETX=%.2f",
                 senderName, mp.rx_rssi, mp.rx_snr, etx);

        updateNeighborInfo(mp.from, mp.rx_rssi, mp.rx_snr, mp.rx_time);
    }

    // Periodic graph maintenance
    if (routingGraph) {
        uint32_t currentTime = getValidTime(RTCQualityFromNet);
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
    if (!signalBasedRoutingEnabled || !routingGraph) {
        return false;
    }

    // Check if destination is signal-based capable (unicast)
    if (!isBroadcast(p->to) && isSignalBasedCapable(p->to)) {
        return true;
    }

    // Check if ≥60% of heard nodes are signal-based capable (broadcast)
    if (isBroadcast(p->to)) {
        float percentage = getSignalBasedCapablePercentage();
        return percentage >= 60.0f;
    }

    return false;
}

NodeNum SignalRoutingModule::getNextHop(NodeNum destination)
{
    if (!routingGraph) {
        return 0;
    }

    uint32_t currentTime = getValidTime(RTCQualityFromNet);
    Route route = routingGraph->calculateRoute(destination, currentTime);

    if (route.nextHop != 0) {
        LOG_DEBUG("SignalRouting: Next hop for %08x is %08x (cost: %.2f)", destination, route.nextHop, route.cost);
        return route.nextHop;
    }

    return 0; // No route found
}

void SignalRoutingModule::updateNeighborInfo(NodeNum nodeId, int32_t rssi, int32_t snr, uint32_t lastRxTime, uint32_t variance)
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
        } else {
            LOG_INFO("SignalRouting: Significant ETX change for %s", neighborName);
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
    if (!shouldUseSignalBasedRouting(p)) {
        return;
    }

    // For unicast packets, implement 400ms listen + one speculative retransmit
    if (!isBroadcast(p->to)) {
        LOG_DEBUG("SignalRouting: Scheduling speculative retransmit for packet %08x to %08x", p->id, p->to);
    }
}

bool SignalRoutingModule::isSignalBasedCapable(NodeNum nodeId)
{
    // For now, assume all nodes we've heard from recently are capable
    // In the future, we'll track this from received SignalRoutingInfo
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    if (!node) return false;

    uint32_t now = getValidTime(RTCQualityFromNet);
    return (now - node->last_heard) < 300;
}

float SignalRoutingModule::getSignalBasedCapablePercentage()
{
    // TODO: Track actual capability from received SignalRoutingInfo packets
    // For now, return 100% to enable signal-based routing for broadcasts
    return 100.0f;
}
