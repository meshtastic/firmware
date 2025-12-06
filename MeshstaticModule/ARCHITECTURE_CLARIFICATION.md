# 🏗️ Meshstatic Module - Dual-Core Architecture Clarification

**CRITICAL DESIGN UPDATE**: Understanding RP2350 dual-core separation

---

## ⚠️ Architecture Correction

### The Reality: Two Cores, Two Worlds

```
RP2350 Dual-Core Architecture
│
├── Core 0 (Arduino/Meshtastic)
│   ├── Meshtastic firmware runs here
│   ├── OSThread scheduler runs here
│   ├── MeshstaticModule::runOnce() runs here
│   ├── LoRa transmission runs here
│   ├── Serial/logging runs here
│   └── **Reads** from keystroke queue (Core1 → Core0)
│
└── Core 1 (PIO USB Capture) ⭐ THIS IS WHERE MESHSTATIC BATCHING HAPPENS
    ├── PIO program runs here (okhi.pio)
    ├── capture_controller_core1_main_v2() runs here
    ├── USB packet decoding runs here
    ├── **Meshstatic batching runs here** ⭐
    ├── **Flash writes happen here** ⭐
    └── **Writes** to keystroke queue (Core1 → Core0)
```

---

## 🎯 Correct Integration Points

### Core 1 (Where Meshstatic Actually Runs)

**Location**: `/Users/rstown/Desktop/Projects/STE/client_pico/lib/USBCapture/capture_v2.cpp`

**Function**: `capture_controller_core1_main_v2()`

**Integration**:

```cpp
#include "meshstatic_core1.h"

void capture_controller_core1_main_v2(void)
{
    // ... initialization code ...

    // ⭐ INITIALIZE MESHSTATIC ON CORE 1
    meshstatic_core1_init();

    while (g_capture_running_v2)
    {
        // ... PIO FIFO reading ...
        // ... packet processing ...
        // ... keyboard decoding ...

        // After successful keystroke decode:
        if (keystroke_valid) {
            // Push to Core0 queue (existing)
            keystroke_queue_push(g_keystroke_queue_v2, &event);

            // ⭐ ALSO ADD TO MESHSTATIC BATCH (CORE 1)
            meshstatic_core1_add_keystroke(
                event.scancode,
                event.modifier,
                event.character,
                event.timestamp_us
            );

            // Meshstatic will:
            // 1. Add to CSV batch
            // 2. Check if batch full (200 bytes)
            // 3. If full → Save to flash (on Core 1!)
            // 4. Create new batch
        }

        // Check auto-flush (10 second timeout)
        meshstatic_core1_check_auto_flush(time_us_64());
    }

    // ⭐ SHUTDOWN MESHSTATIC
    meshstatic_core1_shutdown();  // Flush remaining data
}
```

### Core 0 (Meshtastic/OSThread)

**The MeshstaticModule on Core 0 is for:**
1. **Status monitoring** (read statistics from Core 1)
2. **Transmission coordination** (Component 4 - future)
3. **Configuration management** (enable/disable, settings)

**NOT for:**
- ❌ Keystroke capture (happens on Core 1)
- ❌ Batch creation (happens on Core 1)
- ❌ Flash writes (happens on Core 1)

---

## 🔄 Updated Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│ CORE 1 (Dedicated USB Capture + Meshstatic Batching)       │
│                                                             │
│  PIO Hardware Capture (okhi.pio)                           │
│         ↓                                                   │
│  Packet Processor                                           │
│         ↓                                                   │
│  Keyboard Decoder                                           │
│         ↓                                                   │
│  ┌─────────────────────┬─────────────────────┐            │
│  │                     │                     │            │
│  │  Lock-Free Queue    │  MESHSTATIC MODULE  │            │
│  │  (Core1→Core0)      │  (Core1 Batching)   │            │
│  │                     │                     │            │
│  │  └→ Keystroke       │  1. Add to batch    │            │
│  │     event pushed    │  2. Check if full   │            │
│  │                     │  3. Save to flash   │            │
│  │                     │  4. Reset batch     │            │
│  └─────────────────────┴─────────────────────┘            │
│         │                       ↓                          │
│         │               Flash Storage                      │
│         │               /meshstatic/batch_XXXXX.csv        │
│         │                                                   │
└─────────┼───────────────────────────────────────────────────┘
          │
          │ Lock-Free Queue
          ↓
