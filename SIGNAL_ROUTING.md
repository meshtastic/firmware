# SignalRouting Algorithm

## Overview

SignalRouting (SR) is an advanced mesh networking protocol for Meshtastic that fundamentally transforms packet routing from traditional flooding-based approaches to intelligent, graph-based coordination. Unlike stock firmware's simple broadcast flooding or basic next-hop forwarding, SR maintains a comprehensive network topology graph using Expected Transmission Count (ETX) metrics to make optimal routing decisions.

### Key Differentiators from Stock Routing

**Stock Firmware Behavior:**
- **Broadcasts**: SNR-based flooding with duplicate suppression - nodes retransmit with random delays, but cancel retransmissions when hearing others transmit first
- **Unicasts**: Simple next-hop forwarding based on learned routes or direct connectivity
- **Coordination**: Limited duplicate detection prevents some redundancy, but still allows broadcast storms in dense networks

**SignalRouting Behavior:**
- **Broadcasts**: Intelligent relay coordination to prevent redundant transmissions while ensuring coverage
- **Unicasts**: Graph-based multi-hop routing with ETX optimization, plus coordinated relay selection through overhearing
- **Coordination**: Distributed algorithm where nodes compete for relay responsibilities based on network position

### SR's Dual Nature

SR operates in two complementary modes:

1. **Intelligent Routing**: Uses Dijkstra algorithm on ETX-weighted topology graphs for optimal path selection
2. **Coordinated Relay Selection**: For both broadcasts and unicasts, SR may broadcast packets to coordinate which nodes should relay them, preventing the redundant transmissions that plague traditional flooding approaches

This dual approach provides the reliability of coordinated networking with the efficiency of graph-based routing, significantly improving network performance over both random flooding and simple forwarding schemes.

## Comparison with Other Routing Approaches

### vs Stock Flooding (FloodingRouter)

**Stock Flooding Problems:**
- **Residual Redundancy**: Despite duplicate suppression, broadcast storms still occur in dense networks due to timing windows and SNR variations
- **Inefficiency**: Limited coordination means wasted transmissions when multiple nodes retransmit before suppression takes effect
- **Unpredictability**: SNR-based delays provide basic prioritization but don't account for overall network topology or coverage optimization
- **No Learning**: Static behavior regardless of network conditions

**SignalRouting Solution:**
- **Coordination**: Distributed algorithm determines optimal relay nodes based on coverage analysis
- **Topology Awareness**: Uses ETX metrics and graph knowledge to select best relay candidates
- **Iterative Selection**: Nodes compete for relay responsibilities with timeout-based fallback
- **Legacy Integration**: Prioritizes existing ROUTER/REPEATER nodes for compatibility

**Key Advantage**: Transforms chaotic flooding into orchestrated relay selection, significantly reducing redundant transmissions while maintaining reliable coverage.

### vs NextHopRouter

**NextHopRouter Limitations:**
- **Reactive Learning**: Only discovers routes through ACK observations, slow convergence
- **Single Path**: Learns one next-hop per destination, no alternative route awareness
- **No Coordination**: Each node forwards independently, potential for redundant unicast paths
- **Link Quality Ignorance**: Doesn't consider ETX or multi-hop path optimization

**SignalRouting Advantages:**
- **Proactive Topology**: Maintains complete network graph through multiple discovery mechanisms
- **ETX Optimization**: Uses Expected Transmission Count for true link quality assessment
- **Multi-hop Intelligence**: Dijkstra algorithm finds optimal paths across the entire network
- **Coordinated Unicasts**: Enables intelligent relay selection through overhearing unicast transmissions

**Key Advantage**: Transforms unicast routing from simple forwarding to intelligent, network-aware path selection with optional coordination for complex scenarios.

## Node Role Behavior Comparison

### Rebroadcast Behavior by Role

