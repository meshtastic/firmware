#include "SignalRoutingModule.h"
#include "graph/Graph.h"
#include "NodeDB.h"
#include "Router.h"
#include "RTC.h"
#include "configuration.h"
#include "memGet.h"

SignalRoutingModule *signalRoutingModule;

SignalRoutingModule::SignalRoutingModule() : MeshModule("SignalRouting")
{
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
    float etx = Graph::calculateETX(rssi, snr);
    routingGraph->updateEdge(nodeDB->getNodeNum(), nodeId, etx, lastRxTime);

    LOG_DEBUG("SignalRouting: Updated edge to %08x: RSSI=%d, SNR=%d, ETX=%.2f", nodeId, rssi, snr, etx);
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
    // TODO: Set ourNodeInfo->signal_based_capable = true after protobuf regeneration
    // For now, this is a no-op since the field doesn't exist yet
    LOG_DEBUG("SignalRouting: Module initialized (signal_based_capable will be set after protobuf update)");
}