┌─────────────────────────────────────────────────────────────┐
│ CORE 0 (Meshtastic Firmware)                               │
│                                                             │
│  Main Loop (Arduino)                                        │
│         ↓                                                   │
│  OSThread Scheduler                                         │
│         ↓                                                   │
│  MeshstaticModule::runOnce() (every 100ms)                 │
│         ↓                                                   │
│  ┌─────────────────────────────────────────┐              │
│  │  Monitor Core1 Statistics                │              │
│  │  Check transmission queue (future)       │              │
│  │  Coordinate with mesh network (future)   │              │
│  └─────────────────────────────────────────┘              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 🎯 Core Separation of Concerns

### Core 1 Responsibilities (USB Capture Thread)

```c
// capture_controller_core1_main_v2() in capture_v2.cpp

✅ PIO USB packet capture (hardware)
✅ Packet validation and decoding
✅ Keystroke event creation
✅ Push to Core0 queue (for LoRa transmission)
✅ MESHSTATIC: Add to CSV batch
✅ MESHSTATIC: Flush batch to flash when full
✅ MESHSTATIC: Auto-flush on timeout
```

### Core 0 Responsibilities (Meshtastic Thread)

```cpp
// MeshstaticModule::runOnce() in MeshstaticModule.cpp

✅ OSThread scheduling (100ms periodic)
✅ Statistics monitoring (read from Core 1)
✅ Configuration management
✅ FUTURE: Transmission coordination
✅ FUTURE: Mesh packet handling
✅ FUTURE: Batch cleanup after transmission
```

---

## 🔧 Updated Module Design

### MeshstaticModule (Core 0) - Revised Purpose

**Primary Role**: **Coordinator and monitor**, NOT capture processor

```cpp
class MeshstaticModule : private concurrency::OSThread
{
  protected:
    virtual int32_t runOnce() override {
        // Option 1: Monitor statistics from Core 1
        // (Core 1 updates global stats struct)

        // Option 2: Coordinate transmission (Component 4)
        // if (shouldTransmit()) {
        //     transmitNextBatch();
        // }

        // Option 3: Configuration management
        // if (configChanged()) {
        //     updateCore1Settings();
        // }

        return RUN_SAME;  // 100ms interval
    }
};
```

### meshstatic_core1.c (Core 1) - The Real Worker

**Primary Role**: **Capture, batch, and store** - all on Core 1

```c
// This code runs on Core 1 in capture_controller_core1_main_v2()

void capture_controller_core1_main_v2(void) {
    meshstatic_core1_init();  // Initialize on Core 1

    while (g_capture_running) {
        // PIO capture...
        // Decode...

        // Add to meshstatic batch (ON CORE 1!)
        meshstatic_core1_add_keystroke(scancode, modifier, character, timestamp_us);

        // This function will:
        // - Add to batch
        // - Check if full
        // - Save to flash (ON CORE 1!)
        // - Reset batch

        // All flash I/O happens on Core 1!
    }

    meshstatic_core1_shutdown();  // Flush on Core 1
}
```

---

## ⚡ Inter-Core Communication

### Core 1 → Core 0 (Existing)

```c
// Lock-free queue for keystroke events
keystroke_queue_t *g_keystroke_queue;

// Core 1 writes:
keystroke_queue_push(queue, &event);

// Core 0 reads:
keystroke_event_t event;
keystroke_queue_pop(queue, &event);
```

### Core 1 → Core 0 (Meshstatic Statistics) - OPTIONAL

If Core 0 needs real-time stats from Core 1:

```c
// Shared statistics struct (volatile for multi-core access)
volatile meshstatic_core1_stats_t g_meshstatic_stats_shared;

// Core 1 writes (after each batch save):
g_meshstatic_stats_shared.batches_saved++;

// Core 0 reads (in MeshstaticModule::runOnce()):
uint32_t batches = g_meshstatic_stats_shared.batches_saved;
```

---

## 🎮 Correct Execution Model

### Core 1: Continuous Processing Loop

```
capture_controller_core1_main_v2() - INFINITE LOOP
├── Initialize PIO
├── Initialize meshstatic_core1
└── while (g_capture_running) {
        ├── Read PIO FIFO (hardware)
        ├── Decode keystroke
        ├── Push to Core0 queue
        ├── meshstatic_core1_add_keystroke() ⭐
        │   ├── Add to CSV batch
        │   ├── Check if batch full
        │   └── Save to flash (if full)
        └── Check auto-flush timeout
    }
```

### Core 0: Periodic Monitoring

