#include "GraphLite.h"
#include "configuration.h"
#include "Graph.h"
#include <algorithm>
#include <cmath>

GraphLite::GraphLite() : nodeCount(0), relayStateCount(0), routeCacheTime(0) {}

NodeEdgesLite *GraphLite::findNode(NodeNum nodeId)
{
    for (uint8_t i = 0; i < nodeCount; i++) {
        if (nodes[i].nodeId == nodeId) {
            return &nodes[i];
        }
    }
    return nullptr;
}

const NodeEdgesLite *GraphLite::findNode(NodeNum nodeId) const
{
    for (uint8_t i = 0; i < nodeCount; i++) {
        if (nodes[i].nodeId == nodeId) {
            return &nodes[i];
        }
    }
    return nullptr;
}

NodeEdgesLite *GraphLite::findOrCreateNode(NodeNum nodeId)
{
    // Try to find existing
    NodeEdgesLite *node = findNode(nodeId);
    if (node) {
        return node;
    }

    // Create new if space available
    if (nodeCount < GRAPH_LITE_MAX_NODES) {
        node = &nodes[nodeCount++];
        node->nodeId = nodeId;
        node->edgeCount = 0;
        node->lastFullUpdate = 0;
        return node;
    }

    // Graph full - find the node with fewest edges to evict
    uint8_t minEdges = 255;
    uint8_t evictIdx = 0;
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;

    for (uint8_t i = 0; i < nodeCount; i++) {
        // Never evict our own node
        if (nodes[i].nodeId == myNode) {
            continue;
        }
        if (nodes[i].edgeCount < minEdges) {
            minEdges = nodes[i].edgeCount;
            evictIdx = i;
        }
    }

    // Evict and reuse
    node = &nodes[evictIdx];
    node->nodeId = nodeId;
    node->edgeCount = 0;
    node->lastFullUpdate = 0;
    return node;
}

EdgeLite *GraphLite::findEdge(NodeEdgesLite *node, NodeNum to)
{
    if (!node)
        return nullptr;
    for (uint8_t i = 0; i < node->edgeCount; i++) {
        if (node->edges[i].to == to) {
            return &node->edges[i];
        }
    }
    return nullptr;
}

const EdgeLite *GraphLite::findEdge(const NodeEdgesLite *node, NodeNum to) const
{
    if (!node)
        return nullptr;
    for (uint8_t i = 0; i < node->edgeCount; i++) {
        if (node->edges[i].to == to) {
            return &node->edges[i];
        }
    }
    return nullptr;
}

int GraphLite::updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance,
                          EdgeLite::Source source)
{
    NodeEdgesLite *node = findOrCreateNode(from);
    if (!node) {
        return Graph::EDGE_NO_CHANGE;
    }

    node->lastFullUpdate = timestamp;

    EdgeLite *edge = findEdge(node, to);
    if (edge) {
        // If we already have a reported edge, don't overwrite with a mirrored guess
        if (edge->source == EdgeLite::Source::Reported && source == EdgeLite::Source::Mirrored) {
            return Graph::EDGE_NO_CHANGE;
        }

        // Update existing edge
        float oldEtx = edge->getEtx();
        float change = std::abs(etx - oldEtx) / oldEtx;

        edge->setEtx(etx);
        edge->lastUpdateLo = static_cast<uint16_t>(timestamp & 0xFFFF);
        edge->variance = (variance / 12 > 255) ? 255 : static_cast<uint8_t>(variance / 12); // Scale variance
        edge->source = source;

        return (change > Graph::ETX_CHANGE_THRESHOLD) ? Graph::EDGE_SIGNIFICANT_CHANGE : Graph::EDGE_NO_CHANGE;
    }

    // Add new edge
    if (node->edgeCount < GRAPH_LITE_MAX_EDGES_PER_NODE) {
        edge = &node->edges[node->edgeCount++];
        edge->to = to;
        edge->setEtx(etx);
        edge->lastUpdateLo = static_cast<uint16_t>(timestamp & 0xFFFF);
        edge->variance = (variance / 12 > 255) ? 255 : static_cast<uint8_t>(variance / 12);
        edge->source = source;
        return Graph::EDGE_NEW;
    }

    // Edge list full - replace worst edge if new one is better
    uint8_t worstIdx = 0;
    uint16_t worstEtx = 0;
    for (uint8_t i = 0; i < node->edgeCount; i++) {
        if (node->edges[i].etxFixed > worstEtx) {
            worstEtx = node->edges[i].etxFixed;
            worstIdx = i;
        }
    }

    uint16_t newEtxFixed = static_cast<uint16_t>(etx * 100.0f);
    if (newEtxFixed < worstEtx) {
        edge = &node->edges[worstIdx];
        edge->to = to;
        edge->setEtx(etx);
        edge->lastUpdateLo = static_cast<uint16_t>(timestamp & 0xFFFF);
        edge->variance = (variance / 12 > 255) ? 255 : static_cast<uint8_t>(variance / 12);
        edge->source = source;
        return Graph::EDGE_SIGNIFICANT_CHANGE;
    }

    return Graph::EDGE_NO_CHANGE;
}

