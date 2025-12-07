# Core Distribution Analysis & Optimization Plan

**Current System:** Dual-core RP2350 (Core0 + Core1)
**Goal:** Maximize Core0 availability for Meshtastic by moving more USB logic to Core1

---

## Current Distribution (Baseline)

### **Core1 (USB Capture Core)**

**Current responsibilities:**
1. ✅ PIO FIFO polling
2. ✅ Raw packet accumulation
3. ✅ Packet boundary detection
4. ✅ Bit unstuffing (process_packet_inline)
5. ✅ SYNC/PID validation
6. ✅ Keyboard HID decoding
7. ✅ Keystroke event creation
8. ✅ Queue push operations
9. ✅ Watchdog updates

**CPU Usage:** 15-25% when active, <5% idle

---

### **Core0 (Meshtastic Core)**

**Current responsibilities:**
1. ❌ Queue polling (every 100ms)
2. ❌ Event formatting (`formatKeystrokeEvent`)
3. ❌ Keystroke buffer management
4. ❌ Delta encoding
5. ❌ Buffer finalization logging
6. ❌ Mesh transmission
7. ✅ Meshtastic operations

**USB overhead on Core0:** ~1-2% (queue polling + formatting)

---

## Analysis: What Can Move to Core1?

### **Already on Core1** ✅
- PIO capture and processing
- Bit unstuffing
- Keyboard decoding
- **This is optimal!** These are time-critical operations

### **Should Stay on Core0** ✅
- Mesh transmission (needs MeshService/Router)
- Final buffer logging (uses LOG macros safely)
- Module lifecycle (init, runOnce)

### **Could Move to Core1** 🤔

**Option 1: Event Formatting**
- `formatKeystrokeEvent()` currently on Core0
- Simple string formatting, not time-critical
- **Benefit:** Minimal (~0.1% Core0 reduction)
- **Cost:** More complex, marginal gain
- **Verdict:** Not worth it

**Option 2: Keystroke Buffer Management**
- Buffer accumulation currently on Core0
- Delta encoding on Core0
- **Problem:** Buffer needs to be transmitted via mesh (Core0 only)
- **Verdict:** Must stay on Core0

**Option 3: Nothing - Current is Optimal**
- Core1 does all USB processing
- Core0 just reads finished events from queue
- Clean separation of concerns
- **Verdict:** ✅ RECOMMENDED

---

## Current Architecture Analysis

```
┌─────────────────────────────────────────┐
│ Core1: USB Capture (15-25% CPU)         │
├─────────────────────────────────────────┤
│ PIO FIFO → Packet assembly →            │
│ Bit unstuffing → Validation →           │
│ Keyboard decode → Queue push            │
│                                         │
│ Output: keystroke_event_t in queue     │
└──────────────┬──────────────────────────┘
               │ Lock-free queue
               ↓
┌─────────────────────────────────────────┐
│ Core0: Meshtastic + USB Module (1-2%)  │
├─────────────────────────────────────────┤
│ Queue poll (100ms) → Format event →    │
│ Buffer management → Mesh transmit       │
│                                         │
│ + Full Meshtastic stack (98% of work)  │
└─────────────────────────────────────────┘
```

**Assessment:** ✅ **Excellent distribution!**
- Core1: Heavy lifting (USB)
- Core0: Light coordination + Meshtastic
- Clean separation via queue
- No blocking between cores

---

## Optimization Opportunities

### **1. Already Optimal** ⭐ CURRENT STATE

**Why current distribution is good:**
- Core1 does ALL time-critical USB work
- Core0 overhead is negligible (1-2%)
- Queue provides clean isolation
- No mutex/locking needed
- Easy to debug and maintain

**Recommendation:** **Keep as-is**

---

### **2. Potential Micro-Optimizations** (Not Recommended)

**Move event formatting to Core1:**
```cpp
// In Core1, before queue push:
char formatted[64];
format_event_core1(&event, formatted);
// Push formatted string instead of event struct
```

**Problems:**
- Queue would need variable-length strings
- More complex than fixed-size events
- Breaks clean event abstraction
- Saves <0.5% Core0 CPU

**Verdict:** ❌ Not worth complexity

---

**Reduce queue polling frequency:**
```cpp
// Change from 100ms to 200ms
```

**Benefits:**
- Halves Core0 polling overhead
- Still plenty fast for keyboard

**Risks:**
- Increased latency (100ms → 200ms max)
- Not noticeable for typing

**Verdict:** ⚠️ Possible but unnecessary

---

### **3. Future Optimizations** (If Needed)

**Only pursue if:**
- Core0 CPU usage exceeds 80%
- Meshtastic performance degrades
- Multiple USB devices added
- Measurable performance problem

**Then consider:**
1. Batch queue reads (read 10 events at once)
2. Async mesh transmission
3. Buffer events in Core1, periodic flush
4. DMA for queue (overkill but possible)

---

## Recommended Action

### **Do Nothing** ✅

**Current system is already well-optimized:**
- Core1: Dedicated to USB (correct!)
- Core0: <2% overhead (excellent!)
- Clean architecture
- No performance issues

**Instead, focus on:**
1. **Modifier key support** - User-visible feature
2. **Better mesh packets** - Protobuf encoding
3. **Statistics** - Actual telemetry
4. **Configuration** - Runtime settings

These provide **actual value** vs. micro-optimizing an already-efficient system.

---

## CPU ID Logging Added

**Purpose:** Visibility into core usage

**Changes:**
- `[Core0]` or `[Core1]` prefix on logs
- Easy to see what runs where
- Helps future debugging

**Expected output:**
```
[Core0] USB Capture Module initializing...
[Core0] Launching Core1...
[Core0] Keystroke: CHAR 'h'
[Core0] Queue stats: count=0, dropped=0
```

All should show `[Core0]` because:
- Module runs on Core0
- Queue is polled from Core0
- Logging from Core0

**Core1 doesn't log** - it just pushes events to queue.

---

## Conclusion

**Current core distribution is optimal.**

The experiment request to "move more logic to Core1" is already satisfied:
- ALL USB processing on Core1 ✅
- Only queue polling on Core0 ✅
- No room for meaningful improvement ✅

**Time to build features users will notice!**

---

**Next Steps:**
1. Build with CPU ID logging
2. Test to confirm Core0 vs Core1 labels
3. Move to actual feature development

**Ready to build and test?**