| Role | Stock Flooding | With SignalRouting |
|------|----------------|-------------------|
| **CLIENT_MUTE** | Never rebroadcasts | **Topology broadcasts only** - Announces direct neighbors to help active SR nodes (simplified: no graph maintenance, no complex topology inference) |
| **CLIENT** | Rebroadcasts, can cancel duplicates | Uses SR coordination for broadcasts |
| **ROUTER/ROUTER_LATE** | Always rebroadcasts, never cancels | **Priority relays** - SR gives them highest priority as they always rebroadcast |
| **ROUTER_CLIENT** | Rebroadcasts, can cancel duplicates | Uses SR coordination for broadcasts |
| **REPEATER** | Rebroadcasts, can cancel duplicates | **Priority relays** - SR gives them highest priority as they always rebroadcast |
| **CLIENT_BASE** | Special: acts as ROUTER for favorited nodes | Uses SR coordination for broadcasts |
| **TRACKER/SENSOR/TAK** | Rebroadcasts, can cancel duplicates | Treated as legacy, no SR participation |

**Legacy Node Priority:** SignalRouting prioritizes ROUTER and REPEATER roles because these nodes are configured to always rebroadcast packets. CLIENT_MUTE nodes participate minimally by broadcasting their direct neighbor topology to assist active SignalRouting nodes in making informed unicast routing decisions.

### Retransmission Behavior

| Router Type | Unicast Retransmissions | Notes |
|-------------|------------------------|-------|
| **ReliableRouter** | Up to 3 retransmissions | For want_ack packets only |
| **NextHopRouter** | 2 for intermediate hops, 3 for origin | Route reset on final failure |
| **SignalRouting** | Coordinates unicast relays on overhearing nodes | For SR-selected unicast routes |

### Routing Delays and Timing

| Router Type | Broadcast Delays | Unicast Timing |
|-------------|-----------------|---------------|
| **FloodingRouter** | SNR-based delays (poorer SNR = shorter delay) | Immediate send |
| **NextHopRouter** | SNR-based delays + next hop preference | iface->getRetransmissionMsec() timing |
| **SignalRouting** | Backup relay contention windows (1-2s based on LoRa preset) | ETX-based route selection + speculative retransmit |

## Core Concepts

### Expected Transmission Count (ETX)

ETX measures the expected number of transmissions needed to successfully deliver a packet over a wireless link. It's calculated from RSSI (Received Signal Strength Indicator) and SNR (Signal-to-Noise Ratio) measurements:

- **ETX = 1 / (Delivery Probability)**
- Lower ETX = Better link quality
- ETX of 1.0 = Perfect link (100% delivery probability)
- ETX > 1.0 = Lossy link requiring retransmissions

### Topology Graph

SignalRouting maintains a network topology graph where:
- **Nodes** = Mesh devices
- **Edges** = Wireless links with ETX weights
- **Sources** = Reported (measured by peer) vs Mirrored (estimated from our perspective)

#### Topology Discovery Mechanisms

SignalRouting discovers network topology through multiple channels:

1. **Direct Neighbor Detection**: When receiving packets directly with signal data (RSSI/SNR), immediate neighbor relationships are established with calculated ETX values

2. **Topology Broadcasts**: Nodes periodically broadcast their complete neighbor list, allowing comprehensive topology learning from other nodes' perspectives

3. **Mute Node Topology Sharing**: CLIENT_MUTE nodes broadcast their direct neighbor information to help active SignalRouting nodes discover network topology, even though mute nodes don't participate in packet relaying. Active nodes learn about mute node neighbors for discovery purposes but don't consider routing paths through mute nodes since they don't relay. CLIENT_MUTE nodes maintain their direct neighbor graph (add/remove expired connections) but use simplified topology tracking. Other inactive roles (TRACKER, SENSOR, TAK, etc.) do not participate in topology sharing.

4. **Relayed Packet Inference**: When receiving relayed packets, connectivity between the original sender and relay node is inferred, even without direct signal measurements

5. **Placeholder System**: Unknown relay nodes are tracked as placeholders until their real identities are discovered through direct contact or topology broadcasts

6. **Gateway Relationship Tracking**: Downstream relationships are learned when packets flow through relay nodes, enabling multi-hop route discovery

This multi-modal discovery ensures robust topology awareness even in challenging network conditions. Mute nodes contribute to network intelligence by sharing their local connectivity knowledge without consuming relay bandwidth.

## Unicast Routing

### Route Calculation Algorithm

SignalRouting calculates unicast routes and coordinates delivery for reliable packet transmission:

