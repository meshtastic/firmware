# USB Capture Module - Project Context

**Project:** Meshtastic USB Keyboard Capture Module for RP2350
**Platform:** XIAO RP2350-SX1262 + Heltec V4 (receiver)
**Version:** v7.6 - Randomized TX Interval (Traffic Analysis Resistance)
**Status:** ✅ Production Ready with Randomized Timing
**Last Updated:** 2025-12-14

---

## Quick Reference

### Current Status
- ✅ **Build:** Flash 58.4%, RAM 24.4% - Compiles cleanly
- ✅ **Core Features:** USB capture, FRAM storage, mesh broadcast, channel PSK
- ✅ **Port:** 490 (custom private port in 256-511 range) - Module registered for this port
- ✅ **Channel:** 1 "takeover" with PSK encryption
- ✅ **TX Interval:** Randomized 40s-4min (traffic analysis resistant)
- ✅ **Batch Size:** 180 bytes raw → ~230 bytes decoded (single packet, no fragmentation)
- ✅ **Auto-Broadcast:** No target node required - broadcasts immediately on channel 1
- ✅ **ACK Reception:** Module now properly receives ACKs on port 490 (v7.3 fix)
- ✅ **RAM Fallback:** MinimalBatchBuffer (2 slots, NASA Power of 10 compliant)
- ✅ **FRAM Storage:** 256KB non-volatile (Fujitsu MB85RS2MTA) - Primary storage
- ✅ **Deduplication:** Flash-persistent batch tracking prevents duplicate storage (v7.1)
- ✅ **Core1 Health:** Real-time monitoring with stall detection (v7.4)
- ✅ **Protocol Version:** Magic marker header for backwards compatibility (v7.4)
- ✅ **Command Auth:** Optional auth token for sensitive commands (v7.4)
- ✅ **Capacity Alerts:** Threshold-based FRAM usage warnings (v7.4)
- ⚠️ **Commands:** Must be sent on port 490 (not text messages) - use Heltec canned messages

### Key Achievement v7.6: Randomized TX Interval
**Traffic analysis resistance through unpredictable transmission timing:**
- **Before (v7.5):** Fixed 5-minute interval - predictable traffic patterns
- **After (v7.6):** Random interval 40 seconds to 4 minutes
- **Benefits:** Harder to detect/analyze mesh traffic, reduced congestion patterns
- **Implementation:** New random interval generated after each successful transmission
- **Log Output:** `Tx: Batch 0x... queued (timeout 30000ms, next_tx in 147s)`

### Key Achievement v7.5: Broadcast ACK with Multi-XIAO Support
**Fixed PKI encryption bypass and enabled multi-XIAO deployments:**
- **Problem:** ACKs sent as direct messages → Router.cpp forces PKI → XIAO can't decrypt (Ch=0x0)
- **Solution:** ACKs now broadcast on channel 1 (broadcasts bypass PKI per Router.cpp:614)
- **Enhanced Format:** `ACK:0x{batch_id}:!{receiver_node}` enables multi-XIAO filtering
- **RTC Batch IDs:** Upper 16 bits = RTC seconds, Lower 16 bits = random (no dedup collision on reboot)
- **Backwards Compatible:** Old ACK format still accepted for legacy receivers

### Key Achievement v7.4: Enhanced Monitoring & Security
**Comprehensive operational improvements based on architecture review:**
- **Core1 Health Monitoring:** Real-time tracking of USB capture health (stall detection, error rates)
- **Protocol Versioning:** Magic marker (0x55 0x4B) enables backwards-compatible format detection
- **Command Authentication:** Optional `AUTH:<token>:<command>` format for sensitive commands
- **FRAM Capacity Alerts:** Threshold warnings at 75%, 90%, 99% usage levels
- **FRAM Eviction Tracking:** Statistics on oldest-batch evictions when storage full

### Key Achievement v7.3: Port 490 Module Registration (ACK Fix)
**Fixed ACK reception - module now properly receives ACK responses:**
- **Problem:** USBCaptureModule was registered for TEXT_MESSAGE_APP (port 1), couldn't receive ACKs on port 490
- **Symptom:** XIAO transmitted, Heltec received and sent ACK, but XIAO never processed ACK → infinite retries
- **Solution:** Changed `SinglePortModule` registration from TEXT_MESSAGE_APP to port 490
- **Trade-off:** Commands (STATUS, STATS, etc.) must now come on port 490, not text messages

### Key Achievement v7.2: Single-Packet Batches + Auto-Broadcast
**Complete batches in single LoRa packet, no target node required:**
- **Before (v7.1):** 500-byte buffer could truncate, required target node before TX
- **After (v7.2):** 200-byte buffer → ~230 decoded, fits in 233-byte packet limit
- **Auto-Broadcast:** XIAO broadcasts immediately, Heltec ACKs back as direct message
- **No Fragmentation:** Complete batch guaranteed in every transmission

### Key Achievement v7.1: Receiver-Side Deduplication
**Flash-persistent deduplication prevents duplicate storage on retransmits:**
- **Problem:** When ACK lost in mesh, sender retransmits → duplicate batches stored
- **Solution:** Per-node batch ID tracking with LRU eviction (16 nodes × 16 batches)
- **Persistence:** Stored in `/keylogs/.dedup_cache` (~1.2KB), survives reboots
- **Behavior:** Duplicate detected → ACK still sent (so sender clears FRAM) → no duplicate storage

