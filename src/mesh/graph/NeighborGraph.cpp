#include "NeighborGraph.h"
#include "configuration.h"
#include <algorithm>
#include <cmath>

NeighborGraph::NeighborGraph() : neighborCount(0), downstreamCount(0), relayStateCount(0), routeCacheCount(0) {}

// --- Private helpers ---

NodeEdges *NeighborGraph::findNeighbor(NodeNum nodeId)
{
    for (uint8_t i = 0; i < neighborCount; i++) {
        if (neighbors[i].nodeId == nodeId) {
            return &neighbors[i];
        }
    }
    return nullptr;
}

const NodeEdges *NeighborGraph::findNeighbor(NodeNum nodeId) const
{
    for (uint8_t i = 0; i < neighborCount; i++) {
        if (neighbors[i].nodeId == nodeId) {
            return &neighbors[i];
        }
    }
    return nullptr;
}

NodeEdges *NeighborGraph::findOrCreateNeighbor(NodeNum nodeId)
{
    NodeEdges *node = findNeighbor(nodeId);
    if (node) {
        return node;
    }

    if (neighborCount < NEIGHBOR_GRAPH_MAX_NEIGHBORS) {
        node = &neighbors[neighborCount++];
        node->nodeId = nodeId;
        node->edgeCount = 0;
        node->lastFullUpdate = 0;
        return node;
    }

    // Full - evict oldest non-self neighbor
    uint32_t currentTime = millis() / 1000;
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    uint32_t oldestTime = UINT32_MAX;
    uint8_t evictIdx = 0;
    uint8_t minEdges = 255;

    for (uint8_t i = 0; i < neighborCount; i++) {
        if (neighbors[i].nodeId == myNode)
            continue;
        if (currentTime - neighbors[i].lastFullUpdate < 120)
            continue;

        if (neighbors[i].lastFullUpdate < oldestTime ||
            (neighbors[i].lastFullUpdate == oldestTime && neighbors[i].edgeCount < minEdges)) {
            oldestTime = neighbors[i].lastFullUpdate;
            minEdges = neighbors[i].edgeCount;
            evictIdx = i;
        }
    }

    if (oldestTime == UINT32_MAX) {
        return nullptr;
    }

    // Also remove downstream entries that reference the evicted node as relay
    NodeNum evictedNode = neighbors[evictIdx].nodeId;
    for (uint16_t i = 0; i < downstreamCount;) {
        if (downstream[i].relay == evictedNode) {
            if (i < downstreamCount - 1) {
                downstream[i] = downstream[downstreamCount - 1];
            }
            downstreamCount--;
        } else {
            i++;
        }
    }

    node = &neighbors[evictIdx];
    node->nodeId = nodeId;
    node->edgeCount = 0;
    node->lastFullUpdate = 0;
    return node;
}

Edge *NeighborGraph::findEdge(NodeEdges *node, NodeNum to)
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

const Edge *NeighborGraph::findEdge(const NodeEdges *node, NodeNum to) const
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

bool NeighborGraph::isOurDirectNeighbor(NodeNum nodeId) const
{
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    const NodeEdges *myEdges = findNeighbor(myNode);
    if (!myEdges)
        return false;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        if (myEdges->edges[i].to == nodeId) {
            return true;
        }
    }
    return false;
}

// --- Core methods ---

