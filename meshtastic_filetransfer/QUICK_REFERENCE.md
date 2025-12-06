# ZmodemModule Quick Reference

One-page reference for developers integrating and using the ZmodemModule.

## File Structure

```
meshtastic_filetransfer/
├── modules/
│   ├── ZmodemModule.h          # Module header
│   └── ZmodemModule.cpp        # Implementation
├── AkitaMeshZmodemConfig.h     # Configuration
├── README.md                   # Overview
├── INTEGRATION.md              # Integration guide
├── IMPLEMENTATION_PLAN.md      # Technical spec
└── QUICK_REFERENCE.md          # This file
```

## Integration Checklist

- [ ] Copy files to `firmware/src/modules/`
- [ ] Add to `Modules.cpp`: `#include "modules/ZmodemModule.h"`
- [ ] Register: `zmodemModule = new ZmodemModule();`
- [ ] Add AkitaMeshZmodem lib dependency
- [ ] Build and flash: `pio run -t upload`
- [ ] Check serial: "Initializing ZmodemModule v2.0.0..."

## Command API

### Send File
```
SEND:!<hex_node_id>:/path/to/file.txt
```

Example:
```
SEND:!a1b2c3d4:/data/sensor.log
```

### Receive File
```
RECV:/path/to/save.txt
```

Example:
```
RECV:/data/received.log
```

## Response Codes

| Response | Meaning |
|----------|---------|
| `OK: Started SEND...` | Transfer initiated successfully |
| `OK: Started RECV...` | Ready to receive file |
| `ERROR: Maximum concurrent transfers...` | Hit limit (5 default) |
| `ERROR: Invalid SEND format...` | Command syntax error |
| `ERROR: Transfer already in progress...` | Session exists with that node |
| `ERROR: Failed to create...` | Memory or initialization error |

## Configuration

### In `ZmodemModule.h`:

```cpp
// Maximum concurrent transfers (default: 5)
#define MAX_CONCURRENT_TRANSFERS 5

// Session timeout (default: 60000ms = 60s)
#define TRANSFER_SESSION_TIMEOUT_MS 60000
```

### In `AkitaMeshZmodemConfig.h`:

```cpp
// Port numbers
#define AKZ_ZMODEM_COMMAND_PORTNUM 250  // Command channel
#define AKZ_ZMODEM_DATA_PORTNUM 251     // Data channel

// Packet size (default: 230 bytes)
#define AKZ_DEFAULT_MAX_PACKET_SIZE 230

// Timeouts
#define AKZ_DEFAULT_ZMODEM_TIMEOUT 30000
```

## Module API

### Key Methods

```cpp
class ZmodemModule : public MeshModule {
public:
    ZmodemModule();                     // Constructor
    virtual void setup();               // Initialize
    virtual void loop();                // Process transfers

protected:
    virtual bool wantPacket(p);         // Filter packets
    virtual ProcessMessage handleReceived(mp); // Handle packets

private:
    TransferSession* createSession(...);    // New transfer
    void removeSession(...);                // Cleanup
    void cleanupStaleSessions();            // Timeout check
};
```

### TransferSession

```cpp
struct TransferSession {
    uint32_t sessionId;              // Unique ID
    NodeNum remoteNodeId;            // Peer node
    String filename;                 // File path
    TransferDirection direction;     // SEND/RECV
    TransferState state;             // Current state
    uint32_t bytesTransferred;       // Progress
    unsigned long lastActivity;      // For timeout
    AkitaMeshZmodem* zmodemInstance; // Protocol handler
};
```

## Status Monitoring

### Log Output (every 30s if active)

```
=== ZmodemModule Status ===
Active sessions: 2 / 5
  Session 1: SEND | SENDING | Node 0xa1b2c3d4 | /file.txt | 50/100 KB (50.0%) | Idle: 200ms
  Session 2: RECV | RECEIVING | Node 0x12345678 | /data.log | 20/80 KB (25.0%) | Idle: 150ms
===========================
```

### Transfer States

| State | Meaning |
|-------|---------|
| `IDLE` | Session created, not started |
| `SENDING` | Actively sending data |
| `RECEIVING` | Actively receiving data |
| `COMPLETE` | Transfer finished successfully |
| `ERROR` | Transfer failed |

## Memory Usage

| Component | Size |
|-----------|------|
| Per session | ~1-1.5 KB |
| 5 concurrent | ~5-7.5 KB |
| Module overhead | ~500 bytes |
| **Total** | **~6-8 KB** |

## Performance

### Throughput (Approximate)

