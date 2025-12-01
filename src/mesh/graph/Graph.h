#pragma once
#include "NodeDB.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <limits>
#include <cstdint>

struct Edge {
    NodeNum from;
    NodeNum to;
    float etx; // Expected Transmission Count
    uint32_t lastUpdate; // Timestamp of last update
    float stability; // Stability weighting factor

    Edge(NodeNum f, NodeNum t, float e, uint32_t timestamp)
        : from(f), to(t), etx(e), lastUpdate(timestamp), stability(1.0f) {}
};

struct Route {
    NodeNum destination;
    NodeNum nextHop;
    float cost;
    uint32_t timestamp;

    Route() : destination(0), nextHop(0), cost(0), timestamp(0) {}
    Route(NodeNum dest, NodeNum hop, float c, uint32_t ts)
        : destination(dest), nextHop(hop), cost(c), timestamp(ts) {}
};

class Graph {
public:
    Graph();
    ~Graph();

    /**
     * Add or update an edge in the graph
     */
    void updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp);

    /**
     * Remove edges that haven't been updated in the last 300 seconds
     */
    void ageEdges(uint32_t currentTime);

    /**
     * Calculate route to destination using Dijkstra with ETX costs
     */
    Route calculateRoute(NodeNum destination, uint32_t currentTime);

    /**
     * Get cached route if still valid
     */
    Route getCachedRoute(NodeNum destination, uint32_t currentTime);

    /**
     * Clear all cached routes
     */
    void clearCache();

    /**
     * Calculate ETX from RSSI and SNR values
     */
    static float calculateETX(int32_t rssi, int32_t snr);

    /**
     * Update stability weighting for an edge
     */
    void updateStability(NodeNum from, NodeNum to, float newStability);

private:
    std::unordered_map<NodeNum, std::vector<Edge>> adjacencyList;
    std::unordered_map<NodeNum, Route> routeCache;
    static constexpr uint32_t ROUTE_CACHE_TIMEOUT_MS = 300 * 1000; // 300 seconds
    static constexpr uint32_t EDGE_AGING_TIMEOUT_MS = 300 * 1000; // 300 seconds

    /**
     * Dijkstra implementation for finding lowest cost path
     */
    Route dijkstra(NodeNum source, NodeNum destination, uint32_t currentTime);

    /**
     * Calculate weighted ETX cost including stability
     */
    float getWeightedCost(const Edge& edge, uint32_t currentTime);
};