int NeighborGraph::updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance,
                              Edge::Source source, bool updateTimestamp)
{
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;

    // Only store edges for our node and our direct neighbors
    // If 'from' is our node, always accept (slot 0 effectively)
    // If 'from' is already in neighbors[], update that neighbor's edge list
    // If 'from' is NOT in neighbors[]: only create a slot if 'from' is our direct neighbor
    //   or if we're adding an edge FROM our node
    bool isOurNode = (from == myNode);
    NodeEdges *existing = findNeighbor(from);

    if (!existing && !isOurNode) {
        // Check if 'from' is one of our direct neighbors (has an edge from us)
        if (!isOurDirectNeighbor(from)) {
            return EDGE_NO_CHANGE; // Remote node, not our neighbor - ignore
        }
    }

    NodeEdges *node = findOrCreateNeighbor(from);
    if (!node) {
        return EDGE_NO_CHANGE;
    }

    if (updateTimestamp) {
        node->lastFullUpdate = timestamp;
    }

    // If from == myNode and we're adding an edge to 'to', ensure 'to' has a neighbor slot
    if (isOurNode) {
        findOrCreateNeighbor(to);
    }

    Edge *edge = findEdge(node, to);
    if (edge) {
        if (edge->source == Edge::Source::Reported && source == Edge::Source::Mirrored) {
            return EDGE_NO_CHANGE;
        }

        float oldEtx = edge->getEtx();
        float change = (oldEtx > 0.0f) ? fabs(etx - oldEtx) / oldEtx : 1.0f;

        edge->setEtx(etx);
        if (updateTimestamp) {
            edge->lastUpdate = timestamp;
        }
        edge->variance = ((variance + 6) / 12 > 255) ? 255 : static_cast<uint8_t>((variance + 6) / 12);
        edge->source = source;

        return (change > ETX_CHANGE_THRESHOLD) ? EDGE_SIGNIFICANT_CHANGE : EDGE_NO_CHANGE;
    }

    // Add new edge
    if (node->edgeCount < NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE) {
        edge = &node->edges[node->edgeCount++];
        edge->to = to;
        edge->setEtx(etx);
        edge->lastUpdate = timestamp;
        edge->variance = ((variance + 6) / 12 > 255) ? 255 : static_cast<uint8_t>((variance + 6) / 12);
        edge->source = source;
        return EDGE_NEW;
    }

    // Edge list full - replace worst edge
    uint8_t worstIdx = 0;
    float worstScore = 0;
    for (uint8_t i = 0; i < node->edgeCount; i++) {
        float edgeEtx = node->edges[i].getEtx();
        float ageSeconds = static_cast<float>(timestamp - node->edges[i].lastUpdate);
        float score = edgeEtx + ageSeconds / 300.0f;
        if (score > worstScore) {
            worstScore = score;
            worstIdx = i;
        }
    }

    float newScore = etx;
    if (newScore < worstScore) {
        edge = &node->edges[worstIdx];
        edge->to = to;
        edge->setEtx(etx);
        edge->lastUpdate = timestamp;
        edge->variance = ((variance + 6) / 12 > 255) ? 255 : static_cast<uint8_t>((variance + 6) / 12);
        edge->source = source;
        return EDGE_SIGNIFICANT_CHANGE;
    }

    return EDGE_NO_CHANGE;
}

const NodeEdges *NeighborGraph::getEdgesFrom(NodeNum node) const
{
    return findNeighbor(node);
}

void NeighborGraph::updateNodeActivity(NodeNum nodeId, uint32_t timestamp)
{
    NodeEdges *node = findOrCreateNeighbor(nodeId);
    if (node) {
        node->lastFullUpdate = timestamp;
    }
}

void NeighborGraph::ageEdges(uint32_t currentTimeSecs, std::function<uint32_t(NodeNum)> getTtlForNode)
{
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    uint16_t currentLo = static_cast<uint16_t>(currentTimeSecs & 0xFFFF);
    bool edgesRemoved = false;

    for (uint8_t n = 0; n < neighborCount;) {
        NodeEdges *node = &neighbors[n];

        if (node->nodeId == myNode) {
            n++;
            continue;
        }

        uint32_t nodeTtl = getTtlForNode ? getTtlForNode(node->nodeId) : EDGE_AGING_TIMEOUT_SECS;

        // Age individual edges
        uint8_t writeIdx = 0;
        for (uint8_t i = 0; i < node->edgeCount; i++) {
            if ((currentTimeSecs - node->edges[i].lastUpdate) <= nodeTtl) {
                if (writeIdx != i) {
                    node->edges[writeIdx] = node->edges[i];
                }
                writeIdx++;
            }
        }
        if (writeIdx < node->edgeCount) {
            edgesRemoved = true;
            node->edgeCount = writeIdx;
        }

        bool isPlaceholder = (node->nodeId & 0xFF000000) == 0xFF000000;
        uint32_t ttl = isPlaceholder ? 300 : nodeTtl; // Placeholders: 5 min

        if (currentTimeSecs - node->lastFullUpdate > ttl || node->edgeCount == 0) {
            // Remove downstream entries that reference this neighbor as relay
            NodeNum removedNode = node->nodeId;
            for (uint16_t i = 0; i < downstreamCount;) {
                if (downstream[i].relay == removedNode) {
                    if (i < downstreamCount - 1) {
                        downstream[i] = downstream[downstreamCount - 1];
                    }
                    downstreamCount--;
                } else {
                    i++;
                }
            }

            if (n < neighborCount - 1) {
                neighbors[n] = neighbors[neighborCount - 1];
            }
            neighborCount--;
            edgesRemoved = true;
            continue;
        }
        n++;
    }

    // Age downstream entries
    for (uint16_t i = 0; i < downstreamCount;) {
        uint32_t dsTtl = getTtlForNode ? getTtlForNode(downstream[i].destination) : EDGE_AGING_TIMEOUT_SECS;
        if ((currentTimeSecs - downstream[i].lastUpdate) > dsTtl) {
            if (i < downstreamCount - 1) {
                downstream[i] = downstream[downstreamCount - 1];
            }
            downstreamCount--;
            edgesRemoved = true;
        } else {
            // Also remove if the relay is no longer our neighbor
            if (!findNeighbor(downstream[i].relay)) {
                if (i < downstreamCount - 1) {
                    downstream[i] = downstream[downstreamCount - 1];
                }
                downstreamCount--;
                edgesRemoved = true;
            } else {
                i++;
            }
        }
    }

    // Age relay states
    for (uint8_t i = 0; i < relayStateCount;) {
        uint16_t age = currentLo - relayStates[i].timestampLo;
        if (age > 2) {
            if (i < relayStateCount - 1) {
                relayStates[i] = relayStates[relayStateCount - 1];
            }
            relayStateCount--;
            continue;
        }
        i++;
    }

    if (edgesRemoved) {
        routeCacheCount = 0;
    }
}