| LoRa Config | Speed | 1 MB Time |
|-------------|-------|-----------|
| SF7, BW250 | ~5 KB/s | ~3.5 min |
| SF9, BW250 | ~2 KB/s | ~8 min |
| SF12, BW125 | ~0.3 KB/s | ~55 min |

### Limits

- **Max concurrent transfers**: 5 (configurable)
- **Max packet size**: 230 bytes (Meshtastic MTU)
- **Session timeout**: 60 seconds (configurable)
- **Max filename length**: Platform dependent (String)

## Troubleshooting

### Module Not Loading

```bash
# Check compile output
pio run | grep ZmodemModule

# Check registration
grep "zmodemModule" src/modules/Modules.cpp
```

### Commands Not Working

```cpp
// Enable debug logging
#define LOG_LEVEL LOG_LEVEL_DEBUG

// Check ports not blocked
grep "PORTNUM 250" firmware/src/**/*.cpp
grep "PORTNUM 251" firmware/src/**/*.cpp
```

### Memory Issues

```cpp
// Reduce concurrent transfers
#define MAX_CONCURRENT_TRANSFERS 3

// Monitor heap
LOG_INFO("Free heap: %d", ESP.getFreeHeap());
```

### Transfer Hangs

```cpp
// Increase timeout
#define TRANSFER_SESSION_TIMEOUT_MS 120000  // 2 minutes

// Check loop() is called
LOG_DEBUG("Loop called");
```

## Common Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| No initialization msg | Not registered | Add to Modules.cpp |
| Commands ignored | Wrong port | Check 250/251 not blocked |
| Transfer hangs | Timeout too short | Increase timeout |
| Memory errors | Too many sessions | Reduce MAX_CONCURRENT |
| Compile errors | Wrong types | Check meshtastic_MeshPacket |

## Debug Tips

### Enable Verbose Logging

```ini
[env:your_board]
build_flags =
    -DDEBUG_PORT=Serial
    -DLOG_LEVEL=LOG_LEVEL_DEBUG
```

### Monitor Sessions

Check status logs every 30s when transfers active.

### Test Command Format

```bash
# Valid
SEND:!a1b2c3d4:/data/file.txt  ✓
RECV:/data/file.txt            ✓

# Invalid
SEND a1b2c3d4:/file.txt        ✗  (missing ! and :)
RECV data/file.txt             ✗  (not absolute path)
send:!abc:/file.txt            ✗  (lowercase)
```

## Integration TODOs

Before production use, verify:

- [ ] AkitaMeshZmodem `begin()` signature
- [ ] AkitaMeshZmodem `processDataPacket()` type
- [ ] NodeID parsing method
- [ ] Filesystem API (FSCom vs platform-specific)
- [ ] Logging macros (LOG_* availability)
- [ ] Router API (allocForSending, enqueueReceivedMessage)

## Code Locations

### Key Files

```
firmware/src/modules/ZmodemModule.h:92   // Class definition
firmware/src/modules/ZmodemModule.cpp:60 // Constructor
firmware/src/modules/ZmodemModule.cpp:93 // loop() - process sessions
firmware/src/modules/ZmodemModule.cpp:148 // handleReceived() - packet routing
firmware/src/modules/ZmodemModule.cpp:220 // handleCommand() - SEND/RECV
firmware/src/modules/ZmodemModule.cpp:416 // createSession() - new transfer
```

### Integration Points

```
firmware/src/modules/Modules.cpp         // Module registration
platformio.ini or CMakeLists.txt         // Build config
AkitaMeshZmodemConfig.h                  // Port and config
```

## Example: Send File from Coordinator

```cpp
// Coordinator code to initiate transfer
void sendFileToNode(NodeNum targetNode, const char* filename) {
    // Format command
    char cmd[200];
    snprintf(cmd, sizeof(cmd), "SEND:!%08x:%s", targetNode, filename);

    // Send to target node's ZmodemModule (port 250)
    meshtastic_MeshPacket *packet = router->allocForSending();
    packet->to = targetNode;
    packet->decoded.portnum = 250;  // AKZ_ZMODEM_COMMAND_PORTNUM
    packet->decoded.payload.size = strlen(cmd);
    memcpy(packet->decoded.payload.bytes, cmd, strlen(cmd));
    router->enqueueReceivedMessage(packet);

    // Monitor for response
    // ... listen on port 250 for "OK:" or "ERROR:" response
}
```

## Links

- **README**: Overview and quick start
- **INTEGRATION.md**: Detailed integration guide
- **IMPLEMENTATION_PLAN.md**: Technical specification
- **Meshtastic Docs**: https://meshtastic.org/docs/

---

**Version**: 2.0.0
**Last Updated**: 2025-12-01
**Quick Ref Version**: 1.0
