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

int Graph::updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance) {
    // Check if this is a new node
    bool isNewNode = (adjacencyList.find(from) == adjacencyList.end());
    NodeNum myNode = nodeDB->getNodeNum();

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
        // Check for significant change
        float oldEtx = it->etx;
        float change = std::abs(etx - oldEtx) / oldEtx;

        // Update existing edge
        it->etx = etx;
        it->lastUpdate = timestamp;
        it->variance = variance;

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
                return EDGE_SIGNIFICANT_CHANGE;
            }
            // New edge is worse than all existing, don't add
            return EDGE_NO_CHANGE;
        }

        // Add new edge
        Edge newEdge(from, to, etx, timestamp);
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
    Route route = dijkstra(nodeDB->getNodeNum(), destination, currentTime);

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
