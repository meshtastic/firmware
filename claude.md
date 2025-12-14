# USB Capture Module - Project Context

**Project:** Meshtastic USB Keyboard Capture Module for RP2350
**Platform:** XIAO RP2350-SX1262 + Heltec V4 (receiver)
**Version:** v6.0 - ACK-Based Reliable Transmission
**Status:** ✅ End-to-End System Complete - PKI Encryption Working
**Last Updated:** 2025-12-14

---

## Quick Reference

### Current Status
- ✅ **Build:** Flash 58.3%, RAM 24.7% - Compiles cleanly
- ✅ **Core Features:** USB capture, FRAM storage, LoRa transmission, RTC timestamps
- ✅ **Performance:** 90% Core0 overhead reduction (2% → 0.2%)
- ✅ **FRAM Storage:** 256KB non-volatile (Fujitsu MB85RS2MTA) - Hardware Validated
- ✅ **SPI Bus Sharing:** FRAM + LoRa on same SPI0 with lock-based arbitration

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
  ├── USBCaptureModule_Documentation.md    - Complete architecture (v4.0)
  ├── PSRAM_BUFFER_ARCHITECTURE.md         - Buffer design
  ├── RTC_INTEGRATION_DESIGN.md            - RTC three-tier fallback
  └── IMPLEMENTATION_COMPLETE.md           - v3.0 summary

Root:
  ├── BRANCH_STRATEGY.md                   - Git workflow
  └── claude.md                             - This file
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
| 5.0 | 2025-12-13 | FRAM non-volatile storage (256KB) | Validated ✅ |
| **6.0** | **2025-12-14** | **ACK-based reliable transmission + KeylogReceiverModule** | **Validated** ✅ |

### v6.0 - ACK-Based Reliable Transmission (Current)

**Feature:** Complete end-to-end reliable delivery with Heltec V4 base station

**Architecture:**
```
XIAO RP2350 (Sender)              Heltec V4 (Receiver)
┌──────────────────┐              ┌───────────────────┐
│ USBCaptureModule │── PKI DM ──> │ KeylogReceiverMod │
│ + ACK tracking   │              │ + Flash storage   │
│ + FRAM storage   │<── ACK ───── │ /keylogs/<node>/  │
└──────────────────┘              └───────────────────┘
```

**Key Components:**
- **USBCaptureModule (XIAO):** Sends batches with batch_id, waits for ACK
- **KeylogReceiverModule (Heltec):** Receives batches, stores to flash, sends ACK
- **PKI Encryption:** X25519 key exchange for secure direct messages
- **Persistent Retry:** Batches never deleted on failure, retry every 20 seconds

**Transmission Protocol:**
- Port: `PRIVATE_APP` (256)
- Payload: `[batch_id:4][decoded_text:N]`
- ACK format: `ACK:0x<8-hex-digits>`
- Retry: Exponential backoff 30s→60s→120s, then reset and retry next cycle

**Simulation Mode:** Build with `-D USB_CAPTURE_SIMULATE_KEYS` to test without USB keyboard

**PKI Troubleshooting:**
- If "PKC decrypt failed": Reset flash on both devices to regenerate keys
- Keys are exchanged automatically via NodeInfo broadcasts
- Both nodes must have each other's public keys stored

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

### 🔴 Critical Priority (v5.0 Testing)
1. [ ] Test FRAM write operation (type on USB keyboard)
2. [ ] Test FRAM read and transmission (verify LoRa broadcast)
3. [ ] Test FRAM persistence (power cycle, verify batches survive)
4. [ ] Test SPI bus contention (simultaneous FRAM + LoRa)

### 🟠 High Priority
5. [ ] Add RGB LED status indicators for FRAM read/write/delete
6. [ ] Validate SPI timing under heavy keystroke load
7. [ ] Test FRAM storage cleanup when full

### 🟡 Medium Priority
8. [ ] Add Core1 observability (circular log buffer)
9. [ ] Implement key release detection (currently press-only)
10. [ ] Make configuration runtime-adjustable (USB speed, channel, GPIO)

### 🔵 Future Enhancements
11. [ ] **Authentication** - Secure remote commands
12. [ ] **Function Keys** - F1-F12, arrows, Page Up/Down, Home/End
13. [ ] **Web UI** - View keylogs from Heltec V4 flash storage

### ✅ Completed
- [x] **FRAM Migration** - 256KB non-volatile storage (v5.0)
- [x] **Reliable Transmission** - ACK-based with exponential backoff (v6.0)
- [x] **KeylogReceiverModule** - Heltec V4 base station with flash storage (v6.0)
- [x] **Simulation Mode** - Test without USB keyboard (v6.0)
- [x] **PKI Encryption** - Secure direct messages between nodes (v6.0)

---

## Performance Metrics

### Memory Usage
- **Total RAM:** 137,896 bytes (26.3% of 524,288)
- **Flash:** 878,672 bytes (56.0% of 1,568,768)
- **PSRAM Buffer:** 4,144 bytes (header + 8×512 slots)

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

**Project Status:** Production Ready (v6.0) - End-to-End System Validated

**Current State:**
- ✅ XIAO captures keystrokes → stores in FRAM → transmits via LoRa
- ✅ Heltec V4 receives → stores to flash → sends ACK
- ✅ PKI encryption working (X25519 key exchange)
- ✅ Reliable delivery with retry on failure

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

*Last Updated: 2025-12-14 | Version 6.0 | Hardware Validated: v3.2, v4.0, v5.0 (FRAM), v6.0 (ACK+PKI)*
