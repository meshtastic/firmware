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
#include "Graph.h"  // For shared constants
#include <cstdint>
#include <limits>

// Compile-time configuration for constrained devices
#ifndef GRAPH_LITE_MAX_NODES
#define GRAPH_LITE_MAX_NODES 120 // Maximum nodes in graph
#endif

#ifndef GRAPH_LITE_MAX_EDGES_PER_NODE
#define GRAPH_LITE_MAX_EDGES_PER_NODE 12 // Maximum neighbors per node
#endif

#ifndef GRAPH_LITE_MAX_RELAY_STATES
#define GRAPH_LITE_MAX_RELAY_STATES 16 // Track recent transmissions
#endif

#ifndef GRAPH_LITE_MAX_CACHED_ROUTES
#define GRAPH_LITE_MAX_CACHED_ROUTES 32 // Maximum cached routes
#endif

#ifndef GRAPH_LITE_MAX_RELAY_TIERS
#define GRAPH_LITE_MAX_RELAY_TIERS 3 // Maximum relay tiers for coordination
#endif

struct EdgeLite {
    enum class Source : uint8_t { Mirrored = 0, Reported = 1 };

    NodeNum to;
    uint16_t etxFixed;     // ETX * 100 (fixed-point, range 1.00-655.35)
    uint32_t lastUpdate;   // Full timestamp (seconds since boot)
    uint8_t variance;      // Position variance (0-255, scaled)
    uint8_t stability;     // Stability * 100 (1.0 = 100, lower = less stable)
    Source source;

    EdgeLite() : to(0), etxFixed(100), lastUpdate(0), variance(0), stability(100), source(Source::Mirrored) {}

    float getEtx() const { return etxFixed / 100.0f; }
    void setEtx(float etx) { etxFixed = static_cast<uint16_t>(etx * 100.0f); }

    float getStability() const { return stability / 100.0f; }
    void setStability(float s) { stability = static_cast<uint8_t>(s * 100.0f); }
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

struct RelayCandidateLite {
    NodeNum nodeId;
    uint8_t coverageCount; // Number of new nodes covered
    uint16_t avgCostFixed; // Average cost to reach covered nodes * 100
    uint8_t tier;          // 0 = primary, 1 = backup, etc.

    RelayCandidateLite() : nodeId(0), coverageCount(0), avgCostFixed(0), tier(0) {}
    RelayCandidateLite(NodeNum node, uint8_t coverage, uint16_t cost, uint8_t t)
        : nodeId(node), coverageCount(coverage), avgCostFixed(cost), tier(t) {}

    float getAvgCost() const { return avgCostFixed / 100.0f; }

    // Sort by tier first (lower is better), then coverage (higher is better), then cost (lower is better)
    bool operator<(const RelayCandidateLite& other) const {
        if (tier != other.tier) return tier < other.tier;
        if (coverageCount != other.coverageCount) return coverageCount > other.coverageCount;
        return avgCostFixed < other.avgCostFixed;
    }
};

struct RelayStateLite {
    NodeNum nodeId;
    uint32_t packetId;
    uint16_t timestampLo; // Lower 16 bits

    RelayStateLite() : nodeId(0), packetId(0), timestampLo(0) {}
};

class GraphLite {
  public:
    static uint32_t getContentionWindowMs();
    static constexpr uint32_t EDGE_AGING_TIMEOUT_SECS = 600; // 10 minutes for GraphLite (more conservative)

    GraphLite();

    /**
     * Add or update an edge in the graph
     */
    int updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance = 0,
                   EdgeLite::Source source = EdgeLite::Source::Mirrored, bool updateTimestamp = true);

    /**
     * Update node activity timestamp (keeps node in graph without edges)
     */
    void updateNodeActivity(NodeNum nodeId, uint32_t timestamp);

    /**
     * Remove edges that haven't been updated recently
     */
    void ageEdges(uint32_t currentTimeSecs);