Route NeighborGraph::calculateRoute(NodeNum destination, uint32_t currentTime, std::function<bool(NodeNum)> nodeFilter)
{
    // Check cache first
    Route cached = getCachedRoute(destination, currentTime);
    if (cached.nextHop != 0) {
        return cached;
    }

    Route result;
    result.destination = destination;
    result.nextHop = 0;
    result.costFixed = 0xFFFF;
    result.timestamp = currentTime;

    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    if (myNode == 0) {
        return result;
    }

    // 1. Direct neighbor check (1-hop)
    const NodeEdges *myEdges = findNeighbor(myNode);
    if (myEdges) {
        for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
            if (myEdges->edges[i].to == destination) {
                result.nextHop = destination;
                result.costFixed = myEdges->edges[i].etxFixed;
                if (routeCacheCount < NEIGHBOR_GRAPH_MAX_CACHED_ROUTES) {
                    routeCache[routeCacheCount++] = result;
                } else {
                    routeCache[0] = result;
                }
                return result;
            }
        }

        // 2. Neighbor's neighbor check (2-hop)
        for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
            NodeNum neighbor = myEdges->edges[i].to;

            if (nodeFilter && !nodeFilter(neighbor)) {
                continue;
            }

            uint16_t costToNeighbor = myEdges->edges[i].etxFixed;

            const NodeEdges *neighborEdges = findNeighbor(neighbor);
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

    // 3. Downstream table lookup (if no route found yet or found route is expensive)
    if (result.nextHop == 0) {
        for (uint16_t i = 0; i < downstreamCount; i++) {
            if (downstream[i].destination == destination) {
                // Verify the relay is still our neighbor and passes filter
                if (nodeFilter && !nodeFilter(downstream[i].relay)) {
                    continue;
                }
                if (!findNeighbor(downstream[i].relay)) {
                    continue;
                }
                // Find cost to relay from our edges
                uint16_t costToRelay = 0xFFFF;
                if (myEdges) {
                    for (uint8_t j = 0; j < myEdges->edgeCount; j++) {
                        if (myEdges->edges[j].to == downstream[i].relay) {
                            costToRelay = myEdges->edges[j].etxFixed;
                            break;
                        }
                    }
                }
                uint16_t totalCost = (costToRelay < 0xFFF0 && downstream[i].costFixed < 0xFFF0)
                                         ? costToRelay + downstream[i].costFixed
                                         : 0xFFFF;
                if (totalCost < result.costFixed) {
                    result.nextHop = downstream[i].relay;
                    result.costFixed = totalCost;
                }
            }
        }
    }

    if (result.nextHop != 0) {
        if (routeCacheCount < NEIGHBOR_GRAPH_MAX_CACHED_ROUTES) {
            routeCache[routeCacheCount++] = result;
        } else {
            routeCache[0] = result;
        }
    }

    return result;
}

Route NeighborGraph::getCachedRoute(NodeNum destination, uint32_t currentTime)
{
    for (uint8_t i = 0; i < routeCacheCount; i++) {
        if (routeCache[i].destination == destination && (currentTime - routeCache[i].timestamp) < ROUTE_CACHE_TIMEOUT_SECS) {
            return routeCache[i];
        }
    }
    return Route();
}

void NeighborGraph::clearCache()
{
    routeCacheCount = 0;
}

// --- Downstream methods ---

void NeighborGraph::updateDownstream(NodeNum destination, NodeNum relay, float totalCost, uint32_t timestamp)
{
    if (destination == 0 || relay == 0 || destination == relay)
        return;

    // Don't add downstream entries for ourselves
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    if (destination == myNode)
        return;

    uint16_t costFixed = static_cast<uint16_t>(std::min(totalCost * 100.0f, 65535.0f));

    // Update existing entry for the same (destination, relay) pair
    for (uint16_t i = 0; i < downstreamCount; i++) {
        if (downstream[i].destination == destination && downstream[i].relay == relay) {
            downstream[i].costFixed = costFixed;
            downstream[i].lastUpdate = timestamp;
            return;
        }
    }

    // Add new entry
    if (downstreamCount < NEIGHBOR_GRAPH_MAX_DOWNSTREAM) {
        DownstreamEntry &entry = downstream[downstreamCount++];
        entry.destination = destination;
        entry.relay = relay;
        entry.costFixed = costFixed;
        entry.lastUpdate = timestamp;
    } else {
        // Replace oldest entry
        uint8_t oldestIdx = 0;
        uint32_t oldestTime = downstream[0].lastUpdate;
        for (uint8_t i = 1; i < downstreamCount; i++) {
            if (downstream[i].lastUpdate < oldestTime) {
                oldestTime = downstream[i].lastUpdate;
                oldestIdx = i;
            }
        }
        downstream[oldestIdx].destination = destination;
        downstream[oldestIdx].relay = relay;
        downstream[oldestIdx].costFixed = costFixed;
        downstream[oldestIdx].lastUpdate = timestamp;
    }
}

