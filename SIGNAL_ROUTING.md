# SignalRouting Algorithm

## Overview

SignalRouting is an advanced mesh networking protocol for Meshtastic that replaces traditional SNR-based flooding delays with graph-based topology awareness and coordinated packet forwarding. It uses Expected Transmission Count (ETX) metrics to make intelligent routing decisions, significantly improving network efficiency and reliability over both random and SNR-based flooding approaches.

## Comparison with Other Routing Approaches

### vs Stock Flooding (FloodingRouter)
- **Flooding**: Every node rebroadcasts received packets with SNR-based delays (poorer links = shorter delays)
- **SignalRouting**: Coordinates relays to prevent redundant transmissions while ensuring coverage
- **Advantage**: Reduces broadcast storms and provides more reliable delivery than SNR-based prioritization

### vs NextHopRouter
- **NextHopRouter**: Learns single next hop per destination reactively through ACK observations
- **SignalRouting**: Uses graph-based multi-hop routing with ETX quality metrics
- **Advantage**: More reliable routes and coordinated broadcast behavior

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
| **SignalRouting** | Adds speculative retransmit (600ms timeout) | For SR-selected unicast routes |

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

1. **Direct sender coverage check**: If the sending node has direct connection to destination, don't relay (destination already received)
2. **Direct delivery to neighbors**: For relayed packets where destination didn't hear original transmission
3. **Calculated multi-hop routes**: Using Dijkstra algorithm with ETX weights
4. **Gateway routes**: Prefer direct connections to known gateways for extended reach
5. **Opportunistic forwarding**: When topology incomplete, forward to best direct neighbor
6. **Broadcast coordination**: Only for known, well-connected destinations to prevent flooding

### Speculative Retransmission

For unicast packets, SignalRouting implements speculative retransmission:
- Monitors unicast packet transmissions for ACK responses
- Retransmits after 600ms timeout if no ACK received
- Helps recover from temporary link failures or interference

### Unicast Broadcast Coordination

SignalRouting broadcasts unicast packets only when the destination is known and well-connected:

```cpp
bool shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p) {
    // Only broadcast unicast packets for known destinations
    bool topologyHealthy = topologyHealthyForUnicast(p->to);
    if (!topologyHealthy) {
        // Don't broadcast unicasts for unknown destinations to prevent flooding
        return false;
    }

    // Check if we have a good route to the known destination
    Route route = routingGraph->calculateRoute(p->to, getTime());
    if (route.nextHop != 0 && route.cost < 5.0f) {
        // Broadcast unicast packet for relay coordination
        return true;
    }
    return false; // Use traditional unicast routing
}
```

This conservative approach prevents network flooding by only coordinating delivery for destinations that are already known in the network topology. Unknown nodes are discovered through multiple mechanisms:

- **Broadcast topology announcements**: Nodes periodically broadcast their neighbor information
- **Direct packet reception**: When receiving packets directly from unknown nodes with signal data
- **Relayed packet inference**: Inferring connectivity between senders and relays from relayed packets
- **Placeholder resolution**: Unknown relays are tracked as placeholders until real node identities are discovered

Unicast coordination can occur once a destination is known through any of these discovery methods. Additionally, unicast packets are not relayed if the sending node has a direct connection to the destination, as the destination should have already received the packet directly.

## Broadcast Routing

### Iterative Relay Coordination Algorithm

SignalRouting uses an iterative algorithm to coordinate broadcast relays, ensuring coverage while minimizing redundancy. The algorithm prioritizes legacy routers/repeaters and uses timeout-based candidate selection:

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
SignalRouting attempts to coordinate broadcast relays to reduce redundant transmissions. In networks with multiple nodes that can hear the same transmissions, the algorithm identifies which nodes provide unique coverage and prioritizes their relays. However, coordination depends on accurate topology information and may fall back to contention-based approaches when topology is incomplete.

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

SignalRouting implements conservative unicast relaying to prevent unnecessary transmissions:

1. **Direct Sender Coverage Check**: If the sending node has a direct connection to the destination, don't relay (destination already received the packet directly)
2. **Downstream Relay Check**: If destination is downstream of a relay we can hear, broadcast for coordination
3. **Legacy Router Priority**: Check if legacy routers that heard the packet should relay instead
4. **Route Cost Comparison**: Compare our route cost against other nodes that heard the transmission
5. **Gateway Coordination**: Use gateway relationships for extended network reach

```cpp
bool shouldRelayUnicastForCoordination(NodeNum destination, NodeNum heardFrom) {
    // 1. Check if sender has direct connection to destination
    if (senderHasDirectConnectionTo(heardFrom, destination)) {
        return false; // Destination already received directly
    }

    // 2. Check downstream relay coordination...
    // 3. Check legacy router priorities...
    // 4. Compare route costs...
}
```

### Performance Characteristics

**Broadcast Coordination:**
- Attempts to minimize redundant transmissions through coverage analysis
- Uses timeout-based coordination rather than perfect synchronization
- May still have some redundant relays in dynamic network conditions
- Effectiveness depends on topology knowledge and node participation

**Unicast Optimization:**
- Uses ETX-based routing for known destinations
- Provides basic retransmission recovery for unicast failures
- May broadcast unicast packets for coordination in well-connected scenarios
- Falls back to opportunistic forwarding when routes are unknown

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

**"Broadcast unicasts being sent"**
- SignalRouting only broadcasts unicast packets for known destinations with good routes
- Unknown destinations use traditional unicast routing to prevent network flooding

SignalRouting provides an alternative to traditional flooding-based mesh networking, offering basic coordination for packet relay decisions. It works alongside existing routing approaches and provides benefits in networks with sufficient SignalRouting-capable nodes, while maintaining compatibility with legacy devices through prioritized relay handling.
