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
    // RP2040 RAM guard: disable signal-based routing if free heap is below 50KB
    uint32_t freeHeap = memGet.getFreeHeap();
    if (freeHeap < 50 * 1024) {
        LOG_WARN("SignalRouting: Insufficient RAM on RP2040 (%u bytes free), disabling signal-based routing", freeHeap);
        routingGraph = nullptr;
        return;
    }
#endif

    routingGraph = new Graph();
    updateSignalBasedCapable();
}

bool SignalRoutingModule::shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p) {
    if (!config.routing.signal_based_routing || !routingGraph) {
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
#ifdef DEBUG_SIGNAL_ROUTING
        LOG_DEBUG("SignalRouting: Next hop for %08x is %08x (cost: %.2f)", destination, route.nextHop, route.cost);
#endif
        return route.nextHop;
    }

    return 0; // No route found
}

void SignalRoutingModule::updateNeighborInfo(NodeNum nodeId, int32_t rssi, int32_t snr, uint32_t lastRxTime) {
    float etx = Graph::calculateETX(rssi, snr);
    routingGraph->updateEdge(nodeDB->getNodeNum(), nodeId, etx, lastRxTime);

#ifdef DEBUG_SIGNAL_ROUTING
    LOG_DEBUG("SignalRouting: Updated edge to %08x: RSSI=%d, SNR=%d, ETX=%.2f", nodeId, rssi, snr, etx);
#endif
}

void SignalRoutingModule::handleSpeculativeRetransmit(const meshtastic_MeshPacket *p) {
    if (!shouldUseSignalBasedRouting(p)) {
        return;
    }

    // For unicast packets, implement 400ms listen + one speculative retransmit
    if (!isBroadcast(p->to)) {
        // Schedule a retransmit after 400ms if no ACK received
        // This would typically be handled by the Router/ReliableRouter
        // For now, just log the intent
#ifdef DEBUG_SIGNAL_ROUTING
        LOG_DEBUG("SignalRouting: Scheduling speculative retransmit for packet %08x to %08x", p->id, p->to);
#endif
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

#ifdef DEBUG_SIGNAL_ROUTING
            LOG_DEBUG("SignalRouting: Aged edges and updated graph");
#endif
        }
    }

    return ProcessMessage::CONTINUE;
}

void SignalRoutingModule::onNodeInfoChanged() {
    // Update our neighbors list when node info changes
    updateSignalBasedCapable();
}

bool SignalRoutingModule::isSignalBasedCapable(NodeNum nodeId) {
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    return node && node->signal_based_capable;
}

float SignalRoutingModule::getSignalBasedCapablePercentage() {
    int totalNodes = 0;
    int capableNodes = 0;

    for (int i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (node) {
            totalNodes++;
            if (node->signal_based_capable) {
                capableNodes++;
            }
        }
    }

    if (totalNodes == 0) return 0.0f;
    return (static_cast<float>(capableNodes) / totalNodes) * 100.0f;
}

void SignalRoutingModule::updateSignalBasedCapable() {
    // Update our own node info to indicate signal-based capability
    meshtastic_NodeInfo *ourNodeInfo = nodeDB->getMutableNodeInfo();
    if (ourNodeInfo) {
        ourNodeInfo->signal_based_capable = true;

        // Update neighbors list (max 6 as per requirements)
        ourNodeInfo->neighbors_count = std::min(static_cast<size_t>(6), neighbors.size());
        for (size_t i = 0; i < ourNodeInfo->neighbors_count; i++) {
            ourNodeInfo->neighbors[i] = neighbors[i];
        }

#ifdef DEBUG_SIGNAL_ROUTING
        LOG_DEBUG("SignalRouting: Updated NodeInfo - signal_based_capable=true, neighbors=%d",
                  ourNodeInfo->neighbors_count);
#endif
    }
}