NodeNum NeighborGraph::getDownstreamRelay(NodeNum destination) const
{
    uint32_t now = millis() / 1000;
    NodeNum bestRelay = 0;
    uint16_t bestCost = UINT16_MAX;
    for (uint16_t i = 0; i < downstreamCount; i++) {
        if (downstream[i].destination == destination && (now - downstream[i].lastUpdate) < EDGE_AGING_TIMEOUT_SECS) {
            if (downstream[i].costFixed < bestCost) {
                bestCost = downstream[i].costFixed;
                bestRelay = downstream[i].relay;
            }
        }
    }
    return bestRelay;
}

bool NeighborGraph::isDownstream(NodeNum destination) const
{
    return getDownstreamRelay(destination) != 0;
}

size_t NeighborGraph::getDownstreamCountForRelay(NodeNum relay) const
{
    size_t count = 0;
    uint32_t now = millis() / 1000;
    for (uint16_t i = 0; i < downstreamCount; i++) {
        if (downstream[i].relay == relay && (now - downstream[i].lastUpdate) < EDGE_AGING_TIMEOUT_SECS) {
            count++;
        }
    }
    return count;
}

size_t NeighborGraph::getDownstreamNodesForRelay(NodeNum relay, NodeNum *outArray, uint16_t *outCosts, size_t maxCount) const
{
    size_t count = 0;
    uint32_t now = millis() / 1000;
    for (uint16_t i = 0; i < downstreamCount && count < maxCount; i++) {
        if (downstream[i].relay == relay && (now - downstream[i].lastUpdate) < EDGE_AGING_TIMEOUT_SECS) {
            outArray[count] = downstream[i].destination;
            if (outCosts) outCosts[count] = downstream[i].costFixed;
            count++;
        }
    }
    return count;
}

bool NeighborGraph::isRelayFor(NodeNum myNode, NodeNum destination) const
{
    for (uint16_t i = 0; i < downstreamCount; i++) {
        if (downstream[i].destination == destination && downstream[i].relay == myNode) {
            uint32_t now = millis() / 1000;
            if ((now - downstream[i].lastUpdate) < EDGE_AGING_TIMEOUT_SECS) {
                return true;
            }
        }
    }
    return false;
}

void NeighborGraph::clearDownstreamForRelay(NodeNum relay)
{
    for (uint16_t i = 0; i < downstreamCount;) {
        if (downstream[i].relay == relay) {
            if (i < downstreamCount - 1) {
                downstream[i] = downstream[downstreamCount - 1];
            }
            downstreamCount--;
        } else {
            i++;
        }
    }
}

size_t NeighborGraph::transferDownstream(NodeNum oldRelay, NodeNum newRelay)
{
    uint32_t now = millis() / 1000;
    size_t count = 0;
    // First pass: add entries under newRelay
    for (uint16_t i = 0; i < downstreamCount; i++) {
        if (downstream[i].relay == oldRelay) {
            updateDownstream(downstream[i].destination, newRelay, downstream[i].costFixed / 100.0f, now);
            count++;
        }
    }
    // Second pass: remove old entries
    clearDownstreamForRelay(oldRelay);
    return count;
}

void NeighborGraph::clearDownstreamForDestination(NodeNum destination)
{
    for (uint16_t i = 0; i < downstreamCount;) {
        if (downstream[i].destination == destination) {
            if (i < downstreamCount - 1) {
                downstream[i] = downstream[downstreamCount - 1];
            }
            downstreamCount--;
        } else {
            i++;
        }
    }
}

// --- Static methods ---

float NeighborGraph::calculateETX(int32_t rssi, float snr)
{
    static constexpr int32_t rssiBreak[] = {-110, -100, -90, -80, -70, -60};
    static constexpr float probBreak[] = {0.05f, 0.15f, 0.40f, 0.65f, 0.85f, 0.95f};
    static constexpr int N = 6;

    float deliveryProb;
    if (rssi <= rssiBreak[0]) {
        deliveryProb = probBreak[0];
    } else if (rssi >= rssiBreak[N - 1]) {
        deliveryProb = probBreak[N - 1];
    } else {
        int seg = 0;
        for (int i = 1; i < N; i++) {
            if (rssi < rssiBreak[i]) {
                seg = i - 1;
                break;
            }
        }
        float t = static_cast<float>(rssi - rssiBreak[seg]) / static_cast<float>(rssiBreak[seg + 1] - rssiBreak[seg]);
        deliveryProb = probBreak[seg] + t * (probBreak[seg + 1] - probBreak[seg]);
    }

    float snrFactor;
    if (snr <= 0.0f) {
        snrFactor = 0.5f;
    } else if (snr >= 10.0f) {
        snrFactor = 1.0f;
    } else {
        snrFactor = 0.5f + snr * 0.05f;
    }
    deliveryProb *= snrFactor;

    return (deliveryProb > 0.0f) ? (1.0f / deliveryProb) : 100.0f;
}

