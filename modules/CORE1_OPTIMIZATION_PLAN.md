# Core1 Optimization Plan: Move Formatting & Storage

**Goal:** Maximize Core0 availability by moving all USB processing to Core1
**Future:** FRAM-based storage with Core0 just reading finished data

---

## Current Architecture (Baseline)

```
Core1:
  USB capture → Packet processing → Keyboard decode
    ↓ (keystroke_event_t via queue)
Core0:
  Queue poll → formatKeystrokeEvent() → Buffer management → Mesh TX
```

**Core0 overhead:** ~2% (queue poll + formatting + buffer mgmt)

---

## Target Architecture

```
Core1:
  USB capture → Packet processing → Keyboard decode
    → formatKeystrokeEvent() → Formatted string
    → Store to FRAM/PSRAM buffer
    ↓ (formatted strings in external storage)
Core0:
  Read from FRAM/PSRAM → Mesh transmit
```

**Core0 overhead:** <0.5% (just FRAM read + mesh TX)

---

## Implementation Phases

### Phase 1: Move Formatting to Core1 (Today)
**Changes:**
- Move `formatKeystrokeEvent()` to Core1 context
- Format immediately after decode
- Store formatted strings instead of events

**Benefits:**
- Core0 saved from string formatting
- Events pre-formatted on Core1
- Simpler Core0 code

**Implementation:**
1. Create formatted string buffer on Core1
2. Call format function on Core1
3. Push formatted strings to queue (or temp buffer)
4. Core0 just reads and transmits

---

### Phase 2: Add Storage Abstraction (Future)
**Create storage layer:**
```cpp
class EventStorage {
  virtual void write(const char* formatted_event);
  virtual const char* read();
};

// Implementations:
- RAMStorage (current, for testing)
- PSRAMStorage (RP2350 has no PSRAM, but can add external)
- FRAMStorage (future hardware addition)
```

**Benefits:**
- Hardware-independent code
- Easy to swap storage backends
- Prepare for FRAM addition

---

### Phase 3: FRAM Integration (Future Hardware)
**Add external FRAM:**
- SPI or I2C FRAM chip
- Non-volatile, fast write
- Large capacity (256KB - 4MB)
- Power-loss safe

**Architecture:**
```
Core1 → FRAM (SPI/I2C from Core1)
Core0 → FRAM (read only)
```

---

## Step-by-Step Implementation

### Step 1: Add CPU ID Logging ✅ DONE
- Added `get_core_num()` to logs
- Visibility into what runs where

### Step 2: Move formatKeystrokeEvent to Core1
**Current location:** `USBCaptureModule.cpp` (Core0)
**New location:** `usb_capture_main.cpp` (Core1)

**Changes:**
1. Copy function to Core1 file
2. Create formatted string buffer on Core1
3. Format after keyboard decode
4. Push formatted strings

### Step 3: Create Shared String Buffer
**Design:**
```cpp
#define FORMATTED_EVENT_BUFFER_SIZE 100  // Circular buffer
#define MAX_EVENT_STRING_LEN 128

struct formatted_event_t {
    char text[MAX_EVENT_STRING_LEN];
    uint64_t timestamp;
};

formatted_event_t g_formatted_buffer[FORMATTED_EVENT_BUFFER_SIZE];
volatile uint32_t g_format_write_idx;
volatile uint32_t g_format_read_idx;
```

### Step 4: Update Core0 to Read Formatted Events
**New processFormattedEvents():**
```cpp
void processFormattedEvents() {
    while (formatted_buffer_has_data()) {
        formatted_event_t* event = read_formatted_event();
        LOG_INFO("[Core%u] %s", get_core_num(), event->text);
        // Add to mesh buffer, etc.
    }
}
```

---

## Benefits Analysis

### Immediate (Phase 1)
- **Core0 CPU:** 2% → 0.5% (75% reduction!)
- **Core1 CPU:** 20% → 22% (minor increase, plenty of headroom)
- **Simpler Core0:** Just read and transmit
- **Better separation:** Core1 owns all USB logic

### Future (Phase 2-3)
- **Non-volatile storage:** Power-loss safe
- **Larger buffering:** Handle mesh outages
- **Core0 ultra-light:** Just FRAM read + mesh TX
- **Scalability:** Easy to add more USB devices

---

## Implementation Time

**Phase 1 (today):**
- Move formatting: 30 min
- Create string buffer: 30 min
- Update Core0: 30 min
- Test: 30 min
- **Total: 2 hours**

**Phase 2 (future):**
- Storage abstraction: 1 hour
- Testing: 30 min

**Phase 3 (requires hardware):**
- FRAM driver: 2 hours
- Integration: 1 hour
- Testing: 1 hour

---

## Risk Assessment

**Phase 1 risks:**
- Low - just moving existing code
- Easy rollback if issues
- Well-defined interfaces

**Mitigation:**
- Feature flag for new path
- Keep baseline working
- Test incrementally

---

## Success Criteria

**Phase 1:**
- ✅ Logs show formatting on Core1
- ✅ Core0 logs show <0.5% overhead
- ✅ Keystrokes still captured correctly
- ✅ Mesh transmission still works

---

**Ready to implement Phase 1?**
