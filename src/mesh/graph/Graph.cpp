#include "Graph.h"
#include "RTC.h"
#include <algorithm>
#include <cmath>

Graph::Graph() {}

Graph::~Graph() {}

bool Graph::hasMemoryForNewNode() const {
    uint32_t freeHeap = memGet.getFreeHeap();
    // Estimate memory needed for a new node with average edges
    size_t estimatedMemory = NODE_OVERHEAD_ESTIMATE + (MAX_EDGES_PER_NODE / 2) * EDGE_MEMORY_ESTIMATE;
    return freeHeap > (MIN_FREE_HEAP_FOR_GRAPH + estimatedMemory);
}

int Graph::updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance, Edge::Source source) {
    // Check if this is a new node
    bool isNewNode = (adjacencyList.find(from) == adjacencyList.end());
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;

    if (isNewNode && !hasMemoryForNewNode()) {
        // Graph is full - find the least connected node to potentially evict
        // Prioritize keeping well-connected "hub" nodes for better network reliability
        // NEVER evict nodes whose only connection is to us - we're their bridge to the network
        // "Worst" = node with fewest neighbors (least connected)
        // Tie-breaker: highest average ETX (poorest links)
        NodeNum worstNode = 0;
        size_t worstNeighborCount = SIZE_MAX;
        float worstAvgEtx = 0;

        for (const auto& pair : adjacencyList) {
            if (pair.second.empty()) continue;

            // Never evict nodes that only connect to us - we're their only path to the network
            if (pair.second.size() == 1 && pair.second[0].to == myNode) {
                continue;
            }

            size_t neighborCount = pair.second.size();
            float totalEtx = 0;
            for (const auto& edge : pair.second) {
                totalEtx += edge.etx;
            }
            float avgEtx = totalEtx / neighborCount;

            // Prefer to evict nodes with fewer neighbors
            // If same neighbor count, evict the one with worse average ETX
            if (neighborCount < worstNeighborCount ||
                (neighborCount == worstNeighborCount && avgEtx > worstAvgEtx)) {
                worstNeighborCount = neighborCount;
                worstAvgEtx = avgEtx;
                worstNode = pair.first;
            }
        }

        // If no evictable node found (all are bridge-dependent), don't add new node
        if (worstNode == 0) {
            return EDGE_NO_CHANGE;
        }

        // Evict if the new node could potentially be better connected
        // We don't know yet how many neighbors the new node has, so we're optimistic
        // and allow it if the worst node has only 1 neighbor, or if the new edge is good
        if (worstNeighborCount <= 1 || etx < worstAvgEtx) {
            adjacencyList.erase(worstNode);
            routeCache.clear(); // Invalidate cache since topology changed
        } else {
            // New node isn't better than existing ones, don't add
            return EDGE_NO_CHANGE;
        }
    }

    auto& edges = adjacencyList[from];

    // Find existing edge
    auto it = std::find_if(edges.begin(), edges.end(),
        [to](const Edge& e) { return e.to == to; });

    if (it != edges.end()) {
        // If we already have a reported edge, don't overwrite with a mirrored guess
        if (it->source == Edge::Source::Reported && source == Edge::Source::Mirrored) {
            return EDGE_NO_CHANGE;
        }

        // Check for significant change
        float oldEtx = it->etx;
        float change = std::abs(etx - oldEtx) / oldEtx;

        // Update existing edge
        it->etx = etx;
        it->lastUpdate = timestamp;
        it->variance = variance;
        it->source = source;

        return (change > ETX_CHANGE_THRESHOLD) ? EDGE_SIGNIFICANT_CHANGE : EDGE_NO_CHANGE;
    } else {
        // Check edge limit per node
        if (edges.size() >= MAX_EDGES_PER_NODE) {
            // Find the worst edge (highest ETX) and replace if new one is better
            auto worstIt = std::max_element(edges.begin(), edges.end(),
                [](const Edge& a, const Edge& b) { return a.etx < b.etx; });
            
            if (etx < worstIt->etx) {
                // Replace worst edge with new better one
                worstIt->to = to;
                worstIt->etx = etx;
                worstIt->lastUpdate = timestamp;
                worstIt->variance = variance;
                worstIt->source = source;
                return EDGE_SIGNIFICANT_CHANGE;
            }
            // New edge is worse than all existing, don't add
            return EDGE_NO_CHANGE;
        }

        // Add new edge
        Edge newEdge(from, to, etx, timestamp, source);
        newEdge.variance = variance;
        edges.push_back(newEdge);
        return EDGE_NEW;
    }
}

