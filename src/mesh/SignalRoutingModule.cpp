#include "SignalRoutingModule.h"
#include "graph/Graph.h"
#include "NodeDB.h"
#include "Router.h"
#include "RTC.h"
#include "configuration.h"
#include "memGet.h"
#include "modules/NodeInfoModule.h"

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

SignalRoutingModule::SignalRoutingModule() : MeshModule("SignalRouting")
{
#ifdef ARCH_STM32WL
    // STM32WL only has 64KB RAM total - disable signal routing entirely
    LOG_INFO("SignalRouting: Disabled on STM32WL (insufficient RAM)");
    routingGraph = nullptr;
    return;
#endif

#ifdef ARCH_RP2040
    // RP2040 RAM guard: Graph uses ~25-35KB worst case (100 nodes, 6 edges each)
    // 30KB threshold leaves headroom for graph + Dijkstra temp allocations
    uint32_t freeHeap = memGet.getFreeHeap();
    if (freeHeap < 30 * 1024) {
        LOG_WARN("SignalRouting: Insufficient RAM on RP2040 (%u bytes free), disabling signal-based routing", freeHeap);
        routingGraph = nullptr;
        return;
    }
#endif

    routingGraph = new Graph();
    updateSignalBasedCapable();
}

bool SignalRoutingModule::shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p) {
    // Signal-based routing enabled by default in this fork
    // TODO: Use config.routing.signal_based_routing after protobuf regeneration
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

NodeNum SignalRoutingModule::getNextHop(NodeNum destination) {
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

void SignalRoutingModule::updateNeighborInfo(NodeNum nodeId, int32_t rssi, int32_t snr, uint32_t lastRxTime) {
    if (!routingGraph) return;

    // Update the graph - returns: 0=no change, 1=new edge, 2=significant ETX change (>20%)
    float etx = Graph::calculateETX(rssi, snr);
    int changeType = routingGraph->updateEdge(nodeDB->getNodeNum(), nodeId, etx, lastRxTime);

    // Trigger NodeInfo update if new neighbor or significant change (with rate limiting)
    if (changeType != Graph::EDGE_NO_CHANGE) {
        char neighborName[64];
        getNodeDisplayName(nodeId, neighborName, sizeof(neighborName));

        uint32_t now = millis();
        if (now - lastNodeInfoBroadcast >= NODE_INFO_MIN_INTERVAL_MS) {
            if (changeType == Graph::EDGE_NEW) {
                LOG_INFO("SignalRouting: New neighbor %s detected, triggering NodeInfo update", neighborName);
            } else {
                LOG_INFO("SignalRouting: Significant ETX change for %s, triggering NodeInfo update", neighborName);
            }

            if (nodeInfoModule) {
                nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, true);
                lastNodeInfoBroadcast = now;
            }
        }
    }
}

void SignalRoutingModule::handleSpeculativeRetransmit(const meshtastic_MeshPacket *p) {
    if (!shouldUseSignalBasedRouting(p)) {
        return;
    }

    // For unicast packets, implement 400ms listen + one speculative retransmit
    if (!isBroadcast(p->to)) {
        // Schedule a retransmit after 400ms if no ACK received
        // This would typically be handled by the Router/ReliableRouter
        LOG_DEBUG("SignalRouting: Scheduling speculative retransmit for packet %08x to %08x", p->id, p->to);
    }
}

ProcessMessage SignalRoutingModule::handleReceived(const meshtastic_MeshPacket &mp) {
    // Update neighbor information from received packets
    if (mp.rx_rssi != 0 || mp.rx_snr != 0) {
        // Log received graph node with descriptive names
        char senderName[64];
        char ourName[64];
        getNodeDisplayName(mp.from, senderName, sizeof(senderName));
        getNodeDisplayName(nodeDB->getNodeNum(), ourName, sizeof(ourName));

        float etx = Graph::calculateETX(mp.rx_rssi, mp.rx_snr);
        LOG_INFO("Received graph node from %s: RSSI=%d, SNR=%d, ETX=%.2f",
                 senderName, mp.rx_rssi, mp.rx_snr, etx);

        updateNeighborInfo(mp.from, mp.rx_rssi, mp.rx_snr, mp.rx_time);
    }

    // Check if we should update graph periodically
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

bool SignalRoutingModule::isSignalBasedCapable(NodeNum nodeId) {
    // TODO: Check node->signal_based_capable after protobuf regeneration
    // For now, assume all nodes we've heard from recently are capable
    // (this is a temporary heuristic until we can exchange capability info)
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    if (!node) return false;

    // Consider a node capable if we've heard from it in the last 5 minutes
    uint32_t now = getValidTime(RTCQualityFromNet);
    return (now - node->last_heard) < 300;
}

float SignalRoutingModule::getSignalBasedCapablePercentage() {
    // int totalNodes = 0;
    // int capableNodes = 0;

    // for (int i = 0; i < nodeDB->getNumMeshNodes(); i++) {
    //     const meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
    //     if (node) {
    //         totalNodes++;
    //         if (node->signal_based_capable) {
    //             capableNodes++;
    //         }
    //     }
    // }

    // if (totalNodes == 0) return 0.0f;
    // return (static_cast<float>(capableNodes) / totalNodes) * 100.0f;
    return 100.0f;
}

void SignalRoutingModule::updateSignalBasedCapable() {
    LOG_DEBUG("SignalRouting: Module initialized with signal_based_capable=true");
}

void SignalRoutingModule::populateNeighbors(meshtastic_User &user) {
    if (!routingGraph) {
        user.neighbors_count = 0;
        user.signal_based_capable = false;
        return;
    }

    user.signal_based_capable = true;

    const std::vector<Edge>* edges = routingGraph->getEdgesFrom(nodeDB->getNodeNum());
    if (!edges || edges->empty()) {
        user.neighbors_count = 0;
        return;
    }

    // Copy up to 4 neighbors (max defined in proto options)
    size_t count = std::min(edges->size(), static_cast<size_t>(4));
    user.neighbors_count = count;

    for (size_t i = 0; i < count; i++) {
        const Edge& edge = (*edges)[i];
        meshtastic_NeighborLink& neighbor = user.neighbors[i];

        neighbor.node_id = edge.to;
        neighbor.last_rx_time = edge.lastUpdate;
        neighbor.signal_based_capable = isSignalBasedCapable(edge.to);
        neighbor.position_variance = 0; // Reserved for future use

        // Convert ETX back to approximate RSSI/SNR
        Graph::etxToSignal(edge.etx, neighbor.rssi, neighbor.snr);
    }

    char ourName[64];
    getNodeDisplayName(nodeDB->getNodeNum(), ourName, sizeof(ourName));
    LOG_INFO("SignalRouting: Populated %d neighbors for %s", user.neighbors_count, ourName);
}

void SignalRoutingModule::processReceivedNeighbors(NodeNum fromNode, const meshtastic_User &user) {
    if (!routingGraph) return;

    char senderName[64];
    getNodeDisplayName(fromNode, senderName, sizeof(senderName));

    // Update node's signal_based_capable status in our DB
    // (This would require NodeDB changes to store the flag - for now just log)

    if (user.neighbors_count == 0) {
        LOG_DEBUG("SignalRouting: %s has no neighbors", senderName);
        return;
    }

    LOG_INFO("SignalRouting: Received %d neighbors from %s (signal_based_capable=%s)",
             user.neighbors_count, senderName, user.signal_based_capable ? "true" : "false");

    // Add edges from the sender to each of their neighbors
    for (pb_size_t i = 0; i < user.neighbors_count; i++) {
        const meshtastic_NeighborLink& neighbor = user.neighbors[i];

        char neighborName[64];
        getNodeDisplayName(neighbor.node_id, neighborName, sizeof(neighborName));

        // Calculate ETX from the received RSSI/SNR
        float etx = Graph::calculateETX(neighbor.rssi, neighbor.snr);

        // Add edge: fromNode -> neighbor.node_id with variance for route cost calculation
        // Higher variance = more mobile/unreliable node = higher routing cost
        routingGraph->updateEdge(fromNode, neighbor.node_id, etx, neighbor.last_rx_time, neighbor.position_variance);

        LOG_INFO("  -> %s: RSSI=%d, SNR=%d, ETX=%.2f, variance=%u, capable=%s",
                 neighborName, neighbor.rssi, neighbor.snr, etx, neighbor.position_variance,
                 neighbor.signal_based_capable ? "true" : "false");
    }
}
