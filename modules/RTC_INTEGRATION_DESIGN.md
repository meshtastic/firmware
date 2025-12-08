# RTC Integration Design for USB Capture Module

**Version:** v4.0 Design
**Date:** 2025-12-07
**Status:** Design Phase - Ready for Implementation

---

## Overview

Replace uptime-based timestamps (`millis() / 1000`) with real unix epoch timestamps from Meshtastic's RTC system, with graceful fallback to BUILD_EPOCH when RTC is not available.

---

## Current Implementation (v3.5)

### Timestamp Source
```cpp
// keyboard_decoder_core1.cpp:431
uint32_t current_epoch = (uint32_t)(millis() / 1000);
```

**Issues:**
- Resets to 0 on every reboot
- Cannot correlate keystrokes across reboots
- Difficult to match with external logs
- No absolute time reference

**Works Fine For:**
- Delta encoding (relative time within a buffer)
- Runtime stability (no overflow for years)

---

## Meshtastic RTC System

### Time Quality Hierarchy
```cpp
enum RTCQuality {
    RTCQualityNone = 0,        // Haven't set RTC yet (use BUILD_EPOCH)
    RTCQualityDevice = 1,      // Time from onboard RTC chip after boot
    RTCQualityFromNet = 2,     // Time from another mesh node
    RTCQualityNTP = 3,         // Time from NTP server
    RTCQualityGPS = 4          // Time from GPS (highest quality)
};
```

### Available Functions
```cpp
// src/gps/RTC.h

// Get current RTC quality level
RTCQuality getRTCQuality();

// Get time (always returns value, even if quality is None)
uint32_t getTime(bool local = false);

// Get time only if quality meets minimum threshold (returns 0 if not)
uint32_t getValidTime(RTCQuality minQuality, bool local = false);
```

### BUILD_EPOCH Constant
```cpp
// Defined at build time in bin/platformio-custom.py
// Example: 1765083600 (December 7, 2025)
#ifdef BUILD_EPOCH
    // Available as compile-time constant
#endif
```

---

## Proposed Design (v4.0)

### Strategy: Hybrid Fallback System

```
Priority 1: RTC (Quality >= FromNet)
    ↓ (if unavailable)
Priority 2: BUILD_EPOCH + uptime
    ↓ (if not defined)
Priority 3: Uptime (current behavior)
```

### Implementation Plan

#### 1. Add RTC Dependency Header
```cpp
// keyboard_decoder_core1.cpp - Add include
#include "gps/RTC.h"  // For getValidTime(), getRTCQuality()
```

#### 2. Update `core1_write_epoch_at()` Function
```cpp
// keyboard_decoder_core1.cpp:520-530 (current implementation)
static void core1_write_epoch_at(size_t pos)
{
    uint32_t epoch = core1_get_current_epoch();
    char epoch_str[EPOCH_SIZE + 1];
    snprintf(epoch_str, sizeof(epoch_str), "%010u", epoch);
    memcpy(&g_core1_keystroke_buffer[pos], epoch_str, EPOCH_SIZE);
}

// NEW: Separate epoch acquisition from formatting
CORE1_RAM_FUNC
static uint32_t core1_get_current_epoch()
{
    // Priority 1: Try RTC with quality >= FromNet (mesh-synced or better)
    uint32_t rtc_time = getValidTime(RTCQualityFromNet, false);
    if (rtc_time > 0) {
        return rtc_time;  // Valid RTC time available
    }

#ifdef BUILD_EPOCH
    // Priority 2: BUILD_EPOCH + uptime (better than pure uptime)
    uint32_t uptime_secs = (uint32_t)(millis() / 1000);
    return BUILD_EPOCH + uptime_secs;
#else
    // Priority 3: Fallback to uptime (v3.5 behavior)
    return (uint32_t)(millis() / 1000);
#endif
}
```

#### 3. Update Buffer Initialization
```cpp
// keyboard_decoder_core1.cpp:355-383
CORE1_RAM_FUNC
static void core1_init_keystroke_buffer()
{
    g_core1_buffer_write_pos = KEYSTROKE_DATA_START;

    // Get current epoch using hybrid fallback system
    g_core1_buffer_start_epoch = core1_get_current_epoch();

    // Write start epoch at position 0
    core1_write_epoch_at(0);
    g_core1_buffer_initialized = true;
}
```

#### 4. Update Delta Calculation
```cpp
// keyboard_decoder_core1.cpp:423-465
static bool core1_add_enter_to_buffer()
{
    if (!g_core1_buffer_initialized) {
        core1_init_keystroke_buffer();
    }

    // Calculate delta from buffer start
    uint32_t current_epoch = core1_get_current_epoch();
    uint32_t delta = current_epoch - g_core1_buffer_start_epoch;

    // Rest of function unchanged...
}
```

---

## Benefits

### With RTC Available (GPS, NTP, Mesh-Synced)
✅ True unix epoch timestamps
✅ Correlate keystrokes with external logs
✅ Timestamps survive reboots
✅ Accurate forensic timeline
✅ Multi-device time synchronization

### Without RTC (Standalone Mode)
✅ BUILD_EPOCH + uptime = pseudo-absolute time
✅ Better than pure uptime (maintains rough absolute time)
✅ Still works if BUILD_EPOCH not defined (degrades to v3.5 behavior)
✅ No breaking changes to existing functionality

### Delta Encoding Still Works
✅ Delta encoding uses relative time (current - start)
✅ Works identically regardless of time source
✅ 70% space savings maintained

