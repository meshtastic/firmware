#include "Graph.h"
#include "RTC.h"
#include <algorithm>
#include <cmath>

Graph::Graph() {}

Graph::~Graph() {}

int Graph::updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance) {
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

float Graph::calculateETX(int32_t rssi, int32_t snr) {
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

    // Factor in SNR
    if (snr < 5) {
        deliveryProb *= 0.5f;
    } else if (snr < 10) {
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