void NeighborGraph::etxToSignal(float etx, int32_t &rssi, int32_t &snr)
{
    static constexpr int32_t rssiBreak[] = {-110, -100, -90, -80, -70, -60};
    static constexpr float probBreak[] = {0.05f, 0.15f, 0.40f, 0.65f, 0.85f, 0.95f};
    static constexpr int N = 6;

    float prob = 1.0f / std::max(etx, 1.0f);

    if (prob <= probBreak[0]) {
        rssi = rssiBreak[0];
    } else if (prob >= probBreak[N - 1]) {
        rssi = rssiBreak[N - 1];
    } else {
        int seg = 0;
        for (int i = 1; i < N; i++) {
            if (prob < probBreak[i]) {
                seg = i - 1;
                break;
            }
        }
        float t = (prob - probBreak[seg]) / (probBreak[seg + 1] - probBreak[seg]);
        rssi = rssiBreak[seg] + static_cast<int32_t>(t * (rssiBreak[seg + 1] - rssiBreak[seg]));
    }

    float etxAtSnr10 = calculateETX(rssi, 10.0f);
    if (etx <= etxAtSnr10 * 1.05f) {
        snr = 10;
    } else {
        float snrFactor = etxAtSnr10 / etx;
        if (snrFactor < 0.5f)
            snrFactor = 0.5f;
        float snrFloat = (snrFactor - 0.5f) / 0.05f;
        snr = static_cast<int32_t>(snrFloat);
        if (snr < -5)
            snr = -5;
        if (snr > 10)
            snr = 10;
    }
}

uint32_t NeighborGraph::getContentionWindowMs()
{
    // Dynamic contention window based on LoRa settings
    // This matches the behavior from Graph::getContentionWindowMs()
    uint32_t baseWindow = 2000; // 2 seconds base

    // Scale based on air time (LoRa modem config)
    // Faster presets need shorter windows, slower ones need longer
    if (config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST ||
        config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE) {
        baseWindow = 3000;
    } else if (config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW ||
               config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW) {
        baseWindow = 5000;
    } else if (config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO ||
               config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST) {
        baseWindow = 1500;
    }

    return baseWindow;
}

// --- Node management ---

uint8_t NeighborGraph::getNeighborCount(NodeNum node) const
{
    const NodeEdges *n = findNeighbor(node);
    return n ? n->edgeCount : 0;
}

size_t NeighborGraph::getAllNodeIds(NodeNum *outArray, size_t maxCount) const
{
    size_t count = 0;
    for (uint8_t i = 0; i < neighborCount && count < maxCount; i++) {
        outArray[count++] = neighbors[i].nodeId;
    }
    return count;
}

void NeighborGraph::removeNode(NodeNum nodeId)
{
    for (uint8_t n = 0; n < neighborCount; n++) {
        if (neighbors[n].nodeId == nodeId) {
            if (n < neighborCount - 1) {
                neighbors[n] = neighbors[neighborCount - 1];
            }
            neighborCount--;

            // Clear edges pointing TO the removed node from all other nodes
            for (uint8_t m = 0; m < neighborCount; m++) {
                for (uint8_t e = 0; e < neighbors[m].edgeCount;) {
                    if (neighbors[m].edges[e].to == nodeId) {
                        if (e < neighbors[m].edgeCount - 1) {
                            neighbors[m].edges[e] = neighbors[m].edges[neighbors[m].edgeCount - 1];
                        }
                        neighbors[m].edgeCount--;
                    } else {
                        e++;
                    }
                }
            }

            // Clear downstream entries referencing this node as relay or destination.
            // With multi-relay support, other relay entries for the same destination survive.
            for (uint16_t i = 0; i < downstreamCount;) {
                if (downstream[i].relay == nodeId || downstream[i].destination == nodeId) {
                    if (i < downstreamCount - 1) {
                        downstream[i] = downstream[downstreamCount - 1];
                    }
                    downstreamCount--;
                } else {
                    i++;
                }
            }

            // Clear route cache entries
            for (uint8_t i = 0; i < routeCacheCount;) {
                if (routeCache[i].destination == nodeId || routeCache[i].nextHop == nodeId) {
                    for (uint8_t j = i; j < routeCacheCount - 1; j++) {
                        routeCache[j] = routeCache[j + 1];
                    }
                    routeCacheCount--;
                } else {
                    i++;
                }
            }
            return;
        }
    }
}

