# Next Session Handoff: Core1 Formatting Implementation

**Current Branch:** `feature/core1-formatting`
**Status:** Ready to implement
**Estimated Time:** 2 hours
**Priority:** HIGH - Real optimization with clear benefits

---

## Context

After 10 hours of exploration today, we discovered:
- ❌ PIO bit unstuffing: Impractical for keyboard capture
- ✅ **Better idea:** Move formatting to Core1 + prepare for FRAM

This optimization is **much more valuable** than hardware unstuffing!

---

## What to Implement

### **Goal**
Move event formatting from Core0 to Core1, preparing for future FRAM storage.

### **Current Architecture**
```
Core1: Capture → Decode → Push keystroke_event_t to queue
Core0: Poll queue → Format event → Buffer → Mesh TX
```
**Core0 overhead:** ~2%

### **Target Architecture**
```
Core1: Capture → Decode → Format event → Push formatted string
Core0: Read formatted string → Mesh TX
```
**Core0 overhead:** ~0.5% (75% reduction!)

---

## Implementation Steps

### Step 1: Create Formatted String Queue (30 min)

**New file:** `formatted_event_queue.h/.cpp`

```cpp
#define FORMATTED_QUEUE_SIZE 64
#define MAX_FORMATTED_LEN 128

typedef struct {
    char text[MAX_FORMATTED_LEN];
    uint64_t timestamp_us;
    uint8_t core_id;  // Track which core formatted it
} formatted_event_t;

typedef struct {
    formatted_event_t events[FORMATTED_QUEUE_SIZE];
    volatile uint32_t write_index;
    volatile uint32_t read_index;
} formatted_event_queue_t;

// Lock-free queue operations (same pattern as keystroke_queue)
void formatted_queue_init(formatted_event_queue_t *queue);
bool formatted_queue_push(formatted_event_queue_t *queue, const formatted_event_t *event);
bool formatted_queue_pop(formatted_event_queue_t *queue, formatted_event_t *event);
```

---

### Step 2: Move Format Function to Core1 (30 min)

**Copy `formatKeystrokeEvent()` to `usb_capture_main.cpp`:**

```cpp
// In usb_capture_main.cpp (Core1)
static void format_keystroke_core1(
    const keystroke_event_t *event,
    char *buffer,
    size_t buffer_size)
{
    // Exact same logic as USBCaptureModule::formatKeystrokeEvent
    switch (event->type) {
        case KEYSTROKE_TYPE_CHAR:
            snprintf(buffer, buffer_size,
                    "CHAR '%c' (scancode=0x%02x, mod=0x%02x)",
                    event->character, event->scancode, event->modifier);
            break;
        // ... rest of cases
    }
}
```

---

### Step 3: Integrate into Core1 Flow (30 min)

**After keyboard decode in Core1:**

```cpp
// In keyboard_decoder_core1_process_report():
// After creating keystroke_event_t:

formatted_event_t formatted;
format_keystroke_core1(&event, formatted.text, sizeof(formatted.text));
formatted.timestamp_us = event.capture_timestamp_us;
formatted.core_id = 1;  // Formatted on Core1

formatted_queue_push(g_formatted_queue, &formatted);
```

---

### Step 4: Update Core0 to Read Formatted (30 min)

**In `USBCaptureModule::runOnce()`:**

```cpp
// OLD: Poll keystroke_queue, format, then log
// NEW: Poll formatted_queue, just log

formatted_event_t formatted;
while (formatted_queue_pop(formatted_queue, &formatted)) {
    LOG_INFO("[Core%u→Core%u] %s",
            formatted.core_id,  // Who formatted (Core1)
            get_core_num(),     // Who's logging (Core0)
            formatted.text);

    // Add to keystroke buffer for mesh transmission
    // (existing buffer logic unchanged)
}
```

---

## File Changes Summary

**New files:**
- `formatted_event_queue.h` (~150 lines)
- `formatted_event_queue.cpp` (~100 lines)

**Modified files:**
- `usb_capture_main.cpp` - Add format function, use formatted queue
- `keyboard_decoder_core1.cpp` - Call format after decode
- `USBCaptureModule.cpp` - Read from formatted queue instead
- `USBCaptureModule.h` - Add formatted queue pointer

**Total:** ~400 lines new/modified

---

## Testing Plan

**Test 1: Verify Core IDs**
```
Expected: [Core1→Core0] CHAR 'h' ...
                ^^^^      ^^^^
              Formatted  Logged
```

**Test 2: Verify Functionality**
- Keystrokes still captured ✓
- Formatting correct ✓
- Mesh transmission works ✓

**Test 3: Performance**
- Measure Core0 CPU (should drop from 2% to 0.5%)
- Verify Core1 headroom (should stay <30%)

---

## Future: FRAM Integration

**Once Phase 1 works, add storage abstraction:**

```cpp
class EventStorage {
    virtual void write_formatted(const char* text);
    virtual const char* read_formatted();
};

// Implementations:
RAMStorage    // Current (testing)
FRAMStorage   // Future (I2C/SPI FRAM chip)
PSRAMStorage  // If external PSRAM added
```

**Then:**
- Core1 writes to FRAM directly
- Core0 reads from FRAM on demand
- Non-volatile, power-safe storage
- Much larger buffering capacity

---

## Quick Start for Next Session

```bash
# 1. Switch to feature branch
git checkout feature/core1-formatting

# 2. Verify clean state
git status

# 3. Create formatted_event_queue files
# Start with header, then implementation

# 4. Copy format function to Core1

# 5. Integrate and test
```

---

## Expected Outcome

**After 2 hours:**
- ✅ Event formatting on Core1
- ✅ Core0 just reads pre-formatted strings
- ✅ 75% Core0 overhead reduction
- ✅ Foundation for FRAM storage

**This is a REAL improvement** with measurable benefits!

---

**Current Commit:** `e142389` - Plan documented
**Next Commit:** Create formatted_event_queue files

**Ready to implement when you are!** 🚀