### Key Achievement v7.0: Mesh Broadcast System
**Switched from PKI direct messages to mesh broadcast:**
- **Before (v6.0):** PKI-encrypted direct messages to targetNode, 6-second interval
- **After (v7.0):** Channel 1 PSK broadcasts to mesh, 5-minute interval
- **Benefits:** Multi-hop mesh routing, no PKI key exchange needed, mesh-friendly

### Key Achievement v5.0: FRAM Non-Volatile Storage
**62x storage capacity increase** with power-loss persistence:
- **Before:** 4KB RAM buffer (8 slots × 512 bytes), volatile
- **After:** 256KB FRAM (500+ batches), non-volatile, survives power loss
- **How:** Fujitsu MB85RS2MTA on SPI0 sharing bus with LoRa radio

### Key Achievement v3.0: 90% Core0 Overhead Reduction
**Dual-core architecture optimization:**
- **Before:** Core0 handled formatting, buffering, transmission (2% CPU)
- **After:** Core0 just polls storage and transmits (0.2% CPU)
- **How:** Moved ALL processing to Core1 with lock-free buffer management

### Quick Commands
```bash
# Build
cd /Users/rstown/Desktop/ste/firmware
pio run -e xiao-rp2350-sx1262

# Branch Management
git checkout dev/usb-capture  # Active development branch
git log upstream/master..dev/usb-capture --oneline  # View changes

# Flash Device
# Copy .pio/build/xiao-rp2350-sx1262/firmware.uf2 to device
```

---

## ✅ FRAM Storage Integration (v5.0)

### Hardware Configuration
- **FRAM Chip:** Fujitsu MB85RS2MTA (Adafruit breakout)
- **Capacity:** 2Mbit / 256KB
- **Interface:** SPI @ 20MHz (supports up to 40MHz)
- **Endurance:** 10^13 read/write cycles
- **Data Retention:** 200+ years at 25°C
- **CS Pin:** GPIO1 (D6)

### SPI Bus Sharing
```
SPI0 Bus (shared):
├─ FRAM CS:  GPIO1 (D6)  - Keystroke storage
├─ LoRa CS:  GPIO6 (D4)  - SX1262 radio
├─ SCK:      GPIO2 (D8)
├─ MISO:     GPIO4 (D9)
└─ MOSI:     GPIO3 (D10)
```

**Thread Safety:** Uses `concurrency::LockGuard` with global `spiLock` (same as LoRa)

### FRAM vs RAM Buffer Comparison

| Aspect | Old (RAM Buffer) | New (FRAM) |
|--------|------------------|------------|
| Capacity | 4KB (8 slots) | 256KB (500+ batches) |
| Persistence | Volatile | Non-volatile |
| Power Loss | Data lost | Data preserved |
| Batch Size | 512B fixed | Up to 512B variable |
| Write Speed | Instant | ~1ms per batch |
| Endurance | Unlimited | 10^13 cycles |

### Boot Log (Hardware Validated)
```
[USBCapture] Initializing FRAM storage on SPI0, CS=GPIO1
FRAM: Found valid storage with 0 batches
[USBCapture] FRAM: Initialized (Mfr=0x04, Prod=0x4803)
[USBCapture] FRAM: 0 batches pending, 262128 bytes free
```

---

## Architecture Overview

### Core Distribution
```
Core1 (Producer):
  USB → PIO → Packet Handler → HID Decoder → Buffer Manager → FRAM

Core0 (Consumer):
  FRAM → Read Batch → Decode Text → Transmit (LoRa) → Delete Batch
```

### Key Components

**1. FRAM Batch Storage** (`FRAMBatchStorage.cpp/h`) - NEW in v5.0
- 256KB non-volatile storage with circular buffer management
- Thread-safe SPI access via `concurrency::LockGuard`
- Auto-cleanup of old batches when storage full
- NASA Power of 10 compliant (assertions, fixed bounds, no dynamic alloc)

**2. RAM Buffer Fallback** (`psram_buffer.h/cpp`)
- 8 slots × 512 bytes = 4KB capacity (used if FRAM init fails)
- Lock-free producer/consumer with memory barriers
- Statistics tracking (transmission failures, overflows, retries)

**3. Core1 Processing** (`keyboard_decoder_core1.cpp`)
- 500-byte keystroke buffer with delta-encoded timestamps (70% space savings)
- Full modifier key support (Ctrl, Alt, GUI, Shift)
- Auto-finalization on buffer full or overflow
- Writes to FRAM when available, falls back to RAM buffer

**4. Core0 Transmission** (`USBCaptureModule.cpp`)
- Ultra-lightweight FRAM/PSRAM polling
- Text decoding for phone apps
- LoRa transmission with 3-attempt retry and 6-second rate limiting
- FRAM batch deletion after successful transmission
- Remote commands: STATUS, START, STOP, STATS

**5. RTC Integration** (v4.0)
- Three-tier fallback: RTC (mesh sync) → BUILD_EPOCH+uptime → uptime only
- Real unix epoch timestamps when mesh-synced
- Quality indicator logging (GPS, Net, None)

---

## Critical Multi-Core Safety Rules

### ❌ FORBIDDEN in Core1
- **NO** logging (`LOG_INFO`, `printf`, etc.) - causes crashes
- **NO** shared mutable state without `volatile`
- **NO** tight loops without `tight_loop_contents()` - bus contention
- **NO** code executing from flash - MUST use `CORE1_RAM_FUNC`
- **NO** watchdog enabled during pause/exit - causes reboot loops

### ✅ REQUIRED Practices
- Use PSRAM/queues for Core0↔Core1 data passing
- Lock-free algorithms only (no mutexes)
- Mark ALL Core1 functions with `CORE1_RAM_FUNC` macro
- Add `__dmb()` memory barriers for cross-core synchronization
- Disable watchdog via hardware register (0x40058000) during pause
- Call `tight_loop_contents()` in Core1 loops for bus arbitration