---

## Edge Cases & Handling

### Case 1: RTC Quality Changes During Runtime
**Scenario:** Device boots without GPS, later gets GPS lock

**Behavior:**
- Early buffers: BUILD_EPOCH + uptime
- Later buffers: GPS epoch
- Delta encoding works within each buffer
- Timestamps may "jump" between buffers (expected)

**Mitigation:** Not needed - this is correct behavior

---

### Case 2: BUILD_EPOCH Not Defined
**Scenario:** Old build system or manual compilation

**Behavior:**
- Falls back to uptime (v3.5 behavior)
- No breaking changes
- Delta encoding still works

**Mitigation:** None needed - graceful degradation

---

### Case 3: RTC Time Goes Backwards
**Scenario:** RTC chip battery dies, time resets

**Behavior:**
- Meshtastic's `perhapsSetRTC()` validates against BUILD_EPOCH
- Times before BUILD_EPOCH are rejected
- Our code will never see invalid timestamps

**Mitigation:** Already handled by Meshtastic RTC system

---

### Case 4: Overflow in Delta Calculation
**Scenario:** `current_epoch - buffer_start_epoch > 65535`

**Current Protection:**
```cpp
// keyboard_decoder_core1.cpp:435
if (delta > DELTA_MAX_SAFE) {
    core1_finalize_buffer();  // Force new buffer
    core1_init_keystroke_buffer();
    delta = 0;
}
```

**Still Valid:** Works regardless of epoch source

---

## Implementation Checklist

### Phase 1: Code Changes (1-2 hours)
- [ ] Add `#include "gps/RTC.h"` to keyboard_decoder_core1.cpp
- [ ] Implement `core1_get_current_epoch()` function
- [ ] Update `core1_init_keystroke_buffer()` to use new function
- [ ] Update `core1_add_enter_to_buffer()` to use new function
- [ ] Update `core1_finalize_buffer()` final epoch write

### Phase 2: Testing (2-4 hours)
- [ ] **Test 1:** Boot without GPS → Verify BUILD_EPOCH + uptime
- [ ] **Test 2:** GPS lock acquired → Verify switch to GPS time
- [ ] **Test 3:** Mesh time sync → Verify RTCQualityFromNet works
- [ ] **Test 4:** Delta encoding → Verify still works correctly
- [ ] **Test 5:** Buffer finalization → Verify epoch timestamps correct

### Phase 3: Documentation (30 minutes)
- [ ] Update USBCaptureModule_Documentation.md with RTC info
- [ ] Update CLAUDE.md with v4.0 changes
- [ ] Add RTC quality to log output for debugging

---

## Example Log Output

### Before RTC Lock
```
[Core0] Transmitting buffer: 6 bytes (epoch 1765083610 → 1765083615)
[Core0] RTC Quality: None, using BUILD_EPOCH+uptime
=== BUFFER START ===
Start Time: 1765083610 (BUILD_EPOCH: 1765083600, uptime: 10s)
Enter [time=1765083610, delta=+0]
Line: test
Final Time: 1765083615
=== BUFFER END ===
```

### After GPS Lock
```
[Core0] Transmitting buffer: 6 bytes (epoch 1733250000 → 1733250005)
[Core0] RTC Quality: GPS, using real epoch
=== BUFFER START ===
Start Time: 1733250000 (Sun Dec 03 2024 12:00:00 GMT)
Enter [time=1733250000, delta=+0]
Line: test
Final Time: 1733250005
=== BUFFER END ===
```

---

## Backwards Compatibility

### v3.5 Firmware (Old)
- Uses uptime only
- Buffer format identical
- Can decode v4.0 buffers (epoch is just a number)

### v4.0 Firmware (New)
- Uses RTC with fallback
- Buffer format unchanged
- Can decode v3.5 buffers (uptime is valid epoch)

**Conclusion:** Fully backwards compatible, no breaking changes

---

## Performance Impact

### Memory
- No additional RAM used
- No additional flash for function (~200 bytes)

### CPU
- `getValidTime()` call: ~5 µs (negligible)
- No change to Core1 performance budget

### Timing
- Delta encoding calculation unchanged
- No impact on capture latency

---

## Future Enhancements (v5.0+)

### Sub-Second Precision
```cpp
// Use time_us_64() for microsecond timestamps
uint64_t capture_timestamp_us = time_us_64();
```

**Benefit:** Accurate keystroke timing for typing speed analysis

### Time Quality Indicator
```cpp
// Add quality byte to buffer header
struct psram_keystroke_buffer_t {
    uint32_t start_epoch;
    uint32_t final_epoch;
    uint8_t time_quality;  // RTCQuality enum
    // ...
};
```

**Benefit:** Receiving nodes know timestamp reliability

---

## Conclusion

This design provides:
1. ✅ **Graceful fallback** - BUILD_EPOCH → uptime if needed
2. ✅ **No breaking changes** - Buffer format unchanged
3. ✅ **Real timestamps** - When RTC available
4. ✅ **Minimal complexity** - Single function change
5. ✅ **Full compatibility** - Works with all Meshtastic features

**Recommendation:** Implement in v4.0 as planned enhancement

---

## References

- `src/gps/RTC.h` - RTC API definitions
- `src/gps/RTC.cpp` - RTC implementation
- `bin/platformio-custom.py:139` - BUILD_EPOCH generation
- `keyboard_decoder_core1.cpp` - Current uptime implementation

---

**Status:** Ready for implementation - Estimated 2-4 hours total