void Graph::ageEdges(uint32_t currentTime) {
    for (auto& pair : adjacencyList) {
        auto& edges = pair.second;
        edges.erase(
            std::remove_if(edges.begin(), edges.end(),
                [currentTime](const Edge& e) {
                    return (currentTime - e.lastUpdate) > EDGE_AGING_TIMEOUT_MS;
                }),
            edges.end());
    }

    // Clear empty adjacency lists
    for (auto it = adjacencyList.begin(); it != adjacencyList.end();) {
        if (it->second.empty()) {
            it = adjacencyList.erase(it);
        } else {
            ++it;
        }
    }

    // Age relay states - remove old transmission records
    for (auto it = relayStates.begin(); it != relayStates.end();) {
        if ((currentTime - it->second.lastTxTime) > RELAY_STATE_TIMEOUT_MS) {
            it = relayStates.erase(it);
        } else {
            ++it;
        }
    }
}

Route Graph::calculateRoute(NodeNum destination, uint32_t currentTime) {
    // Age edges before calculating
    ageEdges(currentTime);

    // Check cache first
    auto cached = getCachedRoute(destination, currentTime);
    if (cached.nextHop != 0) {
        return cached;
    }

    // Calculate new route
    NodeNum myNode = nodeDB ? nodeDB->getNodeNum() : 0;
    if (myNode == 0) {
        return Route(destination, 0, std::numeric_limits<float>::infinity(), currentTime);
    }
    Route route = dijkstra(myNode, destination, currentTime);

    // Cache the result
    if (route.nextHop != 0) {
        routeCache[destination] = route;
    }

    return route;
}

Route Graph::getCachedRoute(NodeNum destination, uint32_t currentTime) {
    auto it = routeCache.find(destination);
    if (it != routeCache.end()) {
        const Route& cached = it->second;
        if ((currentTime - cached.timestamp) < ROUTE_CACHE_TIMEOUT_MS) {
            return cached;
        } else {
            // Cache expired, remove it
            routeCache.erase(it);
        }
    }
    return Route(destination, 0, std::numeric_limits<float>::infinity(), currentTime);
}

void Graph::clearCache() {
    routeCache.clear();
}

float Graph::calculateETX(int32_t rssi, float snr) {
    // ETX calculation based on RSSI and SNR
    // This is a simplified model - in practice, this would be based on
    // empirical measurements of delivery probability

    // Convert RSSI to delivery probability (simplified model)
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

    // Factor in SNR (now correctly a float)
    if (snr < 5.0f) {
        deliveryProb *= 0.5f;
    } else if (snr < 10.0f) {
        deliveryProb *= 0.8f;
    }

    // ETX = 1 / delivery_probability
    if (deliveryProb > 0.0f) {
        return 1.0f / deliveryProb;
    } else {
        return std::numeric_limits<float>::infinity();
    }
}

void Graph::updateStability(NodeNum from, NodeNum to, float newStability) {
    auto& edges = adjacencyList[from];
    auto it = std::find_if(edges.begin(), edges.end(),
        [to](const Edge& e) { return e.to == to; });

    if (it != edges.end()) {
        it->stability = newStability;
    }
}