void NeighborGraph::clearEdgesForNode(NodeNum nodeId)
{
    NodeEdges *node = findNeighbor(nodeId);
    if (node) {
        uint8_t writeIdx = 0;
        for (uint8_t i = 0; i < node->edgeCount; i++) {
            if (node->edges[i].source == Edge::Source::Reported) {
                if (writeIdx != i) {
                    node->edges[writeIdx] = node->edges[i];
                }
                writeIdx++;
            }
        }
        node->edgeCount = writeIdx;
    }

    for (uint8_t i = 0; i < routeCacheCount;) {
        if (routeCache[i].destination == nodeId || routeCache[i].nextHop == nodeId) {
            for (uint8_t j = i; j < routeCacheCount - 1; j++) {
                routeCache[j] = routeCache[j + 1];
            }
            routeCacheCount--;
        } else {
            i++;
        }
    }
}

void NeighborGraph::clearInferredEdgesToNode(NodeNum nodeId)
{
    for (uint8_t nodeIdx = 0; nodeIdx < neighborCount; nodeIdx++) {
        NodeEdges *node = &neighbors[nodeIdx];
        if (node->nodeId == 0)
            continue;

        uint8_t writeIdx = 0;
        for (uint8_t i = 0; i < node->edgeCount; i++) {
            if (!(node->edges[i].to == nodeId && node->edges[i].source == Edge::Source::Mirrored)) {
                if (writeIdx != i) {
                    node->edges[writeIdx] = node->edges[i];
                }
                writeIdx++;
            }
        }
        node->edgeCount = writeIdx;
    }

    routeCacheCount = 0;
}

// --- Relay decisions (ported from GraphLite) ---

size_t NeighborGraph::getCoverageIfRelays(NodeNum relay, NodeNum *coveredNodes, size_t maxNodes,
                                           const NodeNum *alreadyCovered, size_t alreadyCoveredCount) const
{
    if (!coveredNodes || maxNodes == 0)
        return 0;

    size_t coveredCount = 0;
    const NodeEdges *relayEdges = findNeighbor(relay);
    if (!relayEdges)
        return 0;

    for (uint8_t i = 0; i < relayEdges->edgeCount && coveredCount < maxNodes; i++) {
        NodeNum target = relayEdges->edges[i].to;

        bool isAlreadyCovered = false;
        for (size_t j = 0; j < alreadyCoveredCount; j++) {
            if (alreadyCovered[j] == target) {
                isAlreadyCovered = true;
                break;
            }
        }

        if (!isAlreadyCovered) {
            coveredNodes[coveredCount++] = target;
        }
    }

    return coveredCount;
}

RelayCandidate NeighborGraph::findBestRelayCandidate(const std::unordered_set<NodeNum> &candidates,
                                                          const std::unordered_set<NodeNum> &alreadyCovered,
                                                          uint32_t currentTime, uint32_t packetId) const
{
    RelayCandidate bestCandidate(0, 0, 0, 0);

    for (NodeNum candidate : candidates) {
        if (hasNodeTransmitted(candidate, packetId, currentTime)) {
            continue;
        }

        NodeNum newCoverage[NEIGHBOR_GRAPH_MAX_NEIGHBORS * NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE];
        size_t maxCoverage = sizeof(newCoverage) / sizeof(newCoverage[0]);
        size_t coverageCount = getCoverageIfRelays(candidate, newCoverage, maxCoverage, nullptr, 0);

        size_t uniqueCoverageCount = 0;
        for (size_t i = 0; i < coverageCount; i++) {
            if (alreadyCovered.find(newCoverage[i]) == alreadyCovered.end()) {
                newCoverage[uniqueCoverageCount++] = newCoverage[i];
            }
        }

        if (uniqueCoverageCount == 0) {
            continue;
        }

        float totalCost = 0;
        size_t validCosts = 0;

        const NodeEdges *candidateEdges = findNeighbor(candidate);
        if (candidateEdges) {
            for (size_t j = 0; j < uniqueCoverageCount; j++) {
                const Edge *edge = findEdge(candidateEdges, newCoverage[j]);
                if (edge) {
                    totalCost += edge->getEtx();
                    validCosts++;
                }
            }
        }

        if (validCosts == 0)
            continue;

        float avgCost = totalCost / validCosts;
        uint16_t avgCostFixed = static_cast<uint16_t>(avgCost * 100);

        if (uniqueCoverageCount > bestCandidate.coverageCount ||
            (uniqueCoverageCount == bestCandidate.coverageCount && avgCostFixed < bestCandidate.avgCostFixed)) {
            bestCandidate = RelayCandidate(candidate, uniqueCoverageCount, avgCostFixed, 0);
        }
    }

    return bestCandidate;
}