### Why These Rules Exist
**Root Cause Chain (v3.2 Discovery):**
1. Arduino-Pico uses intercore FIFO to pause Core1 during flash writes
2. USB Capture also uses FIFO → conflict → pause mechanism fails
3. Core1 continues running, tries to fetch instruction from flash during write
4. Flash in write mode → instruction fetch impossible → **CRASH**

**Solution:** RAM execution + manual pause + watchdog management

---

## Hardware Setup

### GPIO Connections (CRITICAL: Must be consecutive!)
```
USB Keyboard → XIAO RP2350
├─ D+ (Green)  → GPIO 16
├─ D- (White)  → GPIO 17
├─ GND (Black) → GND
└─ VBUS (Red)  → 5V (optional)
```

**Pin Constraint:** GPIO 16/17 MUST be consecutive for PIO hardware requirements.

### FRAM Connection (SPI0 shared with LoRa)
```
Adafruit FRAM Breakout (MB85RS2MTA) → XIAO RP2350
├─ CS    → GPIO1 (D6)   - Chip Select
├─ SCK   → GPIO2 (D8)   - SPI Clock (shared)
├─ MISO  → GPIO4 (D9)   - SPI Data In (shared)
├─ MOSI  → GPIO3 (D10)  - SPI Data Out (shared)
├─ VCC   → 3V3
└─ GND   → GND
```

**Note:** FRAM and LoRa share SPI0 bus. Thread safety via `spiLock`.

### USB Speed Configuration
```cpp
// In USBCaptureModule::init() - change if needed
capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_LOW);   // 1.5 Mbps (default)
capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_FULL);  // 12 Mbps (alternative)
```

---

## Repository Structure

### Branch Strategy
```
upstream/master (Meshtastic official)
       ↓
    master (tracks upstream, never commit here)
       ↓
 dev/usb-capture (active development, 11 commits ahead)
```

### Important Files

**Core Implementation:**
```
src/modules/USBCaptureModule.{cpp,h}
src/platform/rp2xx0/usb_capture/
  ├── psram_buffer.{cpp,h}              - Ring buffer (v3.0)
  ├── keyboard_decoder_core1.{cpp,h}   - HID decoder + buffer mgmt
  ├── usb_capture_main.{cpp,h}         - Core1 main loop
  ├── usb_packet_handler.{cpp,h}       - Packet processing
  ├── pio_manager.{c,h}                - PIO configuration
  └── common.h                          - CORE1_RAM_FUNC macro
```

**Documentation:**
```
modules/
  ├── USBCaptureModule_Documentation.md    - Complete architecture (v7.3) - PRIMARY
  ├── PSRAM_BUFFER_ARCHITECTURE.md         - Buffer design (v3.0)
  ├── RTC_INTEGRATION_DESIGN.md            - RTC three-tier fallback (v4.0)
  ├── IMPLEMENTATION_COMPLETE.md           - v3.0 summary (historical)
  ├── FILESYSTEM_DEADLOCK_INVESTIGATION.md - Multicore flash issue (resolved)
  ├── COMPREHENSIVE_ANALYSIS_2025-12-07.md - System-wide analysis
  ├── CORE1_OPTIMIZATION_PLAN.md           - Dual-core optimization (v3.0)
  ├── CORE_DISTRIBUTION_ANALYSIS.md        - Core workload analysis
  ├── FINAL_STATUS.md                      - Milestone summary
  ├── LESSONS_LEARNED.md                   - Development insights
  ├── NEXT_SESSION_HANDOFF.md              - Session continuity notes
  ├── ZMODEM_FILE_TRANSFER_MODULE.md       - Future feature design
  └── errors.md                            - Error tracking

src/platform/rp2xx0/usb_capture/
  └── README.md                            - Platform implementation (v7.3)

Root:
  └── CLAUDE.md                            - This file (project context)
```

**Configuration:**
- `platformio.ini` (root) - Build configuration
- `variants/rp2350/xiao-rp2350-sx1262/platformio.ini` - Board-specific

---

## Current Behavior (v4.1)

### Features Active
- ✅ Core1 captures keystrokes via PIO
- ✅ Core1 buffers with delta encoding
- ✅ Core1 writes to PSRAM on finalization
- ✅ Core0 reads from PSRAM and decodes to text
- ✅ **LoRa transmission ACTIVE** - Broadcasting over mesh
- ✅ Rate-limited to 6-second intervals
- ✅ Remote commands working (STATUS, START, STOP, STATS)
- ✅ **RTC timestamps** - Real unix epoch from mesh sync

### Log Output Example (v4.0)
```
[Core0] Transmitting buffer: 46 bytes (epoch 1765155817 → 1765155836)
[Core0] Time source: Net (quality=2)
=== BUFFER START ===
Start Time: 1765155817 (unix epoch from Net)
Line: how are you donb tjskjbfdsgsfhope tdiuhgdf
Enter [time=1765155835 seconds, delta=+18]
Line: d
Final Time: 1765155836 seconds
=== BUFFER END ===
[Core0] Transmitted decoded text (64 bytes)
```

### Statistics Output (every 20 seconds)
```
[Core0] PSRAM: 0 avail, 2 tx, 0 drop | Failures: 0 tx, 0 overflow, 0 psram | Retries: 0
[Core0] Time: RTC=1765155823 (Net quality=2) | uptime=202
```

---

## Version History