Route Graph::dijkstra(NodeNum source, NodeNum destination, uint32_t currentTime) {
    std::unordered_map<NodeNum, float> distances;
    std::unordered_map<NodeNum, NodeNum> previous;
    std::priority_queue<std::pair<float, NodeNum>, std::vector<std::pair<float, NodeNum>>, std::greater<std::pair<float, NodeNum>>> pq;

    // Initialize distances
    for (const auto& pair : adjacencyList) {
        distances[pair.first] = std::numeric_limits<float>::infinity();
    }
    distances[source] = 0.0f;
    pq.push({0.0f, source});

    while (!pq.empty()) {
        auto [cost, current] = pq.top();
        pq.pop();

        if (cost > distances[current]) continue;

        if (current == destination) break;

        auto it = adjacencyList.find(current);
        if (it == adjacencyList.end()) continue;

        for (const Edge& edge : it->second) {
            float weightedCost = getWeightedCost(edge, currentTime);
            float newCost = distances[current] + weightedCost;

            if (newCost < distances[edge.to]) {
                distances[edge.to] = newCost;
                previous[edge.to] = current;
                pq.push({newCost, edge.to});
            }
        }
    }

    // Reconstruct path
    if (distances[destination] == std::numeric_limits<float>::infinity()) {
        return Route(destination, 0, std::numeric_limits<float>::infinity(), currentTime);
    }

    // Find next hop
    NodeNum nextHop = destination;
    NodeNum current = destination;
    while (previous[current] != source) {
        nextHop = previous[current];
        current = previous[current];
        if (current == 0) break; // No path found
    }

    return Route(destination, nextHop, distances[destination], currentTime);
}

float Graph::getWeightedCost(const Edge& edge, uint32_t currentTime) {
    // Age factor - older edges cost more (up to 2x penalty at timeout)
    uint32_t age = currentTime - edge.lastUpdate;
    float ageFactor = 1.0f + (age / static_cast<float>(EDGE_AGING_TIMEOUT_MS));

    // Stability weighting (historical reliability)
    float stabilityFactor = 1.0f / edge.stability;

    // Variance factor - mobile/unreliable nodes get penalized
    // variance of 0 = no penalty, variance of 1000+ = significant penalty
    // Formula: 1.0 + (variance / 500) caps at ~3x penalty for very mobile nodes
    float varianceFactor = 1.0f + (edge.variance / 500.0f);
    if (varianceFactor > 3.0f) varianceFactor = 3.0f; // Cap at 3x penalty

    return edge.etx * ageFactor * stabilityFactor * varianceFactor;
}

const std::vector<Edge>* Graph::getEdgesFrom(NodeNum node) const {
    auto it = adjacencyList.find(node);
    if (it != adjacencyList.end()) {
        return &it->second;
    }
    return nullptr;
}

void Graph::etxToSignal(float etx, int32_t &rssi, int32_t &snr) {
    // Reverse the ETX calculation (approximate)
    // Original: etx = 1.0 / (prr * prr) where prr depends on rssi/snr
    // This is an approximation - we'll estimate reasonable values

    // ETX of 1.0 = perfect link (RSSI ~ -60, SNR ~ 10)
    // ETX of 2.0 = 50% packet loss (RSSI ~ -90, SNR ~ 0)
    // ETX of 4.0 = 75% packet loss (RSSI ~ -110, SNR ~ -5)

    if (etx <= 1.0f) {
        rssi = -60;
        snr = 10;
    } else if (etx <= 2.0f) {
        // Linear interpolation between good and medium
        float t = (etx - 1.0f);
        rssi = -60 - static_cast<int32_t>(t * 30);
        snr = 10 - static_cast<int32_t>(t * 10);
    } else {
        // Linear interpolation between medium and poor
        float t = std::min((etx - 2.0f) / 2.0f, 1.0f);
        rssi = -90 - static_cast<int32_t>(t * 20);
        snr = 0 - static_cast<int32_t>(t * 5);
    }
}

std::unordered_set<NodeNum> Graph::getDirectNeighbors(NodeNum node) const {
    std::unordered_set<NodeNum> neighbors;
    auto it = adjacencyList.find(node);
    if (it != adjacencyList.end()) {
        for (const Edge& edge : it->second) {
            neighbors.insert(edge.to);
        }
    }
    return neighbors;
}

std::unordered_set<NodeNum> Graph::getAllNodes() const {
    std::unordered_set<NodeNum> nodes;
    for (const auto& pair : adjacencyList) {
        nodes.insert(pair.first);
        for (const Edge& edge : pair.second) {
            nodes.insert(edge.to);
        }
    }
    return nodes;
}