```cpp
NodeNum SignalRoutingModule::getNextHop(NodeNum destination, ...) {
    // 1. Check direct connection (1-hop route)
    const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
    for each direct neighbor:
        if (neighbor == destination) return destination; // Direct route

    // 2. Find multi-hop routes using Dijkstra (full mode) or simplified routing (lite mode)
    Route route = routingGraph->calculateRoute(destination, currentTime);
    if (route.nextHop != 0) {
        return route.nextHop; // Multi-hop route found
    }

    // 3. Fallback options: gateway routes, opportunistic forwarding
    return findBestFallbackOption(destination, ...);
}

// Special handling for relayed unicast packets to direct neighbors
bool shouldDeliverDirectToNeighbor(NodeNum destination, NodeNum heardFrom) {
    // If we received this as a relayed packet and destination is our direct neighbor,
    // deliver directly since the destination didn't hear the original transmission
    if (heardFrom != sourceNode && isDirectNeighbor(destination)) {
        // Check if better positioned neighbors exist before delivering directly
        if (!hasBetterPositionedNeighborForDirectDelivery(destination, ourRouteCost)) {
            return destination; // Deliver directly
        }
    }
    return 0; // Use normal routing
}
```

### Unicast Route Selection Priority

When deciding whether to use SR coordination for unicast packets:

1. **Any Route Check**: If we have ANY calculated route to the destination (regardless of cost), use SR coordination
2. **Topology Health**: Verify destination is known in the network topology
3. **Gateway Preferences**: Prefer routes through gateways we can reach directly
4. **Next Hop Capability**: Ensure next hop is SR-capable or legacy router
5. **Designated Gateway Check**: Defer to designated gateways when applicable
6. **Opportunistic Forwarding**: Use when topology is unhealthy or routes unavailable

### Speculative Retransmission

For unicast packets, SignalRouting coordinates relay decisions on overhearing nodes:
- Intermediate nodes that overhear unicast transmissions participate in relay coordination
- Determines optimal relay candidates based on coverage analysis and ETX metrics
- Prevents redundant transmissions through distributed candidate selection
- Originating node retransmission handled by standard Router mechanisms

**Unlike broadcast coordination**, unicast relay failures rely on end-to-end retransmission rather than immediate fallback to alternative relays. In multi-hop unicast scenarios, relay failures are only detected when the original sender doesn't receive an ACK from the final destination.

### Unicast Relay Coordination

SignalRouting fundamentally differs from stock routing in its approach to unicast packets. While stock firmware simply forwards unicasts to the calculated next hop, SR enables intelligent relay coordination through overhearing.

