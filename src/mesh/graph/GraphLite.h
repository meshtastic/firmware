#pragma once
/**
 * GraphLite - Memory-efficient graph implementation for constrained devices
 *
 * Uses fixed-size arrays instead of std::unordered_map to:
 * - Eliminate heap fragmentation
 * - Reduce memory overhead from hash table buckets
 * - Provide predictable memory usage
 *
 * Trade-offs:
 * - O(n) lookups instead of O(1) - acceptable for small networks
 * - Fixed maximum capacity - configurable via compile-time constants
 */

#include "NodeDB.h"
#include <cstdint>
#include <limits>

// Compile-time configuration for constrained devices
#ifndef GRAPH_LITE_MAX_NODES
#define GRAPH_LITE_MAX_NODES 16 // Maximum nodes in graph
#endif

#ifndef GRAPH_LITE_MAX_EDGES_PER_NODE
#define GRAPH_LITE_MAX_EDGES_PER_NODE 6 // Maximum neighbors per node
#endif

#ifndef GRAPH_LITE_MAX_RELAY_STATES
#define GRAPH_LITE_MAX_RELAY_STATES 8 // Track recent transmissions
#endif

struct EdgeLite {
    enum class Source : uint8_t { Mirrored = 0, Reported = 1 };

    NodeNum to;
    uint16_t etxFixed;     // ETX * 100 (fixed-point, range 1.00-655.35)
    uint16_t lastUpdateLo; // Lower 16 bits of timestamp (wraps every ~65s)
    uint8_t variance;      // Position variance (0-255, scaled)
    Source source;

    EdgeLite() : to(0), etxFixed(100), lastUpdateLo(0), variance(0), source(Source::Mirrored) {}

    float getEtx() const { return etxFixed / 100.0f; }
    void setEtx(float etx) { etxFixed = static_cast<uint16_t>(etx * 100.0f); }
};

struct NodeEdgesLite {
    NodeNum nodeId;
    EdgeLite edges[GRAPH_LITE_MAX_EDGES_PER_NODE];
    uint8_t edgeCount;
    uint32_t lastFullUpdate; // Full timestamp for aging

    NodeEdgesLite() : nodeId(0), edgeCount(0), lastFullUpdate(0) {}
};

struct RouteLite {
    NodeNum destination;
    NodeNum nextHop;
    uint16_t costFixed; // Cost * 100 (fixed-point)
    uint32_t timestamp;

    RouteLite() : destination(0), nextHop(0), costFixed(0), timestamp(0) {}

    float getCost() const { return costFixed / 100.0f; }
};

struct RelayStateLite {
    NodeNum nodeId;
    uint32_t packetId;
    uint16_t timestampLo; // Lower 16 bits

    RelayStateLite() : nodeId(0), packetId(0), timestampLo(0) {}
};

class GraphLite {
  public:
    // Return values for updateEdge()
    static constexpr int EDGE_NO_CHANGE = 0;
    static constexpr int EDGE_NEW = 1;
    static constexpr int EDGE_SIGNIFICANT_CHANGE = 2;

    static constexpr float ETX_CHANGE_THRESHOLD = 0.20f;
    static constexpr uint32_t CONTENTION_WINDOW_MS = 200;
    static constexpr uint32_t EDGE_AGING_TIMEOUT_SECS = 300;

    GraphLite();

    /**
     * Add or update an edge in the graph
     */
    int updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance = 0,
                   EdgeLite::Source source = EdgeLite::Source::Mirrored);

    /**
     * Remove edges that haven't been updated recently
     */
    void ageEdges(uint32_t currentTimeSecs);

    /**
     * Calculate route to destination (simplified Dijkstra)
     */
    RouteLite calculateRoute(NodeNum destination, uint32_t currentTime);

    /**
     * Calculate ETX from RSSI and SNR values
     */
    static float calculateETX(int32_t rssi, float snr);

    /**
     * Reverse calculate RSSI and SNR from ETX (approximate)
     */
    static void etxToSignal(float etx, int32_t &rssi, int32_t &snr);

    /**
     * Get edges from a node (returns nullptr if not found)
     */
    const NodeEdgesLite *getEdgesFrom(NodeNum node) const;

    /**
     * Get number of direct neighbors for a node
     */
    uint8_t getNeighborCount(NodeNum node) const;

    /**
     * Check if we should relay a broadcast (simplified algorithm)
     */
    bool shouldRelaySimple(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const;

    /**
     * Record that a node has transmitted
     */
    void recordNodeTransmission(NodeNum nodeId, uint32_t packetId, uint32_t currentTime);

    /**
     * Check if a node has transmitted recently
     */
    bool hasNodeTransmitted(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) const;

    /**
     * Simplified relay decision for constrained devices
     */
    bool shouldRelaySimple(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const;

    /**
     * Conservative relay decision that defers to stock gateways
     */
    bool shouldRelaySimpleConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const;

    /**
     * Get count of nodes in graph
     */
    size_t getNodeCount() const { return nodeCount; }

    /**
     * Get all node IDs (fills provided array, returns count)
     */
    size_t getAllNodeIds(NodeNum *outArray, size_t maxCount) const;

    /**
     * Get memory usage estimate in bytes
     */
    static constexpr size_t getMemoryUsage() {
        return sizeof(GraphLite);
    }

  private:
    NodeEdgesLite nodes[GRAPH_LITE_MAX_NODES];
    uint8_t nodeCount;

    RelayStateLite relayStates[GRAPH_LITE_MAX_RELAY_STATES];
    uint8_t relayStateCount;

    RouteLite routeCache;
    uint32_t routeCacheTime;
    static constexpr uint32_t ROUTE_CACHE_TIMEOUT_SECS = 60;

    // Find or create node entry (returns nullptr if full)
    NodeEdgesLite *findOrCreateNode(NodeNum nodeId);

    // Find node entry (returns nullptr if not found)
    NodeEdgesLite *findNode(NodeNum nodeId);
    const NodeEdgesLite *findNode(NodeNum nodeId) const;

    // Find edge in node (returns nullptr if not found)
    EdgeLite *findEdge(NodeEdgesLite *node, NodeNum to);
    const EdgeLite *findEdge(const NodeEdgesLite *node, NodeNum to) const;
};