std::unordered_set<NodeNum> Graph::getCoverageIfRelays(NodeNum relay, const std::unordered_set<NodeNum>& alreadyCovered) const {
    std::unordered_set<NodeNum> newCoverage;

    // Get all nodes that can hear this relay (nodes that have edges TO the relay)
    // Since our graph stores edges as "from -> to", we need to find edges where to == relay
    // But that's expensive. Instead, we use the relay's neighbors (nodes the relay can reach)
    // Assumption: if relay can reach X, then X can hear relay (bidirectional links)

    auto neighbors = getDirectNeighbors(relay);
    for (NodeNum neighbor : neighbors) {
        if (alreadyCovered.find(neighbor) == alreadyCovered.end()) {
            newCoverage.insert(neighbor);
        }
    }

    return newCoverage;
}

float Graph::getEdgeCost(NodeNum from, NodeNum to, uint32_t currentTime) const {
    auto it = adjacencyList.find(from);
    if (it == adjacencyList.end()) {
        return std::numeric_limits<float>::infinity();
    }

    for (const Edge& edge : it->second) {
        if (edge.to == to) {
            // Calculate weighted cost (same as in dijkstra)
            uint32_t age = currentTime - edge.lastUpdate;
            float ageFactor = 1.0f + (age / static_cast<float>(EDGE_AGING_TIMEOUT_MS));
            float stabilityFactor = 1.0f / edge.stability;
            float varianceFactor = 1.0f + (edge.variance / 500.0f);
            if (varianceFactor > 3.0f) varianceFactor = 3.0f;
            return edge.etx * ageFactor * stabilityFactor * varianceFactor;
        }
    }

    return std::numeric_limits<float>::infinity();
}

NodeNum Graph::findBestRelay(const std::unordered_set<NodeNum>& alreadyCovered,
                              const std::unordered_set<NodeNum>& candidates,
                              uint32_t currentTime) const {
    NodeNum bestRelay = 0;
    size_t bestCoverageCount = 0;
    float bestCost = std::numeric_limits<float>::infinity();

    for (NodeNum candidate : candidates) {
        auto newCoverage = getCoverageIfRelays(candidate, alreadyCovered);
        size_t coverageCount = newCoverage.size();

        if (coverageCount == 0) continue;

        // Calculate average cost to covered nodes
        float totalCost = 0;
        for (NodeNum covered : newCoverage) {
            totalCost += getEdgeCost(candidate, covered, currentTime);
        }
        float avgCost = totalCost / coverageCount;

        // Prefer: more coverage first, then lower cost
        if (coverageCount > bestCoverageCount ||
            (coverageCount == bestCoverageCount && avgCost < bestCost)) {
            bestRelay = candidate;
            bestCoverageCount = coverageCount;
            bestCost = avgCost;
        }
    }

    return bestRelay;
}