| Version | Date | Key Changes | Status |
|---------|------|-------------|--------|
| 1.0-2.1 | 2024-11 | Initial implementation, file consolidation | Superseded |
| 3.0 | 2025-12-06 | Core1 complete processing + PSRAM ring buffer | Validated |
| 3.1 | 2025-12-07 | Bus arbitration with `tight_loop_contents()` | Superseded |
| 3.2 | 2025-12-07 | Multi-core flash solution (RAM exec + pause + watchdog) | Validated |
| 3.3 | 2025-12-07 | LoRa transmission + text decoding + rate limiting | Validated |
| 3.4 | 2025-12-07 | Watchdog bootloop fix (hardware register access) | Validated |
| 3.5 | 2025-12-07 | Memory barriers + retry logic + modifier keys | Validated |
| 4.0 | 2025-12-07 | RTC integration with mesh time sync | Validated |
| 4.1 | 2025-12-07 | Filesystem timeout detection (diagnostics) | Superseded |
| 4.2-dev | 2025-12-08 | Multicore lockout investigation | Superseded |
| 5.0 | 2025-12-13 | FRAM non-volatile storage (256KB) | Validated |
| 6.0 | 2025-12-14 | ACK-based reliable transmission + KeylogReceiverModule | Validated |
| 7.0.1 | 2025-12-14 | Mesh broadcast + Channel PSK + MinimalBatchBuffer + Buffer init fix | Validated |
| 7.1 | 2025-12-14 | Receiver-side deduplication with flash persistence | Validated |
| 7.2 | 2025-12-14 | Single-packet batches (200B) + Auto-broadcast (no target node) | Validated |
| 7.3 | 2025-12-14 | Port 490 module registration - ACK reception fix | Validated |
| 7.4 | 2025-12-14 | Enhanced Monitoring & Security (Core1 health, auth, protocol version, capacity alerts) | Validated |
| 7.5 | 2025-12-14 | Broadcast ACK + RTC batch IDs + Multi-XIAO support | Validated |
| **7.6** | **2025-12-14** | **Randomized TX interval (40s-4min) for traffic analysis resistance** | **Current** ✅ |

### v7.6 - Randomized TX Interval (Current)

**Traffic analysis resistance through unpredictable timing:**

1. **Randomized Transmission Interval**
   - Before: Fixed 5-minute interval (predictable traffic patterns)
   - After: Random 40 seconds to 4 minutes (unpredictable)
   - Constants: `TX_INTERVAL_MIN_MS` (40000), `TX_INTERVAL_MAX_MS` (240000)

2. **Implementation Details**
   - Initial interval: 40 seconds (minimum)
   - After each TX: New random interval generated via `random(40000, 240001)`
   - Log shows next interval: `next_tx in 147s`

3. **Benefits**
   - Harder traffic analysis (no fixed cadence to detect)
   - Reduced mesh congestion patterns
   - Better battery efficiency (average ~2.3 min vs fixed 5 min)

### v7.5 - Broadcast ACK with Multi-XIAO Support

**Fixes PKI encryption issue and prevents batch ID collision:**

1. **Broadcast ACK (PKI Bypass)**
   - Problem: Direct message ACKs forced PKI encryption → XIAO couldn't decrypt (Ch=0x0)
   - Solution: ACKs now broadcast on channel 1 (broadcasts bypass PKI per Router.cpp:614)
   - Format: `ACK:0x{batch_id}:!{receiver_node}` (27 chars)

2. **RTC-Based Batch IDs (Dedup Fix)**
   - Problem: Sequential batch IDs (1, 2, 3...) restart on reboot → collision with receiver's dedup cache
   - Solution: Upper 16 bits = RTC seconds, Lower 16 bits = random
   - Same pattern as `core1_get_current_epoch()`: RTCQualityFromNet with BUILD_EPOCH fallback

3. **Multi-XIAO Support**
   - Enhanced ACK format includes receiver node ID
   - XIAO verifies ACK sender matches expected targetNode
   - Prevents cross-device ACK confusion in multi-XIAO deployments

4. **Backwards Compatibility**
   - Old ACK format `ACK:0x{batch_id}` still accepted
   - New receivers work with old senders (no sender verification)

### v7.4 - Enhanced Monitoring & Security

**New Features based on Architecture Review:**

1. **Core1 Health Monitoring (REQ-OPS-001)**
   - Real-time health metrics: `last_capture_time`, `capture_count`, `error_count`, `buffer_finalize_count`
   - Status tracking: OK, STALLED, ERROR, STOPPED
   - USB connection detection
   - Stall detection (60 second threshold)
   - Exposed via STATS command: `Core1: OK USB:Y 5s | Keys:150 Err:0 Buf:12`

2. **Protocol Version Header (REQ-PROTO-001)**
   - Magic marker: 0x55 0x4B ("UK" for USB Keylog)
   - Version: Major.Minor (currently 1.0)
   - Format: `[magic:2][version:2][batch_id:4][data:N]` (8-byte header)
   - Backwards compatible: Receiver detects magic marker to distinguish old vs new format

3. **Command Authentication (REQ-SEC-001)**
   - Optional auth token configured via platformio.ini: `-DUSB_CAPTURE_AUTH_TOKEN=\"mysecret\"`
   - Format: `AUTH:<token>:<command>` (e.g., `AUTH:mysecret:START`)
   - Protects sensitive commands: START, STOP, TEST
   - Read-only commands allowed without auth: STATUS, STATS, DUMP
   - Backwards compatible: Auth disabled when token not configured