```
MeshstaticModule::runOnce() - CALLED EVERY 100ms
├── Monitor Core 1 statistics (optional)
├── Check transmission queue (Component 4)
├── Log periodic stats (every 60s)
└── return RUN_SAME;
```

---

## 🔨 Corrected Integration

### Where Meshstatic Code Actually Runs

**99% on Core 1**:
- ✅ `meshstatic_core1_init()` - Core 1
- ✅ `meshstatic_core1_add_keystroke()` - Core 1
- ✅ `meshstatic_batch_add()` - Core 1
- ✅ `meshstatic_storage_save_batch()` - Core 1
- ✅ Flash I/O operations - Core 1
- ✅ Auto-flush checks - Core 1

**1% on Core 0**:
- ✅ `MeshstaticModule::runOnce()` - Core 0 (monitoring only)
- ✅ Statistics logging - Core 0
- ✅ FUTURE: Transmission - Core 0

---

## 📝 Updated Integration Steps

### Step 1: Integrate with Core 1 (Primary Integration)

**File**: `client_pico/lib/USBCapture/capture_v2.cpp`

```cpp
#include "meshstatic_core1.h"

void capture_controller_core1_main_v2(void) {
    // ... existing initialization ...

    // Initialize meshstatic on Core 1
    meshstatic_core1_init();

    while (g_capture_running_v2) {
        // ... existing PIO capture code ...

        // After keyboard_decoder_core1_process_packet():
        if (keystroke_valid) {
            // Push to Core0 queue (existing)
            keystroke_queue_push(g_keystroke_queue_v2, &decoded_event);

            // Add to meshstatic batch (NEW - runs on Core 1!)
            meshstatic_core1_add_keystroke(
                decoded_event.scancode,
                decoded_event.modifier,
                decoded_event.character,
                decoded_event.timestamp_us
            );

            // Batching and flash writes happen inline on Core 1
        }

        // Check auto-flush every loop iteration
        meshstatic_core1_check_auto_flush(time_us_64());
    }

    // Shutdown - flush remaining data
    meshstatic_core1_shutdown();
}
```

### Step 2: Add Meshtastic Module (Optional Monitoring)

**File**: `firmware/src/modules/Modules.cpp`

```cpp
// This module is OPTIONAL - only needed for:
// - Statistics monitoring from Core 0
// - Future transmission coordination

#if defined(ARCH_RP2040) && defined(HW_VARIANT_RPIPICO2)
    meshstaticModule = new MeshstaticModule();  // Runs on Core 0
#endif
```

---

## 🎯 Why This Matters

### Problem with Original Design

❌ **Wrong**: Meshtastic module on Core 0 trying to capture keystrokes
- Core 0 doesn't have access to PIO FIFO
- Core 0 doesn't run the USB capture loop
- Core 0 would need to poll Core 1 (inefficient)

### Correct Design

✅ **Right**: Meshstatic batching runs directly on Core 1
- Core 1 has direct access to decoded keystrokes
- Core 1 can do flash I/O without blocking Core 0
- Core 0 only monitors statistics (optional)

---

## 🔄 Execution Timeline

### Boot Sequence

```
Time 0: Meshtastic starts on Core 0
    ↓
Time 1: setupModules() creates MeshstaticModule (Core 0)
    ↓
Time 2: Core 0 launches Core 1 thread
    ↓
Time 3: capture_controller_core1_main_v2() starts on Core 1
    ↓
Time 4: meshstatic_core1_init() on Core 1 ⭐
    ↓
Time 5: Both cores running in parallel
```

### Runtime

```
Core 0 (Meshtastic):                    Core 1 (USB Capture + Meshstatic):
Every 100ms:                            Continuous loop:
├── MeshstaticModule::runOnce()         ├── Read PIO FIFO
│   ├── Log stats (every 60s)           ├── Decode keystroke
│   └── return RUN_SAME                 ├── Push to Core0 queue
│                                       ├── meshstatic_core1_add_keystroke() ⭐
Every 1s:                               │   ├── Add to CSV batch
├── Read keystroke queue                │   ├── Check if full (200 bytes)
├── Transmit via LoRa                   │   └── Save to flash (if full) ⭐
└── Display on OLED                     │
                                        └── Check auto-flush timeout
```

---

## 🎮 Component Placement

### Component 1: Batch Manager
**Runs on**: Core 1 ⭐
**Called by**: `meshstatic_core1_add_keystroke()` (Core 1)
**Flash I/O**: Core 1