bool NeighborGraph::isGatewayNode(NodeNum nodeId, NodeNum sourceNode) const
{
    const NodeEdges *nodeEdges = findNeighbor(nodeId);
    const NodeEdges *sourceEdges = findNeighbor(sourceNode);

    if (!nodeEdges || nodeEdges->edgeCount == 0) {
        return false;
    }

    for (uint8_t i = 0; i < nodeEdges->edgeCount; i++) {
        NodeNum neighbor = nodeEdges->edges[i].to;
        if (neighbor == sourceNode)
            continue;

        bool sourceHasNeighbor = false;
        if (sourceEdges) {
            for (uint8_t j = 0; j < sourceEdges->edgeCount; j++) {
                if (sourceEdges->edges[j].to == neighbor) {
                    sourceHasNeighbor = true;
                    break;
                }
            }
        }

        if (!sourceHasNeighbor) {
            const NodeEdges *neighborEdges = findNeighbor(neighbor);
            if (neighborEdges && neighborEdges->edgeCount > 1) {
                return true;
            }
        }
    }

    return false;
}

bool NeighborGraph::shouldRelayEnhanced(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime,
                                         uint32_t packetId, uint32_t packetRxTime) const
{
    std::unordered_set<NodeNum> alreadyCovered;
    alreadyCovered.insert(sourceNode);
    alreadyCovered.insert(heardFrom);

    const NodeEdges *transmittingEdges = findNeighbor(heardFrom);
    if (transmittingEdges) {
        for (uint8_t i = 0; i < transmittingEdges->edgeCount; i++) {
            alreadyCovered.insert(transmittingEdges->edges[i].to);
        }
    }

    std::unordered_set<NodeNum> candidates;
    if (transmittingEdges) {
        for (uint8_t i = 0; i < transmittingEdges->edgeCount; i++) {
            candidates.insert(transmittingEdges->edges[i].to);
        }
    }

    while (!candidates.empty()) {
        RelayCandidate bestCandidate = findBestRelayCandidate(candidates, alreadyCovered, currentTime, packetId);

        if (bestCandidate.nodeId == 0) {
            break;
        }

        if (bestCandidate.nodeId == myNode) {
            return true;
        }

        if (isGatewayNode(myNode, sourceNode)) {
            return true;
        }

        bool bestHasTransmitted = hasNodeTransmitted(bestCandidate.nodeId, packetId, currentTime);

        if (!bestHasTransmitted) {
            if (packetRxTime > 0) {
                uint32_t timeSinceRx = currentTime - packetRxTime;
                uint32_t contentionWindowMs = getContentionWindowMs();
                if (timeSinceRx > (contentionWindowMs + 500)) {
                    candidates.erase(bestCandidate.nodeId);
                    continue;
                }
            }
            return false;
        }

        std::unordered_set<NodeNum> relayCoverage;
        for (NodeNum candidate : candidates) {
            if (hasNodeTransmitted(candidate, packetId, currentTime)) {
                const NodeEdges *candidateEdges = findNeighbor(candidate);
                if (candidateEdges) {
                    for (uint8_t i = 0; i < candidateEdges->edgeCount; i++) {
                        relayCoverage.insert(candidateEdges->edges[i].to);
                    }
                }
            }
        }

        const NodeEdges *myEdges = findNeighbor(myNode);
        if (myEdges) {
            bool haveUniqueCoverage = false;
            for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
                NodeNum neighbor = myEdges->edges[i].to;
                if (alreadyCovered.find(neighbor) == alreadyCovered.end() &&
                    relayCoverage.find(neighbor) == relayCoverage.end()) {
                    haveUniqueCoverage = true;
                    break;
                }
            }

            if (haveUniqueCoverage) {
                return true;
            }
        }

        return false;
    }

    const NodeEdges *myEdges = findNeighbor(myNode);
    if (myEdges && myEdges->edgeCount > 0) {
        return true;
    }

    return false;
}

bool NeighborGraph::shouldRelayEnhancedConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom,
                                                     uint32_t currentTime, uint32_t packetId,
                                                     uint32_t packetRxTime) const
{
    const NodeEdges *myEdges = findNeighbor(myNode);
    if (!myEdges)
        return false;

    bool hasStockGateways = false;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        const NodeEdges *neighborEdges = findNeighbor(myEdges->edges[i].to);
        if (neighborEdges && neighborEdges->edgeCount >= 8) {
            hasStockGateways = true;
            break;
        }
    }

    if (hasStockGateways) {
        return shouldRelaySimpleConservative(myNode, sourceNode, heardFrom, currentTime);
    }

    return shouldRelayEnhanced(myNode, sourceNode, heardFrom, currentTime, packetId, packetRxTime);
}