4. **FRAM Capacity Alerting (REQ-OPS-002)**
   - Usage percentage tracking: `getUsagePercentage()` method
   - Threshold warnings:
     - 75%: INFO "FRAM: High capacity - Monitor closely"
     - 90%: WARN "FRAM: Critical capacity - Increase TX rate"
     - 99%: ERROR "FRAM: STORAGE FULL - Batches being evicted!"
   - Stats output now includes: `FRAM 5 batches 255KB free (2% used)`

5. **FRAM Eviction Tracking (REQ-STOR-005)**
   - Eviction counter for oldest-batch deletions when storage full
   - Exposed via `getEvictionCount()` and STATS command
   - Implements FIFO oldest-first eviction policy

### v7.3 - Port 490 Module Registration

**Fix:** USBCaptureModule now receives ACK responses from KeylogReceiverModule

**Problem:**
- Module was registered for `TEXT_MESSAGE_APP` (port 1) via `SinglePortModule`
- ACKs from Heltec arrive on port 490
- XIAO received ACK packets but routing module claimed them, not USBCaptureModule
- Result: Infinite retries, "ACK timeout" every 60 seconds

**Solution:**
```cpp
// Before (broken)
SinglePortModule("USBCapture", meshtastic_PortNum_TEXT_MESSAGE_APP)

// After (fixed)
SinglePortModule("USBCapture", static_cast<meshtastic_PortNum>(USB_CAPTURE_PORTNUM))
```

**Trade-off:** Commands (STATUS, STATS, etc.) must now be sent on port 490, not as text messages.

### v7.2 - Single-Packet Batches + Auto-Broadcast

**Features:**
1. **Single-packet batches:** Reduced buffer from 500→200 bytes to fit LoRa packet
2. **Auto-broadcast:** Removed target node requirement - broadcasts immediately on channel 1
3. **Channel 0 default PSK:** Updated to `0x01` (AQ==) for public mesh compatibility

**Buffer Size Changes:**
| Component | Before | After |
|-----------|--------|-------|
| KEYSTROKE_BUFFER_SIZE | 500 bytes | 200 bytes |
| PSRAM_BUFFER_DATA_SIZE | 504 bytes | 200 bytes |
| MAX_DECODED_TEXT_SIZE | 600 bytes | 233 bytes |
| Data area (raw) | 480 bytes | 180 bytes |
| Decoded output | ~600 bytes (truncated) | ~230 bytes (fits packet) |

**Memory Impact:** RAM 24.9% → 24.4% (saved ~2.7KB)

### v7.1 - Receiver-Side Deduplication

**Feature:** Flash-persistent deduplication prevents duplicate storage when ACKs are lost

**Problem Solved:**
```
File BEFORE retransmit:            File AFTER retransmit (without dedup):
--- Batch 0x12345678 ---           --- Batch 0x12345678 ---
hello world                        hello world
                                   --- Batch 0x12345678 ---   ← DUPLICATE!
                                   hello world

With deduplication: Duplicate detected → ACK sent → no duplicate storage
```

**Architecture:**
```
┌─────────────────────────────────────────────────────────────┐
│ KeylogReceiverModule (Heltec V4)                            │
│                                                             │
│  handleReceived()                                           │
│       │                                                     │
│       ▼                                                     │
│  ┌────────────────┐    ┌──────────────────────────────────┐ │
│  │ isDuplicateBatch()├──► dedupCache[16 nodes]             │ │
│  └────────┬───────┘    │  ├─ nodeId                       │ │
│           │            │  ├─ lastAccessTime (LRU)         │ │
│      ┌────┴────┐       │  └─ recentBatchIds[16] (circular)│ │
│      ▼         ▼       └──────────────────────────────────┘ │
│   [NEW]    [DUPLICATE]                                      │
│      │         │                                            │
│      ▼         ▼                                            │
│   Store     Skip store ─────┐                               │
│      │         │            │                               │
│      ▼         ▼            ▼                               │
│   Record   sendAck()    sendAck()  ← ACK always sent       │
│      │                                                      │
│      ▼                                                      │
│  saveDedupCacheIfNeeded() ─► /keylogs/.dedup_cache         │
│  (debounced 30 sec)                                         │
└─────────────────────────────────────────────────────────────┘
```

**Data Structures:**
```cpp
// Per-node tracking (76 bytes each)
struct DedupNodeEntry {
    NodeNum nodeId;                           // 0 = empty slot
    uint32_t lastAccessTime;                  // For LRU eviction (seconds)
    uint32_t recentBatchIds[16];              // Circular buffer
    uint8_t nextIdx, count, padding[2];
};

// Flash file header (8 bytes)
struct DedupCacheHeader {
    uint16_t magic;     // 0xDEDC
    uint16_t version;   // 1
    uint32_t nodeCount;
};

// Total: 8 + (16 × 76) = 1,224 bytes in /keylogs/.dedup_cache
```

**Key Design Decisions:**
| Aspect | Choice | Rationale |
|--------|--------|-----------|
| Storage | Flash file | Survives reboots (user requirement) |
| Nodes | 16 max (LRU) | Supports >8 nodes (user requirement) |
| Batches/node | 16 circular | Handles burst retransmits |
| Save interval | 30 seconds | Reduces flash wear |
| ACK behavior | Always send | Sender must clear FRAM even on duplicate |

**NASA Power of 10 Compliance:**
- ✅ Rule 1: No recursion
- ✅ Rule 2: Fixed loop bounds (16 nodes, 16 batches)
- ✅ Rule 3: No dynamic allocation (fixed cache array)
- ✅ Rule 4: Assertions verify all assumptions
- ✅ Rule 5: Variables at smallest scope
- ✅ Rule 6: All return values checked
- ✅ Rule 7: Limited pointer dereferencing

