# Meshtastic Firmware Module Patterns

Architectural patterns and best practices learned from ZmodemModule development.

## Module Architecture

### Base Classes

**MeshModule** - Use for custom packet filtering
```cpp
class YourModule : public MeshModule {
    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};
```

**SinglePortModule** - Use for single port modules
```cpp
class YourModule : public SinglePortModule {
    YourModule() : SinglePortModule("name", meshtastic_PortNum_YOUR_PORT) {}
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};
```

### Type System

- Use `meshtastic_MeshPacket` not `MeshPacket`
- Return `ProcessMessage::STOP` or `ProcessMessage::CONTINUE`
- Use `router->allocForSending()` for packet creation
- Access node info via `nodeDB` not `mesh`

### Dual-Port Handling

For modules needing multiple ports:
```cpp
bool YourModule::wantPacket(const meshtastic_MeshPacket *p) {
    return (p->decoded.portnum == PORT_A || 
            p->decoded.portnum == PORT_B);
}

ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) {
    if (mp.decoded.portnum == PORT_A) return handlePortA(mp);
    if (mp.decoded.portnum == PORT_B) return handlePortB(mp);
    return ProcessMessage::CONTINUE;
}
```

## Multi-Instance Architecture

### Session-Based Design

For concurrent operations:
```cpp
struct Session {
    uint32_t id;              // Unique identifier
    NodeNum remoteNode;       // Peer
    State state;              // Current state
    unsigned long lastActivity; // Timeout tracking
    InstanceType* instance;   // Per-session handler
};

std::vector<Session*> activeSessions;
```

### Session Management Pattern

```cpp
Session* createSession(params) {
    if (sessions.size() >= MAX) return nullptr;
    Session* s = new Session(nextId++, params);
    sessions.push_back(s);
    return s;
}

void removeSession(Session* s) {
    auto it = std::find(sessions.begin(), sessions.end(), s);
    if (it != sessions.end()) sessions.erase(it);
    delete s;
}

void cleanupStale() {
    for (size_t i = 0; i < sessions.size(); ) {
        if (sessions[i]->isTimedOut()) {
            removeSession(sessions[i]);
        } else i++;
    }
}
```

### Loop Processing

```cpp
void YourModule::loop() {
    // Process all sessions
    for (size_t i = 0; i < sessions.size(); ) {
        Session* s = sessions[i];
        
        // Update state
        s->instance->loop();
        
        // Check completion
        if (s->isComplete() || s->hasError()) {
            removeSession(s);
            continue; // Don't increment
        }
        i++;
    }
    
    // Periodic cleanup
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > CLEANUP_INTERVAL) {
        cleanupStale();
        lastCleanup = millis();
    }
}
```

## Port Configuration

### Private Application Ports

Use range 200-255 for custom applications:
```cpp
#define YOUR_COMMAND_PORT 250
#define YOUR_DATA_PORT 251
```

### Port Registration

Ensure ports don't conflict with existing modules:
```bash
grep "PORTNUM.*250" firmware/src/**/*.{h,cpp}
```

## Packet Handling

### Sending Packets

```cpp
void sendMessage(NodeNum dest, const String& msg, meshtastic_PortNum port) {
    meshtastic_MeshPacket *packet = router->allocForSending();
    if (!packet) return; // Allocation failed
    
    packet->to = dest;
    packet->decoded.portnum = port;
    packet->want_ack = false; // or true for reliability
    
    size_t len = msg.length();
    if (len > sizeof(packet->decoded.payload.bytes)) {
        len = sizeof(packet->decoded.payload.bytes);
    }
    memcpy(packet->decoded.payload.bytes, msg.c_str(), len);
    packet->decoded.payload.size = len;
    
    router->enqueueReceivedMessage(packet);
}
```

### Receiving Packets

```cpp
ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) {
    // Extract payload
    size_t len = mp.decoded.payload.size;
    if (len == 0) return ProcessMessage::CONTINUE;
    
    char buffer[MAX_LEN + 1];
    memcpy(buffer, mp.decoded.payload.bytes, len);
    buffer[len] = '\0';
    String data(buffer);
    
    // Process
    processData(data, mp.from);
    
    return ProcessMessage::STOP;
}
```

## Memory Management

### Per-Session Allocation

- Allocate per session: ~1-1.5KB typical
- Use configurable limits: `MAX_CONCURRENT_SESSIONS`
- Monitor heap: Check free memory periodically
- Clean up promptly: Remove sessions on completion/error

### Memory Efficiency

```cpp
// Good: Per-session instances
Session* s = new Session();
s->handler = new Handler();

// Bad: Global static arrays
static Handler handlers[MAX_SESSIONS]; // Wastes memory
```

## Integration Checklist

- [ ] Correct base class (MeshModule or SinglePortModule)
- [ ] Correct type system (meshtastic_MeshPacket)
- [ ] Proper packet allocation (router->allocForSending)
- [ ] Return ProcessMessage enum from handleReceived
- [ ] Port numbers in private range (200-255)
- [ ] No port conflicts with other modules
- [ ] Memory efficient design (<10KB total)
- [ ] Timeout and cleanup implemented
- [ ] Error handling comprehensive
- [ ] Module registered in Modules.cpp

## Reference Modules

- `TextMessageModule` - Simple single-port pattern
- `SerialModule` - Dual module (thread + radio) pattern
- `StoreForwardModule` - ProtobufModule pattern
- `ZmodemModule` - Multi-instance concurrent pattern

## Documentation Standards

1. **Technical Spec** (IMPLEMENTATION_PLAN.md)
   - Architecture decisions
   - Task breakdown
   - Risk assessment
   - Testing strategy

2. **Integration Guide** (INTEGRATION.md)
   - Step-by-step firmware integration
   - Configuration options
   - Troubleshooting
   - API reference

3. **Overview** (README.md)
   - Quick start
   - Usage examples
   - Performance metrics
   - Status and roadmap

4. **Quick Reference** (QUICK_REFERENCE.md)
   - One-page developer guide
   - Common issues
   - Debug tips

---
**Created**: 2025-12-01
**Context**: ZmodemModule v2.0.0 development
**Status**: Validated patterns for firmware module development