void GraphLite::updateNodeActivity(NodeNum nodeId, uint32_t timestamp)
{
    NodeEdgesLite *node = findOrCreateNode(nodeId);
    if (node) {
        node->lastFullUpdate = timestamp;
    }
}

void GraphLite::ageEdges(uint32_t currentTimeSecs)
{
    for (uint8_t n = 0; n < nodeCount;) {
        NodeEdgesLite *node = &nodes[n];

        // Check if entire node is stale (no recent updates)
        if (currentTimeSecs - node->lastFullUpdate > EDGE_AGING_TIMEOUT_SECS) {
            // Remove this node by swapping with last
            if (n < nodeCount - 1) {
                nodes[n] = nodes[nodeCount - 1];
            }
            nodeCount--;
            continue; // Don't increment n, check swapped node
        }
        n++;
    }

    // Also age relay states
    uint16_t currentLo = static_cast<uint16_t>(currentTimeSecs & 0xFFFF);
    for (uint8_t i = 0; i < relayStateCount;) {
        uint16_t age = currentLo - relayStates[i].timestampLo;
        if (age > 2) { // 2 seconds timeout
            // Remove by swapping
            if (i < relayStateCount - 1) {
                relayStates[i] = relayStates[relayStateCount - 1];
            }
            relayStateCount--;
            continue;
        }
        i++;
    }
}

float GraphLite::calculateETX(int32_t rssi, float snr)
{
    // Simplified ETX calculation
    float deliveryProb = 1.0f;
    if (rssi < -100) {
        deliveryProb = 0.1f;
    } else if (rssi < -80) {
        deliveryProb = 0.5f;
    } else if (rssi < -60) {
        deliveryProb = 0.8f;
    } else {
        deliveryProb = 0.95f;
    }

    if (snr < 5.0f) {
        deliveryProb *= 0.5f;
    } else if (snr < 10.0f) {
        deliveryProb *= 0.8f;
    }

    return (deliveryProb > 0.0f) ? (1.0f / deliveryProb) : 100.0f;
}

void GraphLite::etxToSignal(float etx, int32_t &rssi, int32_t &snr)
{
    if (etx <= 1.0f) {
        rssi = -60;
        snr = 10;
    } else if (etx <= 2.0f) {
        float t = (etx - 1.0f);
        rssi = -60 - static_cast<int32_t>(t * 30);
        snr = 10 - static_cast<int32_t>(t * 10);
    } else {
        float t = std::min((etx - 2.0f) / 2.0f, 1.0f);
        rssi = -90 - static_cast<int32_t>(t * 20);
        snr = 0 - static_cast<int32_t>(t * 5);
    }
}

const NodeEdgesLite *GraphLite::getEdgesFrom(NodeNum node) const
{
    return findNode(node);
}

uint8_t GraphLite::getNeighborCount(NodeNum node) const
{
    const NodeEdgesLite *n = findNode(node);
    return n ? n->edgeCount : 0;
}

RouteLite GraphLite::calculateRoute(NodeNum destination, uint32_t currentTime)
{
    // Check cache first
    if (routeCache.destination == destination && (currentTime - routeCacheTime) < ROUTE_CACHE_TIMEOUT_SECS) {
        return routeCache;
    }

    RouteLite result;
    result.destination = destination;
    result.nextHop = 0;
    result.costFixed = 0xFFFF; // Infinity
    result.timestamp = currentTime;

    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    if (myNode == 0) {
        return result;
    }

    // Simplified single-hop check: are we directly connected?
    const NodeEdgesLite *myEdges = findNode(myNode);
    if (myEdges) {
        for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
            if (myEdges->edges[i].to == destination) {
                result.nextHop = destination;
                result.costFixed = myEdges->edges[i].etxFixed;
                routeCache = result;
                routeCacheTime = currentTime;
                return result;
            }
        }

        // Two-hop search: check neighbors' neighbors
        for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
            NodeNum neighbor = myEdges->edges[i].to;
            uint16_t costToNeighbor = myEdges->edges[i].etxFixed;

            const NodeEdgesLite *neighborEdges = findNode(neighbor);
            if (neighborEdges) {
                for (uint8_t j = 0; j < neighborEdges->edgeCount; j++) {
                    if (neighborEdges->edges[j].to == destination) {
                        uint16_t totalCost = costToNeighbor + neighborEdges->edges[j].etxFixed;
                        if (totalCost < result.costFixed) {
                            result.nextHop = neighbor;
                            result.costFixed = totalCost;
                        }
                    }
                }
            }
        }
    }

    if (result.nextHop != 0) {
        routeCache = result;
        routeCacheTime = currentTime;
    }

    return result;
}