**Boot Log:**
```
[KeylogReceiver] Loaded dedup cache from flash
[KeylogReceiver] Duplicate batch 0x12345678 from !a1b2c3d4 (already stored)
```

**Stats Output (includes duplicates):**
```json
{"status":"ok","command":"stats",...,"duplicates":5}
```

### v7.0 - Mesh Broadcast + Channel PSK

**Feature:** Mesh-wide broadcast with channel PSK encryption

**Architecture:**
```
XIAO RP2350 (Sender)              Mesh Network              Heltec V4 (Receiver)
┌──────────────────┐                                       ┌───────────────────┐
│ USBCaptureModule │── Broadcast (ch1, port 490) ────────> │ KeylogReceiverMod │
│ + ACK tracking   │              ↓ multi-hop              │ + Flash storage   │
│ + FRAM storage   │<── ACK Broadcast ─────────────────────│ /keylogs/<node>/  │
└──────────────────┘                                       └───────────────────┘
```

**Key Changes from v6.0:**
| Aspect | v6.0 | v7.0 |
|--------|------|------|
| Port | 256 (PRIVATE_APP) | 490 (custom) |
| Channel | 0 + PKI | 1 "takeover" + PSK |
| Mode | Direct message | Broadcast |
| Interval | 6 seconds | 5 minutes |
| RAM Fallback | 8-slot PSRAM (4KB) | MinimalBatchBuffer (2 slots) |

**Channel Configuration:**
```bash
# On both devices:
meshtastic --ch-set name "takeover" --ch-index 1
meshtastic --ch-set psk random --ch-index 1  # On sender, copy PSK to receiver
```

**Build:** Flash 58.5%, RAM 24.9%

**Bug Fixes (v7.0.1):**
- Fixed MinimalBatchBuffer assertion crash when FRAM succeeds
- Root cause: `minimal_buffer_init()` was only called on FRAM failure, but command handlers (STATUS, STATS, DUMP) always query buffer state
- Solution: Always initialize MinimalBatchBuffer regardless of FRAM status

### v6.0 - ACK-Based Reliable Transmission

**Feature:** Complete end-to-end reliable delivery with Heltec V4 base station

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
- **Avoids Flash Issue:** FRAM uses SPI, not flash, bypassing the multicore lockout bug

**Hardware Validation:**
```
[USBCapture] Initializing FRAM storage on SPI0, CS=GPIO1
FRAM: Found valid storage with 0 batches
[USBCapture] FRAM: Initialized (Mfr=0x04, Prod=0x4803)
[USBCapture] FRAM: 0 batches pending, 262128 bytes free
```

**Build:** Flash 58.3%, RAM 24.7%

### v4.2-dev - Multicore Lockout Investigation (RESOLVED)

**Original Issue:** Device hangs in `FSCom.remove()` when writing to flash after Core1 starts
**Root Cause:** SDK's `flash_safe_execute()` lockout mechanism had bugs on RP2350
**Resolution:** ✅ Fixed by updating to latest Arduino-Pico SDK with multicore lockout fix

**Note:** v5.0 FRAM storage also provides an alternative path that avoids flash writes for keystroke data entirely, using SPI instead.

### v4.1 - Filesystem Timeout Detection (Superseded)
**Issue:** Potential LittleFS hang during node database saves
**Solution:** Diagnostic timeout detection (logs operations >10s)
**Status:** Removed in v4.2-dev (not needed with proper debugging)

**Implementation:**
```cpp
// SafeFile.cpp - Timeout infrastructure
static volatile uint32_t g_fs_operation_start = 0;
static constexpr uint32_t FS_OPERATION_TIMEOUT_MS = 10000;

// Tracks open(), close(), testReadback(), renameFile()
if (checkFSOperationTimeout()) {
    LOG_ERROR("[SafeFile] FILESYSTEM TIMEOUT! Possible corruption.");
}
```

**Important:** This is diagnostic, NOT a true timeout - cannot interrupt blocking LittleFS calls.

**Build Impact:** Flash +2,272 bytes, RAM +4 bytes vs v4.0

### v4.0 - RTC Integration
**Feature:** Real unix epoch timestamps from mesh time sync

**Three-Tier Fallback System:**
1. **Priority 1:** RTC (RTCQualityFromNet or better) → Real unix epoch
2. **Priority 2:** BUILD_EPOCH + uptime → Pseudo-absolute time
3. **Priority 3:** Uptime only → Fallback behavior

**Hardware Validation Results:**
- ✅ BUILD_EPOCH fallback during boot (1765083600 + uptime)
- ✅ Mesh sync from Heltec V4 GPS → RTCQualityFromNet upgrade
- ✅ Quality transition: None(0) → Net(2) observed
- ✅ Real unix epoch timestamps (1765155817) transmitted

### v3.2 - Complete Multi-Core Flash Solution
**Issues Fixed:**
1. LittleFS freeze on node arrivals
2. Config save crash via CLI
3. Dim red LED reboot loop after config changes

