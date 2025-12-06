# Meshtastic File Transfer Module - Implementation Plan

## Executive Summary
Adapt existing ZmodemModule to work within Meshtastic firmware architecture with support for multiple concurrent file transfers.

## Current State Analysis

### Existing Implementation
- **Location**: `modules/ZmodemModule.{h,cpp}`
- **Protocol**: Zmodem file transfer over Meshtastic
- **Ports**: 250 (command), 251 (data)
- **Library**: AkitaMeshZmodem (external dependency)
- **Transfer Limit**: Single transfer only (blocks concurrent)

### Critical Issues Identified

1. **❌ Architecture Mismatch**
   - Uses `Module` base class → Should use `MeshModule`
   - Uses `MeshPacket` → Should use `meshtastic_MeshPacket`
   - Returns `bool` from handleReceived → Should return `ProcessMessage`
   - Direct packet creation → Should use `router->allocForSending()`

2. **❌ Single Transfer Limitation**
   - Line 138-142: Blocks if transfer in progress
   - No session management
   - Global state in akitaZmodem instance
   - Cannot handle concurrent send/receive

3. **⚠️ ACK Verification**
   - Not visible in module code (may be in library)
   - Need to verify implementation
   - May need explicit ACK tracking

4. **✅ Port Configuration**
   - Private ports 250, 251 properly configured
   - Separate command and data channels

## Implementation Strategy

### Phase 1: Core Architecture Fix
**Goal**: Make module compatible with Meshtastic firmware patterns

#### 1.1 Update Base Class (ZmodemModule.h)
```cpp
// OLD: class ZmodemModule : public Module
// NEW: class ZmodemModule : public MeshModule
```

**Changes**:
- Inherit from `MeshModule` not `Module`
- Remove `MeshInterface& mesh` parameter (not used in MeshModule pattern)
- Update constructor signature
- Override `wantPacket()` for dual-port filtering
- Change `handleReceived()` return type to `ProcessMessage`

#### 1.2 Fix Type System
**Replace**:
- `MeshPacket` → `meshtastic_MeshPacket`
- `bool handleReceived()` → `ProcessMessage handleReceived()`
- `mesh.getNodeNum()` → `nodeDB.getNodeNum()`
- Direct packet creation → `router->allocForSending()`

#### 1.3 Implement Dual-Port Handling
```cpp
virtual bool wantPacket(const meshtastic_MeshPacket *p) override {
    return p->decoded.portnum == AKZ_ZMODEM_COMMAND_PORTNUM ||
           p->decoded.portnum == AKZ_ZMODEM_DATA_PORTNUM;
}
```

### Phase 2: Multiple Transfer System
**Goal**: Support concurrent file transfers with session management

#### 2.1 Transfer Session Design
```cpp
enum class TransferDirection {
    SEND,
    RECEIVE
};

struct TransferSession {
    uint32_t sessionId;           // Unique identifier
    NodeNum remoteNodeId;         // Peer node
    String filename;              // File path
    TransferDirection direction;  // SEND or RECEIVE
    AkitaMeshZmodem::TransferState state;
    uint32_t bytesTransferred;
    uint32_t totalSize;
    unsigned long lastActivity;   // Timeout tracking
    AkitaMeshZmodem* zmodemInstance; // Per-session protocol handler
};
```

#### 2.2 Session Management
```cpp
class ZmodemModule : public MeshModule {
private:
    std::vector<TransferSession> activeSessions;
    uint32_t nextSessionId = 1;

    TransferSession* findSession(NodeNum nodeId);
    TransferSession* findSessionById(uint32_t id);
    TransferSession* createSession(NodeNum nodeId, String filename, TransferDirection dir);
    void removeSession(uint32_t sessionId);
    void cleanupStaleSessions();
};
```

#### 2.3 Concurrent Transfer Logic
- **Command Handler**: Creates new session instead of blocking
- **Data Router**: Routes packets to correct session by NodeNum
- **Loop Handler**: Process all active sessions
- **Timeout**: Clean up stale sessions (>60s idle)

#### 2.4 Transfer Limits
```cpp
#define MAX_CONCURRENT_TRANSFERS 5  // Configurable limit
```

### Phase 3: ACK Verification & Reliability
**Goal**: Ensure reliable transfer with proper acknowledgment

#### 3.1 ACK Tracking
- Verify AkitaMeshZmodem library handles ACKs
- If not, implement chunk-level ACK tracking
- Add retry mechanism for failed chunks
- Timeout and error handling

#### 3.2 Packet-Level Reliability
```cpp
// Use Meshtastic's want_ack for critical packets
replyPacket.want_ack = true;  // For command responses
```

#### 3.3 Transfer Verification
- File size verification
- Checksum validation (if supported by Zmodem)
- Completion confirmation message
- Error reporting to initiator

### Phase 4: Firmware Integration
**Goal**: Properly integrate with Meshtastic module system

#### 4.1 Module Registration
Create module instance and registration in firmware:
```cpp
// In Modules.cpp or equivalent
#include "modules/ZmodemModule.h"
ZmodemModule *zmodemModule;

void setupModules() {
    // ... other modules
    zmodemModule = new ZmodemModule();
}
```

#### 4.2 Build System Integration
- Add to CMakeLists.txt or platformio.ini
- Include AkitaMeshZmodem library
- Verify dependencies