bool NeighborGraph::shouldRelaySimple(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const
{
    const NodeEdges *myEdges = findNeighbor(myNode);
    const NodeEdges *transmittingEdges = findNeighbor(heardFrom);

    if (!myEdges || myEdges->edgeCount == 0) {
        return false;
    }

    if (!transmittingEdges) {
        return false;
    }

    std::unordered_set<NodeNum> alreadyCovered;
    alreadyCovered.insert(sourceNode);
    alreadyCovered.insert(heardFrom);

    for (uint8_t i = 0; i < transmittingEdges->edgeCount; i++) {
        alreadyCovered.insert(transmittingEdges->edges[i].to);
    }

    uint8_t uniqueNeighbors = 0;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        NodeNum neighbor = myEdges->edges[i].to;
        if (alreadyCovered.find(neighbor) == alreadyCovered.end()) {
            uniqueNeighbors++;
        }
    }

    return uniqueNeighbors > 0;
}

bool NeighborGraph::shouldRelaySimpleConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom,
                                                   uint32_t currentTime) const
{
    const NodeEdges *myEdges = findNeighbor(myNode);
    const NodeEdges *transmittingEdges = findNeighbor(heardFrom);

    if (!myEdges || myEdges->edgeCount == 0) {
        return false;
    }

    if (!transmittingEdges) {
        LOG_DEBUG("NeighborGraph: No topology for transmitting node %08x - fallback relay", heardFrom);
        return true;
    }

    uint8_t uniqueSrNeighbors = 0;
    uint8_t totalNeighborsNotCovered = 0;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        NodeNum neighbor = myEdges->edges[i].to;
        if (neighbor == sourceNode || neighbor == heardFrom) {
            continue;
        }

        bool transmittingHasIt = false;
        for (uint8_t j = 0; j < transmittingEdges->edgeCount; j++) {
            if (transmittingEdges->edges[j].to == neighbor) {
                transmittingHasIt = true;
                break;
            }
        }

        if (!transmittingHasIt) {
            uniqueSrNeighbors++;
            totalNeighborsNotCovered++;
        }
    }

    if (uniqueSrNeighbors >= 2) {
        return true;
    }

    if (totalNeighborsNotCovered > 0) {
        LOG_DEBUG("NeighborGraph: Conservative fallback - have %u uncovered neighbors, relaying", totalNeighborsNotCovered);
        return true;
    }

    return false;
}

bool NeighborGraph::shouldRelayWithContention(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t packetId,
                                               uint32_t currentTime) const
{
    const NodeEdges *myEdges = findNeighbor(myNode);
    const NodeEdges *sourceEdges = findNeighbor(sourceNode);
    const NodeEdges *relayEdges = (heardFrom == sourceNode) ? nullptr : findNeighbor(heardFrom);

    if (!myEdges || myEdges->edgeCount == 0) {
        return false;
    }

    uint8_t uniqueNeighbors = 0;
    for (uint8_t i = 0; i < myEdges->edgeCount; i++) {
        NodeNum neighbor = myEdges->edges[i].to;
        if (neighbor == sourceNode || neighbor == heardFrom) {
            continue;
        }

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

    if (uniqueNeighbors == 0) {
        return false;
    }

    for (uint8_t i = 0; i < neighborCount; i++) {
        NodeNum otherNode = neighbors[i].nodeId;
        if (otherNode != myNode && otherNode != sourceNode && otherNode != heardFrom) {
            if (hasNodeTransmitted(otherNode, packetId, currentTime)) {
                return false;
            }
        }
    }

    return true;
}

void NeighborGraph::recordNodeTransmission(NodeNum nodeId, uint32_t packetId, uint32_t currentTime)
{
    for (uint8_t i = 0; i < relayStateCount; i++) {
        if (relayStates[i].nodeId == nodeId) {
            relayStates[i].packetId = packetId;
            relayStates[i].timestampLo = static_cast<uint16_t>(currentTime & 0xFFFF);
            return;
        }
    }

    if (relayStateCount < NEIGHBOR_GRAPH_MAX_RELAY_STATES) {
        relayStates[relayStateCount].nodeId = nodeId;
        relayStates[relayStateCount].packetId = packetId;
        relayStates[relayStateCount].timestampLo = static_cast<uint16_t>(currentTime & 0xFFFF);
        relayStateCount++;
    } else {
        uint8_t oldestIdx = 0;
        uint16_t oldestTimestamp = relayStates[0].timestampLo;
        uint16_t currentLo = static_cast<uint16_t>(currentTime & 0xFFFF);

        for (uint8_t i = 1; i < NEIGHBOR_GRAPH_MAX_RELAY_STATES; i++) {
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

bool NeighborGraph::hasNodeTransmitted(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) const
{
    uint16_t currentLo = static_cast<uint16_t>(currentTime & 0xFFFF);

    for (uint8_t i = 0; i < relayStateCount; i++) {
        if (relayStates[i].nodeId == nodeId && relayStates[i].packetId == packetId) {
            uint16_t age = currentLo - relayStates[i].timestampLo;
            return age <= (getContentionWindowMs() / 1000 + 1);
        }
    }
    return false;
}
