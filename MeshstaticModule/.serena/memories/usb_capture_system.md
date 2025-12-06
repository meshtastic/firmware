# USB Capture Module - Integration Context

## Module Identity
- **Version**: 2.0 (Production Ready)
- **Platform**: RP2350 (XIAO RP2350-SX1262)
- **Architecture**: Dual-core (Core1 capture, Core0 Meshtastic)
- **Location**: `firmware/src/platform/rp2xx0/usb_capture/`
- **Status**: ✅ Verified working, keystrokes captured successfully

## Purpose
Real-time USB HID keyboard capture using PIO (Programmable I/O) for transmission over Meshtastic LoRa mesh network.

## Dual Architecture Overview

### Core 1: USB Capture System (Independent)
**Location**: `firmware/src/platform/rp2xx0/usb_capture/`
**Purpose**: Hardware USB signal capture using PIO state machines
**Components** (10 files, 1873 lines):
1. `usb_capture_main.cpp/h` (386 lines) - Core1 controller and main loop
2. `usb_packet_handler.cpp/h` (399 lines) - Packet validation and processing
3. `keyboard_decoder_core1.cpp/h` (242 lines) - HID to ASCII conversion
4. `keystroke_queue.cpp/h` (246 lines) - Lock-free inter-core queue
5. `pio_manager.c/h` (244 lines) - PIO state machine management
6. `common.h` (167 lines) - Consolidated definitions
7. `okhi.pio.h` (189 lines) - Auto-generated PIO program

### Core 0: Meshtastic Integration
**Location**: `firmware/src/modules/USBCaptureModule.*`
**Purpose**: Meshtastic module wrapper for USB capture
**Components** (2 files, 255 lines):
- `USBCaptureModule.cpp` (190 lines) - Module integration
- `USBCaptureModule.h` (65 lines) - Module interface

## Hardware Configuration

### GPIO Pin Assignment (CRITICAL: Must be consecutive!)
- **GPIO 16**: USB D+ (data positive)
- **GPIO 17**: USB D- (data negative, must be DP+1)
- **GPIO 18**: START (PIO sync, must be DP+2)

### USB Speed Support
- **Low Speed**: 1.5 Mbps (most keyboards, current default)
- **Full Speed**: 12 Mbps (some keyboards)

### Physical Connections
```
USB Keyboard:
├─ D+ (Green)  → GPIO 16
├─ D- (White)  → GPIO 17
├─ GND (Black) → XIAO GND
└─ VBUS (Red)  → XIAO 5V (optional)
```

## Data Flow Architecture

```
USB Keyboard → GPIO 16/17 → PIO Hardware
  ↓
PIO FIFO (31-bit words)
  ↓
Core1: usb_capture_main (polling)
  ↓
Core1: usb_packet_handler (validation)
  ↓
Core1: keyboard_decoder (HID→ASCII)
  ↓
Lock-Free Queue (64 events × 32 bytes)
  ↓
Core0: USBCaptureModule (100ms polling)
  ↓
Meshtastic Logging (current)
  ↓
[TODO] LoRa Mesh Transmission
```

## Key Data Structures

### keystroke_event_t (32 bytes)
```cpp
typedef struct {
    keystroke_type_t type;           // Event type
    char character;                  // ASCII character
    uint8_t scancode;                // HID scancode
    uint8_t modifier;                // HID modifier byte
    uint64_t capture_timestamp_us;   // Capture time
    uint64_t queue_timestamp_us;     // Queue insertion time
    uint32_t processing_latency_us;  // Processing latency
    uint32_t error_flags;            // Error flags
} keystroke_event_t;
```

### keystroke_queue_t (2064 bytes)
- 64 event circular buffer (power of 2 for fast modulo)
- Lock-free single producer/consumer
- Overflow detection and counting
- Total memory: 2 KB queue + ~5 KB overhead = ~7 KB RAM

## Performance Characteristics

### Latency (End-to-End <1ms)
- USB → PIO FIFO: <10 µs (hardware)
- PIO → Core1: <50 µs (polling)
- Processing: ~150 µs (unstuffing + validation)
- Queue: <10 µs (lock-free)
- Core0 poll delay: up to 100ms (scheduled)

### Memory Footprint
- **RAM**: 23.1% (121,128 / 524,288 bytes)
  - Core1 stack: ~2 KB
  - Queue buffer: 2 KB
  - Raw packet buffer: 1 KB
  - Processing buffer: 128 bytes
- **Flash**: 56.2% (881,032 / 1,568,768 bytes)

### CPU Usage
- **Core1**: 15-25% active capture, <5% idle (micro-sleep optimization)
- **Core0**: <1% queue polling (every 100ms)

## Integration with MeshstaticModule