**How SR Coordinates Unicasts:**
When a unicast packet is transmitted, nodes that overhear the transmission (even if they can't decrypt the payload) can participate in relay coordination:

1. **Overhearing Mechanism**: Unicast packets are transmitted normally with unencrypted headers
2. **Intermediate Participation**: Non-destination nodes that overhear the unicast can run coordination logic
3. **Distributed Decision**: Each overhearing node independently calculates if it should relay to the destination
4. **Optimal Selection**: Best-positioned relays are chosen based on route quality and network topology

**When SR Enables Coordination:**
SR allows unicast coordination when:
- ANY route exists to the destination (regardless of cost)
- Network topology is healthy and destination is known
- Gateway preferences and designated gateway logic don't override
- Next hop is SR-capable or legacy router

This allows SR's coordinated delivery algorithm to select the best relay candidates even for challenging routes.

**Overhearing Node Optimization:**
When nodes overhear a unicast transmission, they check if the original transmitter has direct connectivity to the optimal next hop or destination. If so, the transmitter should have handled the transmission directly rather than relying on coordination. This prevents unnecessary relay coordination when direct paths exist.

```cpp
bool shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p) {
    // Complex logic including:
    // - Check if we have ANY route to destination (regardless of cost)
    // - Verify topology health for destination
    // - Apply gateway preferences and designated gateway logic
    // - Ensure next hop is SR-capable or legacy router
    // Returns true when SR coordination should be used for unicast relay
}
```

This conservative approach prevents network flooding by only coordinating delivery for destinations that are already known in the network topology. Unknown nodes are discovered through multiple mechanisms:

- **Broadcast topology announcements**: Nodes periodically broadcast their neighbor information
- **Direct packet reception**: When receiving packets directly from unknown nodes with signal data
- **Relayed packet inference**: Inferring connectivity between senders and relays from relayed packets
- **Placeholder resolution**: Unknown relays are tracked as placeholders until real node identities are discovered

Unicast coordination can occur once a destination is known through any of these discovery methods. Additionally, unicast relay coordination includes optimizations to prevent unnecessary transmissions:

- **Direct Connectivity Optimization**: If the broadcasting node (that initiated coordination) has direct connectivity to the optimal next hop or destination, other nodes won't relay since the broadcasting node should have handled the transmission directly
- **Sender-Destination Direct Check**: If the original sender has a direct connection to the destination, no coordination occurs as the destination already received the packet directly

## Broadcast Routing

### Iterative Relay Coordination Algorithm

SignalRouting uses an iterative algorithm to coordinate broadcast relays, ensuring coverage while minimizing redundancy. The algorithm prioritizes legacy routers/repeaters and uses timeout-based candidate selection. **Note**: This iterative approach is used only for broadcast coordination, not for unicast relay coordination.

```cpp
bool Graph::shouldRelayEnhanced(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime, uint32_t packetId) {
    // Start with all nodes that can hear the transmitter
    std::unordered_set<NodeNum> candidates = getNodesThatHeardTransmitter(heardFrom);

    // Iterative loop: keep trying candidates until we decide to relay or run out of candidates
    while (!candidates.empty()) {
        // Find best candidate from current candidate list (prioritizing legacy routers)
        RelayCandidate bestCandidate = findBestRelayCandidate(candidates, alreadyCovered, currentTime, packetId);

        if (bestCandidate.nodeId == myNode) {
            return true; // We're the best candidate - relay immediately
        }

        // Wait for best candidate to relay within contention window
        if (hasNodeTransmitted(bestCandidate.nodeId, packetId, currentTime)) {
            // Best candidate relayed - check if we have unique coverage
            if (hasUniqueCoverage(myNode, bestCandidate.nodeId, alreadyCovered)) {
                return true; // Relay for unique coverage
            }
            // Remove best candidate and try next best
            candidates.erase(bestCandidate.nodeId);
        } else {
            // Best candidate hasn't relayed yet - wait or timeout
            return false; // Wait for best candidate
        }
    }

    // No candidates left - check if we have uncovered neighbors
    if (hasUncoveredNeighbors(myNode, alreadyCovered)) {
        return true; // Relay to reach uncovered neighbors
    }

    return false; // Don't relay
}
```

### Relay Decision Factors

1. **Candidate Selection**: Only nodes that can hear the transmitter are considered
2. **Legacy Priority**: Legacy routers/repeaters are prioritized as they are designed to always rebroadcast
3. **Iterative Refinement**: Candidate list shrinks with each non-relaying node, ensuring optimal selection
4. **Unique Coverage**: After other nodes relay, check if we cover areas they don't reach
5. **Timeout Handling**: Non-relaying candidates are excluded, forcing re-calculation with remaining nodes
6. **Fallback Logic**: Relay if no candidates remain but neighbors haven't been covered

### Broadcast Coordination Example

```
Network: A ── B ── C
           │    │
           D ── E

Packet from A to BROADCAST:

1. A transmits first
2. B, D, and E hear it (assuming they can all hear A)
3. Candidate list: [B, D, E]
4. Find best candidate: assume B provides most coverage (C,E)
5. B is not best candidate - wait for best candidate (B) to relay
6. B relays, covering C and E
7. After B relays, check for unique coverage:
   - D: covers E (already covered by B) - no unique coverage
   - E: covers no additional nodes - no unique coverage
8. No nodes have unique coverage - no additional relays needed
9. Result: Full coverage with 1 relay
```

**With Legacy Router Priority:**

```
Network: A ── B(legacy) ── C
           │        │
           D ─────── E

Packet from A to BROADCAST:

1. A transmits first
2. B (legacy router), D, and E hear it
3. Candidate list: [B, D, E]
4. Find best candidate: B prioritized as legacy router
5. B relays immediately (legacy behavior)
6. After B relays, check for unique coverage:
   - D: covers areas not reached by B - may relay if unique coverage exists
   - E: covers areas not reached by B - may relay if unique coverage exists
7. Result: Legacy router relays first, then additional relays only for unique coverage
```

## Benefits for Mesh Network Reliability

### Improved Deliverability

**Dense Node Scenarios:**
SignalRouting transforms chaotic broadcast flooding into orchestrated relay selection. In dense networks where multiple nodes hear the same transmissions, SR's iterative coordination algorithm identifies optimal relay nodes based on coverage analysis and ETX metrics. This significantly reduces redundant transmissions compared to traditional SNR-based flooding, while maintaining reliable delivery. Coordination effectiveness depends on topology accuracy, with graceful fallback to contention-based approaches when graph information is incomplete.

### Mesh Branch Handling

**Gateway-Aware Routing:**
```
Network Gateway
     │
     G (Stock/Legacy Node)
    / \
   /   \
  A ── B
  │    │
  C ── D

SignalRouting considerations:
1. Identifies legacy nodes (G) and gives them priority in relay decisions
2. Routes unicasts through gateways when direct routes aren't available
3. Allows broadcasts from legacy gateways to be relayed by SignalRouting nodes for branch coverage
4. Attempts to minimize redundant relays within branches, but legacy nodes may still flood independently
```

### Reliability Improvements

**Link Quality Awareness:**
- Uses ETX metrics to prefer higher quality links for unicast routing
- Routes around known poor quality links when better alternatives exist
- May adapt to changing conditions through periodic topology updates

**Failure Recovery:**
- Speculative retransmission provides basic recovery for unicast packet loss
- Falls back to contention-based flooding when topology information is incomplete
- Limited adaptation to sudden link failures without immediate topology updates

**Congestion Control:**
- Attempts to reduce redundant broadcasts through coordination
- Uses timeout-based relay selection to allow backup relays when primary relays fail
- Coordination effectiveness depends on network topology knowledge and node participation

## Graph vs GraphLite Implementation

### Graph (Full Mode)

**Memory Structure:**
```cpp
class Graph {
    std::unordered_map<NodeNum, std::vector<Edge>> adjacencyList;
    // Dynamic allocation, hash table overhead
    // ~64 bytes per node + ~32 bytes per edge
};
```

**Features:**
- Full Dijkstra algorithm for route calculation
- Enhanced relay coordination with coverage analysis
- Dynamic memory allocation (grows with network)
- Support for 100+ nodes in large networks

**Hardware Requirements:**
- **RAM**: 25-35KB for graph storage (100 nodes, 6 edges each)
- **Flash**: ~15KB additional code space
- **CPU**: Higher processing for complex algorithms
- **Best for**: ESP32, larger microcontrollers

### GraphLite (Lite Mode)

**Memory Structure:**
```cpp
class GraphLite {
    static constexpr size_t MAX_NODES = 100;
    static constexpr size_t MAX_EDGES_PER_NODE = 8;
    NodeEdgesLite nodes[MAX_NODES]; // Fixed-size arrays
};
```

**Features:**
- Simplified routing (direct + 1-hop-through-neighbor)
- Basic relay coordination with contention windows
- Fixed memory footprint, no dynamic allocation
- Optimized for memory-constrained devices

**Hardware Requirements:**
- **RAM**: ~8-12KB fixed usage (predictable memory)
- **Flash**: ~10KB additional code space
- **CPU**: Lower processing requirements
- **Best for**: ESP32-C3, RP2040, STM32WL, constrained devices

### Key Differences

| Aspect | Graph (Full) | GraphLite (Lite) |
|--------|-------------|------------------|
| **Route Length** | Multi-hop (full Dijkstra) | 1-2 hops only |
| **Relay Coordination** | Advanced coverage analysis | Simple contention windows |
| **Memory Usage** | Variable (grows with network) | Fixed (~8-12KB) |
| **Node Capacity** | 100+ nodes | ~50-60 nodes practical |
| **CPU Usage** | Higher (complex algorithms) | Lower (simplified logic) |
| **Memory Safety** | Dynamic allocation risks | Fixed arrays (safer) |
| **Scalability** | Better for large networks | Optimized for small networks |

### Automatic Mode Selection

```cpp
#ifdef SIGNAL_ROUTING_LITE_MODE
    routingGraph = new GraphLite();  // Always lite mode
#else
    routingGraph = new Graph();       // Full mode by default
#endif
```

### Timing and Synchronization

SignalRouting uses **monotonic time** (time since boot) for all graph operations to ensure consistency:

- **Graph aging**: Edge expiration and node cleanup
- **Route caching**: Cache validity and expiration
- **Transmission tracking**: Contention window timing
- **Edge timestamps**: Connection update times

This prevents issues with RTC updates that could cause time jumps, ensuring reliable graph maintenance and routing decisions.

**Automatic Hardware Adaptation:**
- **STM32WL**: Disabled entirely (64KB RAM limit)
- **RP2040**: RAM guard (< 30KB free disables SR)
- **ESP32-C3**: Uses GraphLite automatically
- **ESP32/others**: Uses full Graph mode

## Configuration and Tuning

### Key Parameters

```cpp
// Broadcast interval for topology updates
#define SIGNAL_ROUTING_BROADCAST_SECS 120

// Maximum neighbors tracked per node
#define MAX_SIGNAL_ROUTING_NEIGHBORS 14

// Speculative retransmission timeout
#define SPECULATIVE_RETRANSMIT_TIMEOUT_MS 600
```

### Topology Health Assessment

SignalRouting only activates when network has sufficient capable nodes:

```cpp
// For broadcast: requires at least 1 direct SR-capable neighbor
bool topologyHealthy = capableNeighbors >= 1;

// For unicast: requires knowledge of destination node
bool topologyHealthy = nodeDB->getMeshNode(destination) != nullptr;
```

## Real-World Examples

### Urban Mesh Network
```
Coffee Shop ── Street Node ── Park Node
     │             │             │
     └──── Apartment ──── Office ─────
                    │
               Subway Station (Gateway)
```

**SignalRouting Benefits:**
- Coordinates broadcasts to minimize redundant transmissions
- Routes unicasts through optimal paths based on link quality
- Uses gateway relationships for extended network reach
- Adapts routing when network topology changes

### Emergency Response Network
```
Command Post (Gateway)
        │
    ┌───┴───┐
Mobile Unit  Ambulance  Fire Truck
    │          │          │
    └──────┬───┴───┬──────┘
           │
       Incident Site
```

**SignalRouting Benefits:**
- Coordinated broadcast routing ensures full coverage
- ETX-based unicast routing prioritizes reliable communication paths
- Reduces redundant transmissions during critical operations
- Gateway awareness for command centers bridging network segments

### Rural Mesh with Branches
```
Main Road
    │
Farm House ── Barn ── Equipment Shed
    │                    │
    └───────── Grain Silo ──────┐
                                │
                     Network Gateway (Town)
```

**SignalRouting Benefits:**
- Efficient broadcast coverage across multiple structures
- Routes through intermediate nodes for better connectivity
- Gateway integration for external network access
- Topology-aware routing adapts to physical layout

## Algorithm Details

### Iterative Relay Selection

SignalRouting uses an iterative algorithm for relay coordination:

1. **Initial Candidate Selection**: All nodes that can hear the transmitter
2. **Best Candidate Identification**: Select node with best coverage/cost ratio (prioritizing legacy routers)
3. **Immediate Relay**: If selected node relays immediately
4. **Wait/Timeout**: Wait for best candidate to relay within contention window
5. **Unique Coverage Check**: After relay, other nodes check if they cover additional areas
6. **Candidate Exclusion**: Remove non-relaying candidates and repeat with remaining nodes
7. **Fallback**: If no candidates remain, relay if neighbors haven't been covered

This iterative approach ensures optimal relay selection while handling timeouts and network dynamics.

### Unicast Relay Logic

SignalRouting implements sophisticated unicast relay coordination to optimize packet delivery:

1. **Gateway Override**: If we are the designated gateway for the destination, always relay to ensure downstream connectivity
2. **Transmitter Direct Connectivity**: If the original transmitter has direct connectivity to our calculated next hop or the destination, don't relay - the transmitter should have used that direct path instead of relying on coordination
3. **Sender Direct Connectivity**: If the original sender has direct connection to the destination, don't relay (destination already received directly)
4. **Downstream Relay Coordination**: If destination is downstream of relays we can hear, coordinate delivery through broadcasting
5. **Legacy Router Priority**: Give priority to legacy routers/repeaters that can reach the destination (they are designed to always relay)
6. **Route Cost Comparison**: Compare our route quality against other nodes that heard the transmission to determine best relay positioning
7. **Better Direct Connection Check**: If another node has a significantly better direct connection to the destination, defer to them

**Note**: Unlike broadcast coordination, unicast relay decisions are made statically based on current topology knowledge without iterative candidate selection or contention window waiting.

```cpp
bool shouldRelayUnicastForCoordination(const meshtastic_MeshPacket *p) {
    // Complex coordination logic including all the above checks
    // Determines if this node should relay the coordinated unicast packet
}
```

### Performance Characteristics

**Broadcast Coordination:**
- Uses iterative candidate selection with contention windows and timeout-based fallback
- Attempts to minimize redundant transmissions through coverage analysis
- May still have some redundant relays in dynamic network conditions
- Effectiveness depends on topology knowledge and node participation

**Unicast Coordination:**
- Uses static relay decisions based on current topology knowledge
- Does not implement iterative candidate selection or contention windows
- Enables intelligent relay selection through overhearing unicast transmissions
- Includes transmitter direct connectivity optimization to prevent unnecessary coordination
- Relies on speculative retransmission (600ms timeout) for relay failure recovery
- Falls back to opportunistic forwarding when routes are unknown

**Key Difference**: Broadcasts use dynamic, iterative coordination with immediate fallback; unicasts use static decisions with end-to-end retransmission recovery.

**Network Adaptation:**
- Assesses topology health but may not detect sudden changes immediately
- Degrades to contention-based approaches when coordination isn't possible
- Works with mixed legacy/SignalRouting networks but coordination is limited by legacy node behavior

## Implementation Notes

### Fallback Mechanisms

SignalRouting gracefully degrades when coordination isn't possible:

1. **Unknown Destinations**: Does not broadcast unicast packets for unknown nodes to prevent flooding (discovery occurs via any received packet)
2. **Topology Incomplete**: Uses traditional unicast routing for known but poorly connected destinations
3. **Legacy Node Priority**: Gives priority to legacy routers/repeaters for compatibility
4. **Memory/CPU Constraints**: Automatic feature disabling for constrained devices

### Compatibility

- **Backward Compatible**: Works with all existing Meshtastic nodes
- **Progressive Enhancement**: Benefits increase with more SR-capable nodes
- **Mixed Networks**: Automatically detects and adapts to legacy nodes including CLIENT_MUTE topology sharing
- **Gateway Integration**: Special handling for nodes bridging network segments
- **Mute Node Intelligence**: CLIENT_MUTE nodes contribute topology information without relay participation

## Future Considerations

Potential areas for improvement:

1. **Topology Update Optimization**: Reduce broadcast frequency while maintaining accuracy
2. **Legacy Node Coordination**: Better integration with existing router behaviors
3. **Dynamic Timeout Adjustment**: Adapt contention windows based on network conditions
4. **Route Caching Improvements**: Better handling of topology changes
5. **Memory Optimization**: Further reduce RAM usage for constrained devices

## Troubleshooting

### Common Issues

**"SR disabled - insufficient RAM"**
- Device has <30KB free RAM
- Switch to GraphLite mode or upgrade hardware

**"Topology unhealthy - flooding only"**
- Too few SR-capable nodes or incomplete topology information
- Add more SignalRouting-capable devices or wait for topology convergence

**"No route found for unicast"**
- Destination not in topology graph
- Wait for topology convergence or use opportunistic forwarding

**High CPU usage**
- Large network with Graph mode
- Consider switching to GraphLite or reducing network size

**"Packet not relayed despite good coverage"**
- Iterative algorithm may have excluded the node or found better candidates
- Legacy nodes may have priority and relayed instead
- Unique coverage requirements may not be met

**"Unexpected relays from legacy nodes"**
- Legacy routers/repeaters are prioritized and may relay independently
- This is expected behavior for compatibility with existing infrastructure

**"Coordinated unicast relays being used"**
- SignalRouting only uses coordinated relay selection for unicast packets to known destinations with good routes
- Unknown destinations use traditional unicast routing to prevent network flooding

SignalRouting provides an alternative to traditional flooding-based mesh networking, offering basic coordination for packet relay decisions. It works alongside existing routing approaches and provides benefits in networks with sufficient SignalRouting-capable nodes, while maintaining compatibility with legacy devices through prioritized relay handling.