bool GraphLite::shouldRelaySimple(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const
{
    // Simplified version of the algorithm for constrained devices
    // Find best candidate and relay logic similar to full Graph

    const NodeEdgesLite *myEdges = findNode(myNode);
    const NodeEdgesLite *transmittingEdges = findNode(heardFrom);

    if (!myEdges || myEdges->edgeCount == 0) {
        return false; // We have no neighbors, no point relaying
    }

    if (!transmittingEdges) {
        return false; // No transmitting node edges found
    }

    // Build set of already covered nodes
    std::unordered_set<NodeNum> alreadyCovered;
    alreadyCovered.insert(sourceNode);
    alreadyCovered.insert(heardFrom);

    for (uint8_t i = 0; i < transmittingEdges->edgeCount; i++) {
        alreadyCovered.insert(transmittingEdges->edges[i].to);
    }

    // Find best candidate (simplified - just check if we have unique coverage)
    uint8_t uniqueNeighbors = 0;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        NodeNum neighbor = myEdges->edges[i].to;
        if (alreadyCovered.find(neighbor) == alreadyCovered.end()) {
            uniqueNeighbors++;
        }
    }

    // For lite mode, simplified logic: relay if we have unique neighbors
    // In full implementation this would check if we're the best candidate, wait for others, etc.
    return uniqueNeighbors > 0;
}

bool GraphLite::shouldRelaySimpleConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const
{
    // Conservative relay decision for mixed networks with stock gateways:
    // Only consider nodes that directly heard the transmitting node (heardFrom)
    // Be more selective about relaying to avoid competing with stock gateways

    const NodeEdgesLite *myEdges = findNode(myNode);
    const NodeEdgesLite *transmittingEdges = findNode(heardFrom);  // Only consider transmitting node's neighbors

    if (!myEdges || myEdges->edgeCount == 0) {
        return false; // We have no neighbors, no point relaying
    }

    if (!transmittingEdges) {
        return false; // No transmitting node edges found
    }

    // Count SR neighbors we have that the transmitting node doesn't have direct connection to
    uint8_t uniqueSrNeighbors = 0;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        NodeNum neighbor = myEdges->edges[i].to;
        if (neighbor == sourceNode || neighbor == heardFrom) {
            continue; // They already have the packet
        }

        // Check if the transmitting node has direct connection to this neighbor
        bool transmittingHasIt = false;
        for (uint8_t j = 0; j < transmittingEdges->edgeCount; j++) {
            if (transmittingEdges->edges[j].to == neighbor) {
                transmittingHasIt = true;
                break;
            }
        }

        if (!transmittingHasIt) {
            uniqueSrNeighbors++;
        }
    }

    // Conservative logic: Require at least 2 unique SR neighbors before relaying
    // This reduces redundant relaying while still ensuring branch connectivity
    return uniqueSrNeighbors >= 2;
}

uint32_t GraphLite::getContentionWindowMs()
{
    // Use Graph's shared implementation
    return Graph::getContentionWindowMs();
}

bool GraphLite::shouldRelayWithContention(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t packetId, uint32_t currentTime) const
{
    // Simplified contention logic for constrained environments:
    // Check if we have unique coverage and no other nodes have transmitted

    const NodeEdgesLite *myEdges = findNode(myNode);
    const NodeEdgesLite *sourceEdges = findNode(sourceNode);
    const NodeEdgesLite *relayEdges = (heardFrom == sourceNode) ? nullptr : findNode(heardFrom);

    if (!myEdges || myEdges->edgeCount == 0) {
        return false; // We have no neighbors, no point relaying
    }

    // Count unique neighbors we can reach that source/relay cannot
    uint8_t uniqueNeighbors = 0;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        NodeNum neighbor = myEdges->edges[i].to;
        if (neighbor == sourceNode || neighbor == heardFrom) {
            continue; // They already have the packet
        }

        // Check if source or relayer has direct connection to this neighbor
        bool sourceHasIt = false;
        if (sourceEdges) {
            for (uint8_t j = 0; j < sourceEdges->edgeCount; j++) {
                if (sourceEdges->edges[j].to == neighbor) {
                    sourceHasIt = true;
                    break;
                }
            }
        }

        if (relayEdges && !sourceHasIt) {
            for (uint8_t j = 0; j < relayEdges->edgeCount; j++) {
                if (relayEdges->edges[j].to == neighbor) {
                    sourceHasIt = true;
                    break;
                }
            }
        }

        if (!sourceHasIt) {
            uniqueNeighbors++;
        }
    }

    // Must have unique coverage to relay
    if (uniqueNeighbors == 0) {
        return false;
    }

    // Check if any other nodes have already transmitted this packet
    for (uint8_t i = 0; i < nodeCount && i < GRAPH_LITE_MAX_NODES; i++) {
        NodeNum otherNode = nodes[i].nodeId;
        if (otherNode != myNode && otherNode != sourceNode && otherNode != heardFrom) {
            if (hasNodeTransmitted(otherNode, packetId, currentTime)) {
                // Another node already transmitted, don't duplicate
                return false;
            }
        }
    }

    // We have unique coverage and no one else has transmitted - relay!
    return true;
}