    /**
     * Calculate route to destination (simplified Dijkstra)
     * @param nodeFilter Optional function to filter which nodes can be used as intermediate hops (returns true to allow)
     */
    RouteLite calculateRoute(NodeNum destination, uint32_t currentTime, std::function<bool(NodeNum)> nodeFilter = nullptr);

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
     * Conservative relay decision that defers to stock gateways
     */
    bool shouldRelaySimpleConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const;

    /**
     * Enhanced relay decision with coverage analysis and contention window support
     */
    bool shouldRelayEnhanced(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime, uint32_t packetId, uint32_t packetRxTime = 0) const;

    /**
     * Conservative version of shouldRelayEnhanced that defers to stock gateways
     */
    bool shouldRelayEnhancedConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime, uint32_t packetId, uint32_t packetRxTime = 0) const;

    /**
     * Calculate which nodes would be covered if a specific relay rebroadcasts
     */
    size_t getCoverageIfRelays(NodeNum relay, NodeNum *coveredNodes, size_t maxNodes, const NodeNum *alreadyCovered, size_t alreadyCoveredCount) const;

    /**
     * Find the best relay node to cover uncovered nodes
     */
    NodeNum findBestRelay(const NodeNum *alreadyCovered, size_t alreadyCoveredCount,
                         const NodeNum *candidates, size_t candidateCount, uint32_t currentTime) const;

    /**
     * Relay decision with basic contention window support for SR nodes
     */
    bool shouldRelayWithContention(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t packetId, uint32_t currentTime) const;

    /**
     * Record that a node has transmitted
     */
    void recordNodeTransmission(NodeNum nodeId, uint32_t packetId, uint32_t currentTime);

    /**
     * Check if a node has transmitted recently
     */
    bool hasNodeTransmitted(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) const;

    /**
     * Find the best relay candidate from a set of potential candidates
     */
    RelayCandidateLite findBestRelayCandidate(const std::unordered_set<NodeNum>& candidates,
                                             const std::unordered_set<NodeNum>& alreadyCovered,
                                             uint32_t currentTime, uint32_t packetId) const;

    /**
     * Check if a node is a gateway node for a given source
     */
    bool isGatewayNode(NodeNum nodeId, NodeNum sourceNode) const;

    /**
     * Get count of nodes in graph
     */
    size_t getNodeCount() const { return nodeCount; }

    /**
     * Get cached route if still valid
     */
    RouteLite getCachedRoute(NodeNum destination, uint32_t currentTime);

    /**
     * Clear all cached routes
     */
    void clearCache();

    /**
     * Get all node IDs (fills provided array, returns count)
     */
    size_t getAllNodeIds(NodeNum *outArray, size_t maxCount) const;

    /**
     * Remove a node and all its edges from the graph
     * @param nodeId Node to remove
     */
    void removeNode(NodeNum nodeId);

    /**
     * Update stability weighting for an edge
     */
    void updateStability(NodeNum from, NodeNum to, float newStability);

    /**
     * Clear all edges to/from a specific node (used for graph merging)
     * @param nodeId Node whose edges to clear
     */
    void clearEdgesForNode(NodeNum nodeId);

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

    RouteLite routeCache[GRAPH_LITE_MAX_CACHED_ROUTES];
    uint8_t routeCacheCount;
    static constexpr uint32_t ROUTE_CACHE_TIMEOUT_SECS = 300;

    // Find or create node entry (returns nullptr if full)
    NodeEdgesLite *findOrCreateNode(NodeNum nodeId);

    // Find node entry (returns nullptr if not found)
    NodeEdgesLite *findNode(NodeNum nodeId);
    const NodeEdgesLite *findNode(NodeNum nodeId) const;

    // Find edge in node (returns nullptr if not found)
    EdgeLite *findEdge(NodeEdgesLite *node, NodeNum to);
    const EdgeLite *findEdge(const NodeEdgesLite *node, NodeNum to) const;
};