**Root Causes:**
- Arduino-Pico FIFO conflict (can't pause Core1)
- Core1 executing from flash during Core0 flash writes
- Watchdog still armed during reboot

**Solution (3-part):**
1. **RAM Execution:** `CORE1_RAM_FUNC` macro for ~15 functions
2. **Memory Barriers:** `__dmb()` for cache coherency
3. **Manual Pause:** Volatile flags + hardware watchdog disable

**Results:** ✅ ALL ISSUES RESOLVED - Validated on hardware

---

## Data Structures

### Buffer Format (500 bytes per keystroke buffer)
```
[epoch:10][data:480][epoch:10]

Data encoding:
- Regular char: 1 byte (stored as-is)
- Tab: '\t', Backspace: '\b'
- Enter: 0xFF + 2-byte delta (3 bytes total)
- Modifiers: ^C (Ctrl+C), ~T (Alt+Tab), @L (GUI+L)
```

### PSRAM Buffer (4144 bytes total)
```
Header: 48 bytes
  - magic, write_index, read_index, buffer_count
  - Statistics: transmission_failures, buffer_overflows,
                psram_write_failures, retry_attempts

Slots[8]: 512 bytes each
  - start_epoch (4B), final_epoch (4B)
  - data_length (2B), flags (2B)
  - data[500] (keystroke data)
  - padding[4]
```

---

## TODO List

### 🔴 Critical Priority (v7.0 Testing)
1. [ ] Configure channel 1 "takeover" with matching PSK on both devices
2. [ ] Test mesh broadcast (verify multi-hop routing)
3. [ ] Test 5-minute interval (verify rate limiting)
4. [ ] Test MinimalBatchBuffer fallback (disable FRAM to verify)

### 🟠 High Priority
5. [ ] Add RGB LED status indicators for FRAM read/write/delete
6. [ ] Test canned messages from Heltec V4 (STATUS, STATS)
7. [ ] Validate mesh performance under heavy keystroke load

### 🟡 Medium Priority
8. [ ] Implement key release detection (currently press-only)
9. [ ] Make configuration runtime-adjustable (USB speed, channel, GPIO)

### 🔵 Future Enhancements
10. [ ] **Function Keys** - F1-F12, arrows, Page Up/Down, Home/End
11. [ ] **Web UI** - View keylogs from Heltec V4 flash storage

### ✅ Completed
- [x] **Core1 Health Monitoring** - Real-time health metrics with stall detection (v7.4)
- [x] **Protocol Versioning** - Magic marker header for backwards compatibility (v7.4)
- [x] **Command Authentication** - Optional AUTH:<token>:<command> for sensitive commands (v7.4)
- [x] **FRAM Capacity Alerts** - Threshold warnings at 75%, 90%, 99% usage (v7.4)
- [x] **FRAM Eviction Tracking** - Statistics on oldest-batch evictions (v7.4)
- [x] **ACK Reception Fix** - Module registered for port 490 to receive ACKs (v7.3)
- [x] **Deduplication** - Flash-persistent per-node batch tracking with LRU eviction (v7.1)
- [x] **Single-Packet Batches** - 200-byte buffer fits in single LoRa packet (v7.2)
- [x] **Mesh Broadcast** - Channel 1 PSK broadcast to mesh (v7.0)
- [x] **Port 490** - Custom private port (v7.0)
- [x] **Randomized TX Interval** - 40s-4min for traffic analysis resistance (v7.6)
- [x] **MinimalBatchBuffer** - NASA-compliant 2-slot RAM fallback (v7.0)
- [x] **Canned Messages** - STATUS/STATS via Heltec V4 LCD (v7.0)
- [x] **FRAM Migration** - 256KB non-volatile storage (v5.0)
- [x] **Reliable Transmission** - ACK-based with retry logic (v6.0→v7.0)
- [x] **KeylogReceiverModule** - Heltec V4 base station with flash storage (v6.0→v7.0)
- [x] **Simulation Mode** - Test without USB keyboard (v6.0)

---

## Performance Metrics

### Memory Usage (v7.3)
- **Total RAM:** 127,784 bytes (24.4% of 524,288)
- **Flash:** 918,032 bytes (58.5% of 1,568,768)
- **MinimalBatchBuffer:** ~1,048 bytes (header + 2×520 slots)

### CPU Usage
- **Core1:** 15-25% active, <5% idle
- **Core0:** ~0.2% (was 2% before v3.0)
- **Reduction:** 90% ✅

### Throughput
- **Max keystroke rate:** ~100 keys/sec (USB HID limited)
- **Buffer capacity:** 8 × 500 bytes = 4,000 bytes data
- **Transmission rate:** 1 buffer every 6 seconds (rate limited)

---

## Troubleshooting

### Build Fails
```bash
cd /Users/rstown/Desktop/ste/firmware
rm -rf .pio/build
pio run -e xiao-rp2350-sx1262
```

### No Keystrokes Captured
1. Check GPIO 16/17 connections (must be consecutive)
2. Try different USB speed (LOW ↔ FULL in `USBCaptureModule::init()`)
3. Check Core1 status codes in logs (0xC1-0xC4)
4. Verify PIO configuration succeeded (0xC3)

### System Crashes on Keystroke
- **Cause:** LOG_INFO from Core1 (not thread-safe)
- **Fix:** ✅ Fixed in v3.0 - removed all Core1 logging

### Reboot Loop (Dim Red LED)
- **Cause:** Watchdog armed during Core1 pause/exit
- **Fix:** ✅ Fixed in v3.4 - hardware register disable

### LittleFS Freeze on Node Arrivals
- **Cause:** Core1 executing from flash during Core0 flash writes
- **Fix:** ✅ Fixed in v3.2 - RAM execution + manual pause

### MinimalBatchBuffer Assertion Crash on Command
- **Cause:** `minimal_buffer_init()` not called when FRAM succeeds, but commands query buffer state
- **Symptom:** `assertion "g_minimal_buffer.header.magic == MINIMAL_BUFFER_MAGIC" failed` after STATUS/STATS/DUMP
- **Fix:** ✅ Fixed in v7.0.1 - always initialize MinimalBatchBuffer regardless of FRAM status

---

## Integration Points

### Meshtastic Module System
- Inherits from `concurrency::OSThread`
- `init()` - Called during system startup
- `runOnce()` - Called by scheduler every 100ms
- Sends via `MeshService::allocForSending()` and `Router::send()`

### Channel Configuration
- **Channel:** 1 ("takeover" - private encrypted)
- **Encryption:** AES256 with 32-byte PSK (platformio.ini)
- **Port:** TEXT_MESSAGE_APP (displays as text on receivers)

### Core1 Independence
- Launched via `multicore_launch_core1(usb_capture_core1_main, NULL)`
- Runs completely independently with own stack
- Uses volatile variables for config/control
- Watchdog: 4-second timeout (disabled during pause)

---

## Common Git Workflows

### Update from Upstream
```bash
git checkout master
git pull                          # Pulls from upstream/master
git checkout dev/usb-capture
git rebase master                 # Resolve conflicts if any
```

### Create Feature Branch
```bash
git checkout dev/usb-capture
git checkout -b feature/new-feature
# Develop feature
git checkout dev/usb-capture
git merge feature/new-feature
```

### View Your Changes
```bash
git log upstream/master..dev/usb-capture --oneline
git branch -vv
git log --oneline --graph --all -15
```

---

## Key Technical Decisions

### 1. PSRAM Buffer Instead of Queue
- **Old:** Core0 processed events one-by-one, formatted each, managed buffer
- **New:** Core1 writes complete buffers, Core0 just reads and transmits
- **Benefit:** 90% Core0 overhead reduction

### 2. Delta Encoding for Timestamps
- **Old:** 10 bytes per Enter (full epoch: "1733250000")
- **New:** 3 bytes per Enter (0xFF + 2-byte delta)
- **Benefit:** 70% space savings

### 3. No Logging from Core1
- **Issue:** LOG_INFO caused crashes (reboot on keystroke)
- **Root Cause:** Logging not thread-safe for multi-core
- **Solution:** Core1 silent, Core0 logs from PSRAM buffer

### 4. RAM Execution for Core1
- **Issue:** Flash writes by Core0 froze Core1 instruction fetch
- **Root Cause:** Arduino-Pico FIFO conflict, flash in write mode
- **Solution:** `CORE1_RAM_FUNC` macro for all Core1 code

---

## Success Criteria ✅

- ✅ Real-time keystroke capture with PIO hardware
- ✅ Dual-core architecture (Core1 independent)
- ✅ Lock-free communication with memory barriers
- ✅ Sub-millisecond latency
- ✅ Thread-safe operation (no crashes)
- ✅ 90% Core0 overhead reduction
- ✅ LoRa mesh transmission with rate limiting
- ✅ Real unix epoch timestamps from mesh sync
- ✅ Multi-core flash operations (no deadlocks)
- ✅ Clean reboots after config changes
- ✅ Comprehensive documentation
- ✅ Receiver-side deduplication (no duplicate storage on retransmits)
- ✅ Flash-persistent state (survives reboots)
- ✅ ACK reception and FRAM cleanup (v7.3 fix)

**Project Status:** Production Ready (v7.3) - End-to-End System with ACK

**Current State:**
- ✅ XIAO captures keystrokes → stores in FRAM → broadcasts via LoRa mesh
- ✅ Heltec V4 receives → checks dedup cache → stores to flash → sends ACK
- ✅ XIAO receives ACK → deletes batch from FRAM (v7.3 fix)
- ✅ Channel 1 PSK encryption (mesh-wide broadcast)
- ✅ Reliable delivery with retry on failure
- ✅ Duplicate detection survives reboots (flash-persistent)

---

## Important Notes for Claude Sessions

### ✅ RESOLVED: Previous Issues Fixed

**Filesystem Deadlock (2025-12-08):** Fixed by updating Arduino-Pico SDK
**PKI Encryption (2025-12-14):** Fixed by resetting flash to regenerate keys

### When Resuming Work
1. Read this file (CLAUDE.md) for complete project context
2. Check `git log -5` for recent commits
3. Check TODO section above for pending work

### Key Files
| Component | File |
|-----------|------|
| XIAO Sender | `src/modules/USBCaptureModule.cpp` |
| Heltec Receiver | `src/modules/KeylogReceiverModule.cpp` |
| FRAM Storage | `src/modules/FRAMBatchStorage.cpp` |
| USB Capture Core | `src/platform/rp2xx0/usb_capture/` |

### Build Commands
```bash
# XIAO RP2350 (sender)
pio run -e xiao-rp2350-sx1262

# Heltec V4 (receiver)
pio run -e heltec-v4
```

### PKI Troubleshooting
If direct messages fail with "PKC decrypt failed":
1. Reset flash on both devices: `meshtastic --factory-reset-device`
2. Let devices rediscover each other (exchange NodeInfo with public keys)
3. Send test DM from Meshtastic app to verify

---

## Contribution Guidelines

### When Ready for Upstream PR
1. Create GitHub fork of `meshtastic/firmware`
2. Push `dev/usb-capture` to your fork
3. Create PR: `meshtastic/firmware:master ← your-fork:dev/usb-capture`
4. Reference this documentation and test results

### Documentation Standards
- Update `USBCaptureModule_Documentation.md` for architecture changes
- Update version number and date in headers
- Document breaking changes with migration path
- Add code examples for new features

---

*Last Updated: 2025-12-14 | Version 7.6 | Hardware Validated: v3.2, v4.0, v5.0 (FRAM), v6.0 (ACK+PKI), v7.0 (Mesh Broadcast), v7.1 (Deduplication), v7.3 (ACK Reception), v7.6 (Randomized TX)*