### Current State: Parallel Systems
- **USBCaptureModule**: Captures keystrokes → logs to console → [TODO] mesh transmission
- **MeshstaticModule**: (Independent) CSV batching → flash storage → [TODO] mesh transmission

### Integration Opportunity
Both modules capture keystroke data but use different approaches:

1. **USBCaptureModule** (Hardware-based):
   - PIO capture from GPIO pins
   - Real-time USB protocol processing
   - Lock-free queue to Core0
   - Direct keystroke events

2. **MeshstaticModule** (Abstraction-based):
   - Expects keystroke events from USB capture
   - CSV batching (200-byte limit)
   - Flash storage with LittleFS
   - Batch file management

### Integration Path
**MeshstaticModule can consume USBCaptureModule events:**
```cpp
// In MeshstaticModule::processKeystrokes():
while (keystroke_queue_available()) {
    keystroke_event_t event;
    if (keystroke_queue_pop(&event)) {
        // Add to meshstatic batch
        meshstatic_core1_add_keystroke(
            event.scancode,
            event.modifier,
            event.character,
            event.capture_timestamp_us
        );
    }
}
```

## Current Status (Dec 1, 2024)

### USB Capture Module ✅
- ✅ Core1 launches successfully
- ✅ PIO configuration working
- ✅ Keystrokes captured and logged
- ✅ Queue operations verified (zero drops)
- ✅ Memory stable, no leaks
- ⚠️ TODO: Mesh transmission not implemented

### Known Limitations
1. **Mesh Integration**: Keystrokes logged but not transmitted over LoRa
2. **Key Support**: A-Z, 0-9, symbols, Space, Enter, Backspace, Tab
3. **Not Supported**: F-keys, arrows, Home/End, numpad, Ctrl/Alt/GUI modifiers
4. **CRC Validation**: Disabled for performance (can be enabled if needed)

## Development History

### Phase 2: GPIO Conflict Resolution
- **Problem**: Device froze at `multicore_launch_core1()`
- **Cause**: GPIO 20/21 conflict with USB serial console
- **Solution**: Changed pins to GPIO 16/17
- **Result**: ✅ Core1 launches successfully

### Phase 3: Compilation Fix
- **Problem**: "stats functions not declared" build error
- **Cause**: C++ linkage mismatch with `stats.h` include
- **Solution**: Moved include inside `extern "C"` block
- **Result**: ✅ Build success, keystrokes captured

### Phase 4: File Organization (Current)
- **Goal**: Improve maintainability
- **Actions**: Renamed files, consolidated headers, merged related code
- **Results**: 17 → 12 files (-29%), build verified, functionality confirmed

## Next Steps

### Priority 1: Mesh Transmission (USBCaptureModule)
Implement keystroke transmission over LoRa mesh:
```cpp
void USBCaptureModule::sendKeystrokeMessage(const char *keys) {
    meshtastic_MeshPacket *p = allocDataPacket();
    p->channel = 1;  // "takeover" channel
    p->want_ack = false;
    p->decoded.payload.size = strlen(keys);
    memcpy(p->decoded.payload.bytes, keys, p->decoded.payload.size);
    service->sendToMesh(p);
}
```

### Priority 2: Integration with MeshstaticModule
Connect USB capture to CSV batching system:
- USBCaptureModule provides keystroke events
- MeshstaticModule batches into CSV format
- CSV batches saved to flash via LittleFS
- Flash batches transmitted over mesh network

## Configuration

### Compile-Time Options
```cpp
// Enable USB capture
#define XIAO_USB_CAPTURE_ENABLED

// USB speed selection (in USBCaptureModule::init())
capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_LOW);  // 1.5 Mbps

// GPIO pin configuration (in common.h)
#define DP_INDEX    16    // USB D+ (must be consecutive)
#define DM_INDEX    17    // USB D- (DP+1)
#define START_INDEX 18    // Sync (DP+2)

// Queue size (in keystroke_queue.h)
#define KEYSTROKE_QUEUE_SIZE 64  // Must be power of 2
```

## Core1 Status Codes
- `0xC1`: Core1 entry point reached
- `0xC2`: Starting PIO configuration
- `0xC3`: PIO configured successfully
- `0xC4`: Ready to capture USB data
- `0xDEADC1C1`: PIO configuration failed (error)

## Troubleshooting

### No Keystrokes Captured
1. Check USB speed setting (try LOW ↔ FULL)
2. Verify D+/D- wiring to GPIO 16/17
3. Check logs for Core1 status codes
4. Try different keyboard
5. Verify GPIO connections with multimeter

### Keystrokes Dropped
- Check `dropped_count` in queue stats
- Increase queue size if needed
- Reduce Core0 polling interval

### Build Issues
- Ensure `stats.h` inside `extern "C"` block
- Use `common.h` instead of old headers
- Verify consecutive GPIO pin configuration