### Component 2: Storage Manager
**Runs on**: Core 1 ⭐
**Called by**: `meshstatic_batch_is_full()` (Core 1)
**Flash I/O**: Core 1

### Component 3: Core 1 Controller
**Runs on**: Core 1 ⭐
**Called by**: `capture_controller_core1_main_v2()` (Core 1)
**Flash I/O**: Core 1

### Meshtastic Module Wrapper
**Runs on**: Core 0
**Purpose**: Statistics monitoring, future transmission
**Does NOT**: Capture keystrokes or write to flash

---

## 📋 Integration Checklist (Corrected)

### Mandatory (Core 1 Integration)

- [x] Copy meshstatic_core1.{h,c} to project
- [x] Copy meshstatic_batch.{h,c} to project
- [x] Copy meshstatic_storage.{h,c} to project
- [ ] Add `#include "meshstatic_core1.h"` to capture_v2.cpp
- [ ] Call `meshstatic_core1_init()` in Core 1 startup
- [ ] Call `meshstatic_core1_add_keystroke()` after keystroke decode
- [ ] Call `meshstatic_core1_check_auto_flush()` in Core 1 loop
- [ ] Call `meshstatic_core1_shutdown()` before Core 1 exit

### Optional (Core 0 Monitoring)

- [ ] Copy MeshstaticModule.{h,cpp} to firmware/src/modules/
- [ ] Add to Modules.cpp setupModules()
- [ ] Use for statistics monitoring
- [ ] Use for future transmission coordination

---

## 🚨 Critical Understanding

### The Meshstatic module has TWO parts:

1. **Core 1 Worker** (meshstatic_core1.{h,c}) - **MANDATORY**
   - This is where capture/batch/save happens
   - Runs in `capture_controller_core1_main_v2()`
   - Directly integrated with USB capture
   - Does flash I/O on Core 1

2. **Core 0 Monitor** (MeshstaticModule.{h,cpp}) - **OPTIONAL**
   - This is Meshtastic OSThread wrapper
   - Runs on Core 0 for monitoring
   - Only needed for stats/transmission
   - Does NOT capture keystrokes

---

## 🎯 Minimum Viable Integration

### For Just Capture + Batch + Save (No Meshtastic Module Needed)

```cpp
// In capture_v2.cpp (Core 1)

#include "meshstatic_core1.h"

void capture_controller_core1_main_v2(void) {
    meshstatic_core1_init();  // Initialize on Core 1

    while (g_capture_running_v2) {
        // ... PIO capture ...

        if (keystroke_valid) {
            // Add to meshstatic (Core 1)
            meshstatic_core1_add_keystroke(scancode, modifier, character, timestamp_us);
        }
    }

    meshstatic_core1_shutdown();
}
```

**That's it!** Meshstatic now works completely on Core 1.

---

## 📖 Updated Documentation

### Key Documents to Read

1. **This file** (ARCHITECTURE_CLARIFICATION.md) - Understand dual-core
2. **MESHTASTIC_INTEGRATION.md** - Integration steps (now updated mentally)
3. **FINAL_SUMMARY.md** - Complete reference

### Mental Model Update

**Before**: "Meshtastic module processes keystrokes on Core 0"
**After**: "Meshstatic batching runs on Core 1, Meshtastic module optionally monitors from Core 0"

---

## ✅ Correct Design Summary

| Aspect | Core 0 (Meshtastic) | Core 1 (USB Capture) |
|--------|---------------------|----------------------|
| **Role** | Monitor & transmit | Capture & batch |
| **Meshstatic Code** | Optional monitoring | **Primary worker** ⭐ |
| **Keystroke Capture** | ❌ No | ✅ Yes |
| **CSV Batching** | ❌ No | ✅ Yes |
| **Flash Writes** | ❌ No | ✅ Yes |
| **Statistics** | ✅ Read-only | ✅ Read-write |
| **Transmission** | ✅ Future | ❌ No |

---

## 🎊 Final Understanding

**Meshstatic module = Core 1 worker + optional Core 0 monitor**

- **Core 1**: Does the real work (capture, batch, save) ⭐
- **Core 0**: Optional monitoring and future transmission

The module is **already designed correctly** for Core 1 execution!
Just integrate `meshstatic_core1.{h,c}` into `capture_v2.cpp` and you're done!

The Meshtastic wrapper (`MeshstaticModule.{h,cpp}`) is optional for monitoring.

---

**Status**: ✅ Architecture clarified and correct! 🎯