void GraphLite::recordNodeTransmission(NodeNum nodeId, uint32_t packetId, uint32_t currentTime)
{
    // Find existing entry
    for (uint8_t i = 0; i < relayStateCount; i++) {
        if (relayStates[i].nodeId == nodeId) {
            relayStates[i].packetId = packetId;
            relayStates[i].timestampLo = static_cast<uint16_t>(currentTime & 0xFFFF);
            return;
        }
    }

    // Add new entry
    if (relayStateCount < GRAPH_LITE_MAX_RELAY_STATES) {
        relayStates[relayStateCount].nodeId = nodeId;
        relayStates[relayStateCount].packetId = packetId;
        relayStates[relayStateCount].timestampLo = static_cast<uint16_t>(currentTime & 0xFFFF);
        relayStateCount++;
    } else {
        // Replace oldest entry
        uint8_t oldestIdx = 0;
        uint16_t oldestTimestamp = relayStates[0].timestampLo;
        uint16_t currentLo = static_cast<uint16_t>(currentTime & 0xFFFF);

        for (uint8_t i = 1; i < GRAPH_LITE_MAX_RELAY_STATES; i++) {
            uint16_t age = currentLo - relayStates[i].timestampLo;
            uint16_t oldestAge = currentLo - oldestTimestamp;
            if (age > oldestAge) {
                oldestIdx = i;
                oldestTimestamp = relayStates[i].timestampLo;
            }
        }

        relayStates[oldestIdx].nodeId = nodeId;
        relayStates[oldestIdx].packetId = packetId;
        relayStates[oldestIdx].timestampLo = static_cast<uint16_t>(currentTime & 0xFFFF);
    }
}

bool GraphLite::hasNodeTransmitted(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) const
{
    uint16_t currentLo = static_cast<uint16_t>(currentTime & 0xFFFF);

    for (uint8_t i = 0; i < relayStateCount; i++) {
        if (relayStates[i].nodeId == nodeId && relayStates[i].packetId == packetId) {
            uint16_t age = currentLo - relayStates[i].timestampLo;
            return age <= (getContentionWindowMs() / 1000 + 1); // Within contention window
        }
    }
    return false;
}

size_t GraphLite::getAllNodeIds(NodeNum *outArray, size_t maxCount) const
{
    size_t count = 0;
    for (uint8_t i = 0; i < nodeCount && count < maxCount; i++) {
        outArray[count++] = nodes[i].nodeId;
    }
    return count;
}

void GraphLite::removeNode(NodeNum nodeId)
{
    for (uint8_t n = 0; n < nodeCount; n++) {
        if (nodes[n].nodeId == nodeId) {
            // Remove this node by swapping with last
            if (n < nodeCount - 1) {
                nodes[n] = nodes[nodeCount - 1];
            }
            nodeCount--;

            // Also clear route cache if it was for this node
            if (routeCache.destination == nodeId) {
                routeCache.destination = 0;
                routeCacheTime = 0;
            }
            return;
        }
    }
}

void GraphLite::clearEdgesForNode(NodeNum nodeId)
{
    // Find and clear edges from this node
    NodeEdgesLite *node = findNode(nodeId);
    if (node) {
        node->edgeCount = 0;
    }

    // Remove edges from other nodes that point to this node
    for (uint8_t i = 0; i < nodeCount; i++) {
        NodeEdgesLite *otherNode = &nodes[i];
        // Remove edges where destination is the target node
        uint8_t writeIdx = 0;
        for (uint8_t readIdx = 0; readIdx < otherNode->edgeCount; readIdx++) {
            if (otherNode->edges[readIdx].to != nodeId) {
                if (writeIdx != readIdx) {
                    otherNode->edges[writeIdx] = otherNode->edges[readIdx];
                }
                writeIdx++;
            }
        }
        otherNode->edgeCount = writeIdx;
    }

    // Clear route cache if it involves this node
    if (routeCache.destination == nodeId) {
        routeCache.destination = 0;
        routeCacheTime = 0;
    }
}