#### 4.3 Configuration
- Port number definitions (already in AkitaMeshZmodemConfig.h)
- Transfer limits
- Timeout values
- Buffer sizes

## Detailed Task Breakdown

### Task 1: Update ZmodemModule.h
**File**: `modules/ZmodemModule.h`

**Changes**:
1. Change base class to MeshModule
2. Update constructor (remove mesh parameter)
3. Add wantPacket() override
4. Change handleReceived() signature
5. Add TransferSession struct
6. Add session management members
7. Update method signatures

**Dependencies**: None

### Task 2: Update ZmodemModule.cpp
**File**: `modules/ZmodemModule.cpp`

**Changes**:
1. Update constructor implementation
2. Fix all type references (MeshPacket → meshtastic_MeshPacket)
3. Change handleReceived() return values (true→STOP, false→CONTINUE)
4. Implement session management methods
5. Update command handler to create sessions
6. Update loop to process all sessions
7. Fix packet creation using router->allocForSending()
8. Update sendReply() implementation

**Dependencies**: Task 1

### Task 3: Session Management Implementation
**Location**: `ZmodemModule.cpp`

**Methods to implement**:
- `createSession()`: Allocate new TransferSession
- `findSession()`: Lookup by NodeNum
- `findSessionById()`: Lookup by session ID
- `removeSession()`: Cleanup completed/failed session
- `cleanupStaleSessions()`: Timeout handler

**Dependencies**: Task 2

### Task 4: Multi-Transfer Logic
**Location**: `ZmodemModule.cpp`

**Updates**:
1. Remove single-transfer check in handleCommand()
2. Add concurrent transfer limit check
3. Route data packets to correct session
4. Process all active sessions in loop()
5. Handle session state transitions

**Dependencies**: Task 3

### Task 5: ACK Implementation
**Location**: `ZmodemModule.cpp`

**Verify**:
1. Check AkitaMeshZmodem library ACK handling
2. Add explicit ACK if needed
3. Implement retry logic
4. Add timeout handling

**Dependencies**: Task 4

### Task 6: Integration Code
**Files**: Create integration documentation

**Content**:
1. How to add to firmware build
2. Module registration code
3. Configuration options
4. Testing procedures

**Dependencies**: Tasks 1-5

## Testing Strategy

### Unit Tests
1. Session creation and lookup
2. Concurrent session management
3. Command parsing
4. Packet routing

### Integration Tests
1. Single file transfer
2. Concurrent transfers (2-5 simultaneous)
3. Large file transfer (>1MB)
4. Transfer cancellation
5. Error recovery
6. Timeout handling

### Firmware Integration Tests
1. Module loads successfully
2. Responds to commands
3. Integrates with router
4. Doesn't interfere with other modules

## Dependencies & Requirements

### External Dependencies
- **AkitaMeshZmodem Library**: Core Zmodem protocol implementation
  - Must be available in firmware build environment
  - API must support multiple instances (or be modified to do so)

### Meshtastic Firmware Components
- `MeshModule` base class
- `router->allocForSending()` for packet creation
- `nodeDB` for node information
- Filesystem API for file operations
- Serial logging (LOG_INFO, LOG_ERROR)

### Build System
- Must include `modules/ZmodemModule.cpp` in compilation
- Must link AkitaMeshZmodem library
- Private port numbers 250, 251 must not conflict

## Risk Assessment

### High Risk
1. **AkitaMeshZmodem Library Compatibility**
   - May not support multiple instances
   - Mitigation: Verify library design, modify if needed

2. **Memory Constraints**
   - Multiple sessions consume memory
   - Mitigation: Limit concurrent transfers, add memory checks

### Medium Risk
1. **Port Conflicts**
   - Ports 250, 251 may be used by other modules
   - Mitigation: Verify port allocation, use different ports if needed

2. **Performance Impact**
   - Multiple transfers may strain network
   - Mitigation: Add rate limiting, transfer scheduling

### Low Risk
1. **Integration Issues**
   - Module registration may need adjustments
   - Mitigation: Follow existing module patterns closely

## Success Criteria

### Functional Requirements
✅ Module compiles with firmware
✅ Handles SEND and RECV commands
✅ Supports 3+ concurrent transfers
✅ Proper ACK verification
✅ Uses private ports (250, 251)
✅ Compatible with coordinator (future work)

### Non-Functional Requirements
✅ Memory efficient (<10KB per session)
✅ Network efficient (respects Meshtastic MTU)
✅ Reliable (handles timeouts, retries)
✅ Maintainable (follows Meshtastic patterns)

## Next Steps

1. ✅ Create implementation plan (this document)
2. ⏳ Update ZmodemModule.h
3. ⏳ Update ZmodemModule.cpp
4. ⏳ Implement session management
5. ⏳ Test with firmware
6. ⏳ Document integration
7. ⏳ Prepare for coordinator integration

## Notes for Future Coordinator Integration

The coordinator will need to:
- Send commands to ZmodemModule via Meshtastic messages
- Track transfer progress across multiple nodes
- Handle transfer orchestration
- Provide user interface for file management

Module interface is ready for coordinator commands on port 250.

---

**Version**: 1.0
**Date**: 2025-12-01
**Status**: Ready for implementation