bool Graph::shouldRelay(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const {
    // Build set of nodes that have already "covered" (source + anyone who relayed)
    std::unordered_set<NodeNum> alreadyCovered;
    alreadyCovered.insert(sourceNode);

    // Add all nodes that the source can reach directly
    auto sourceNeighbors = getDirectNeighbors(sourceNode);
    for (NodeNum n : sourceNeighbors) {
        alreadyCovered.insert(n);
    }

    // If we heard from a relayer (not the source), add their coverage too
    if (heardFrom != sourceNode) {
        alreadyCovered.insert(heardFrom);
        auto relayerNeighbors = getDirectNeighbors(heardFrom);
        for (NodeNum n : relayerNeighbors) {
            alreadyCovered.insert(n);
        }
    }

    // If the upstream relayer already reaches everyone we can reach, do not relay.
    {
        std::unordered_set<NodeNum> emptyCovered;
        auto myCoverage = getCoverageIfRelays(myNode, emptyCovered);
        if (heardFrom != 0) {
            auto relayerCoverage = getCoverageIfRelays(heardFrom, emptyCovered);
            bool redundant = !myCoverage.empty();
            for (NodeNum n : myCoverage) {
                if (relayerCoverage.find(n) == relayerCoverage.end()) {
                    redundant = false;
                    break;
                }
            }
            if (redundant) {
                return false;
            }
        }
    }

    // Get all nodes that heard this packet (source's neighbors + relayer's neighbors)
    // These are the candidates who could relay
    std::unordered_set<NodeNum> candidates;
    for (NodeNum n : sourceNeighbors) {
        candidates.insert(n);
    }
    if (heardFrom != sourceNode) {
        auto relayerNeighbors = getDirectNeighbors(heardFrom);
        for (NodeNum n : relayerNeighbors) {
            candidates.insert(n);
        }
    }

    // Find the best relay among candidates
    NodeNum bestRelay = findBestRelay(alreadyCovered, candidates, currentTime);

    // Should we relay?
    if (bestRelay == myNode) {
        return true;
    }

    // If no good relay found (all nodes covered), don't relay
    if (bestRelay == 0) {
        return false;
    }

    // We're not the best relay - let the best one handle it
    return false;
}

void Graph::recordNodeTransmission(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) {
    relayStates[nodeId] = RelayState(currentTime, packetId);
    LOG_DEBUG("Graph: Recorded transmission from node %08x for packet %08x at time %u", nodeId, packetId, currentTime);
}

bool Graph::hasNodeTransmitted(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) const {
    auto it = relayStates.find(nodeId);
    if (it == relayStates.end()) {
        LOG_DEBUG("Graph: Node %08x has no recent transmission record", nodeId);
        return false;  // Node hasn't transmitted anything recently
    }

    const RelayState& state = it->second;

    // If it's a different packet, they haven't transmitted for this one
    if (state.packetId != packetId) {
        LOG_DEBUG("Graph: Node %08x transmitted for different packet %08x (current: %08x)", nodeId, state.packetId, packetId);
        return false;
    }

    // Check if transmission was within the contention window
    uint32_t timeSinceTx = currentTime - state.lastTxTime;
    bool hasTransmitted = timeSinceTx <= CONTENTION_WINDOW_MS;
    LOG_DEBUG("Graph: Node %08x %s transmitted for packet %08x (%ums ago, window: %ums)",
              nodeId, hasTransmitted ? "HAS" : "has NOT", packetId, timeSinceTx, CONTENTION_WINDOW_MS);
    return hasTransmitted;
}

std::vector<RelayCandidate> Graph::findAllRelayCandidates(const std::unordered_set<NodeNum>& alreadyCovered,
                                                         const std::unordered_set<NodeNum>& candidates,
                                                         uint32_t currentTime, uint32_t packetId) const {
    std::vector<RelayCandidate> relayCandidates;

    LOG_DEBUG("Graph: Finding relay candidates from %zu potential nodes", candidates.size());

    for (NodeNum candidate : candidates) {
        // Skip candidates that have already transmitted for this packet
        if (hasNodeTransmitted(candidate, packetId, currentTime)) {
            continue;
        }

        auto newCoverage = getCoverageIfRelays(candidate, alreadyCovered);
        size_t coverageCount = newCoverage.size();

        if (coverageCount == 0) {
            LOG_DEBUG("Graph: Candidate %08x provides no additional coverage", candidate);
            continue;
        }

        // Calculate average cost to covered nodes
        float totalCost = 0;
        for (NodeNum covered : newCoverage) {
            totalCost += getEdgeCost(candidate, covered, currentTime);
        }
        float avgCost = totalCost / coverageCount;

        relayCandidates.emplace_back(candidate, coverageCount, avgCost, 0);  // Tier will be set later
        LOG_DEBUG("Graph: Candidate %08x covers %zu nodes with avg cost %.2f", candidate, coverageCount, avgCost);
    }

    // Sort candidates by coverage (descending) then cost (ascending)
    std::sort(relayCandidates.begin(), relayCandidates.end(),
              [](const RelayCandidate& a, const RelayCandidate& b) {
                  if (a.coverageCount != b.coverageCount) return a.coverageCount > b.coverageCount;
                  return a.avgCost < b.avgCost;
              });

    // Assign tiers: top coverage gets tier 0, next get tier 1, etc.
    int currentTier = 0;
    size_t currentTierCoverage = 0;
    for (auto& candidate : relayCandidates) {
        if (candidate.coverageCount < currentTierCoverage) {
            currentTier++;
            if (currentTier >= MAX_RELAY_TIERS) break;
        }
        candidate.tier = currentTier;
        currentTierCoverage = candidate.coverageCount;
        LOG_DEBUG("Graph: Candidate %08x assigned to tier %d (covers %zu nodes)",
                  candidate.nodeId, candidate.tier, candidate.coverageCount);
    }

    LOG_DEBUG("Graph: Selected %zu relay candidates across %d tiers", relayCandidates.size(), currentTier + 1);
    return relayCandidates;
}

bool Graph::isGatewayNode(NodeNum nodeId, NodeNum sourceNode) const {
    // A node is a gateway if it connects to nodes that are not reachable from the source
    // through any other path (i.e., it bridges otherwise disconnected components)

    auto gatewayNeighbors = getDirectNeighbors(nodeId);
    auto sourceNeighbors = getDirectNeighbors(sourceNode);

    LOG_DEBUG("Graph: Checking if %08x is gateway for source %08x (%zu vs %zu neighbors)",
              nodeId, sourceNode, gatewayNeighbors.size(), sourceNeighbors.size());

    // Check if this node connects to any nodes that the source doesn't connect to
    // (excluding the gateway node itself)
    for (NodeNum neighbor : gatewayNeighbors) {
        if (neighbor == sourceNode) continue;  // Don't count direct connection to source

        // If source doesn't have this neighbor, gateway might be bridging
        if (sourceNeighbors.find(neighbor) == sourceNeighbors.end()) {
            // Additional check: see if this neighbor is connected to other nodes
            // that form a separate component
            auto neighborNeighbors = getDirectNeighbors(neighbor);
            bool hasOtherConnections = false;
            for (NodeNum nn : neighborNeighbors) {
                if (nn != nodeId && sourceNeighbors.find(nn) == sourceNeighbors.end()) {
                    hasOtherConnections = true;
                    break;
                }
            }
            if (hasOtherConnections) {
                LOG_DEBUG("Graph: Node %08x IS a gateway (bridges to %08x and separate component)", nodeId, neighbor);
                return true;  // This node bridges to a separate component
            }
        }
    }

    LOG_DEBUG("Graph: Node %08x is NOT a gateway", nodeId);
    return false;
}

bool Graph::shouldRelayEnhanced(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom,
                               uint32_t currentTime, uint32_t packetId) const {
    LOG_DEBUG("Graph: === Relay decision for node %08x, source %08x, heard from %08x, packet %08x ===",
              myNode, sourceNode, heardFrom, packetId);

    // Build set of nodes that have already "covered" (source + anyone who relayed)
    std::unordered_set<NodeNum> alreadyCovered;
    alreadyCovered.insert(sourceNode);

    // Add all nodes that the source can reach directly
    auto sourceNeighbors = getDirectNeighbors(sourceNode);
    for (NodeNum n : sourceNeighbors) {
        alreadyCovered.insert(n);
    }

    // If we heard from a relayer (not the source), add their coverage too
    if (heardFrom != sourceNode) {
        alreadyCovered.insert(heardFrom);
        auto relayerNeighbors = getDirectNeighbors(heardFrom);
        for (NodeNum n : relayerNeighbors) {
            alreadyCovered.insert(n);
        }
    }

    LOG_DEBUG("Graph: Already covered: %zu nodes", alreadyCovered.size());

    // Get all nodes that heard this packet (source's neighbors + relayer's neighbors)
    std::unordered_set<NodeNum> candidates;
    for (NodeNum n : sourceNeighbors) {
        candidates.insert(n);
    }
    if (heardFrom != sourceNode) {
        auto relayerNeighbors = getDirectNeighbors(heardFrom);
        for (NodeNum n : relayerNeighbors) {
            candidates.insert(n);
        }
    }

    LOG_DEBUG("Graph: Potential candidates: %zu nodes", candidates.size());

    // Get all relay candidates with their tiers
    auto relayCandidates = findAllRelayCandidates(alreadyCovered, candidates, currentTime, packetId);

    if (relayCandidates.empty()) {
        LOG_DEBUG("Graph: No relay candidates found - not relaying");
        return false;  // No one can provide additional coverage
    }

    // Find candidates in tier 0 (primary relays)
    std::vector<NodeNum> primaryRelays;
    for (const auto& candidate : relayCandidates) {
        if (candidate.tier == 0) {
            primaryRelays.push_back(candidate.nodeId);
        }
    }

    LOG_DEBUG("Graph: Found %zu primary relays", primaryRelays.size());

    // Check if we're a primary relay
    bool isPrimaryRelay = std::find(primaryRelays.begin(), primaryRelays.end(), myNode) != primaryRelays.end();
    if (isPrimaryRelay) {
        LOG_DEBUG("Graph: WE ARE PRIMARY RELAY - TRANSMITTING");
        return true;  // We're a primary relay - transmit
    }

    // If we're not a primary relay, check if we're a gateway node
    if (isGatewayNode(myNode, sourceNode)) {
        LOG_DEBUG("Graph: WE ARE GATEWAY NODE - TRANSMITTING");
        return true;  // We're a gateway bridging disconnected segments
    }

    // Check if any primary relays have transmitted
    bool primaryRelayHasTransmitted = false;
    for (NodeNum primary : primaryRelays) {
        if (hasNodeTransmitted(primary, packetId, currentTime)) {
            primaryRelayHasTransmitted = true;
            LOG_DEBUG("Graph: Primary relay %08x has transmitted", primary);
            break;
        }
    }

    // If no primary relay has transmitted and we're in tier 1 (backup), transmit
    if (!primaryRelayHasTransmitted) {
        for (const auto& candidate : relayCandidates) {
            if (candidate.tier == 1 && candidate.nodeId == myNode) {
                LOG_DEBUG("Graph: WE ARE BACKUP RELAY (tier 1) - TRANSMITTING");
                return true;  // We're the backup relay
            }
        }
    } else {
        LOG_DEBUG("Graph: Primary relay has transmitted, not checking backup status");
    }

    // Check if all our direct neighbors are already covered
    auto myNeighbors = getDirectNeighbors(myNode);
    bool allNeighborsCovered = true;
    for (NodeNum neighbor : myNeighbors) {
        if (alreadyCovered.find(neighbor) == alreadyCovered.end()) {
            allNeighborsCovered = false;
            break;
        }
    }

    LOG_DEBUG("Graph: Our %zu neighbors - %s covered", myNeighbors.size(),
              allNeighborsCovered ? "ALL" : "NOT ALL");

    // If all our neighbors are covered and we haven't transmitted yet, don't relay
    if (allNeighborsCovered && !hasNodeTransmitted(myNode, packetId, currentTime)) {
        LOG_DEBUG("Graph: All neighbors covered and we haven't transmitted - NOT relaying");
        return false;
    }

    // Final check: if we can provide coverage that no higher-tier relay can provide
    auto myCoverage = getCoverageIfRelays(myNode, alreadyCovered);
    if (!myCoverage.empty()) {
        LOG_DEBUG("Graph: We can cover %zu additional nodes", myCoverage.size());
        // Check if any higher-priority relay can cover these same nodes
        bool coveredByHigherTier = false;
        for (const auto& candidate : relayCandidates) {
            if (candidate.tier <= 1) {  // Check primary and backup relays
                auto theirCoverage = getCoverageIfRelays(candidate.nodeId, alreadyCovered);
                bool coversAll = true;
                for (NodeNum covered : myCoverage) {
                    if (theirCoverage.find(covered) == theirCoverage.end()) {
                        coversAll = false;
                        break;
                    }
                }
                if (coversAll) {
                    coveredByHigherTier = true;
                    LOG_DEBUG("Graph: Higher-tier relay %08x covers our nodes", candidate.nodeId);
                    break;
                }
            }
        }
        if (!coveredByHigherTier) {
            LOG_DEBUG("Graph: We provide unique coverage - TRANSMITTING");
            return true;  // We can cover nodes that higher-tier relays can't
        }
    } else {
        LOG_DEBUG("Graph: We provide no additional coverage");
    }

    LOG_DEBUG("Graph: No relay condition met - NOT relaying");
    return false;  // Don't relay
}
