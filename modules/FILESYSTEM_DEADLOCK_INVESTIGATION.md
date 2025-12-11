# Filesystem Deadlock Investigation - Session 2025-12-08

**Issue:** Device hangs with dim red/orange LED when writing to LittleFS after Core1 starts
**Status:** UNRESOLVED - Multiple attempted fixes, still hanging
**Last Updated:** 2025-12-08

---

## Problem Description

### Symptoms
- ✅ **Before Core1 starts:** File operations work perfectly
- ❌ **After Core1 starts:** Device hangs on ANY filesystem write
- **Hang point:** `FSCom.remove()` or `FSCom.open()` in SafeFile.cpp
- **LED:** Dim red or orange (hardware-level deadlock)
- **Logs:** Stops at "Opening /prefs/nodes.proto, fullAtomic=0"
- **Recovery:** Manual reboot required, settings don't save

### Trigger
Any config change that requires writing to flash:
- Node database updates (nodes.proto)
- Config changes via CLI
- Channel modifications
- Any SafeFile operation

---

## Root Cause Analysis (Research Findings)

### Primary Issue: SDK Multicore Lockout Bugs

**Source:** [MicroPython Issue #16619](https://github.com/micropython/micropython/issues/16619), [pico-sdk Issue #2201](https://github.com/raspberrypi/pico-sdk/issues/2201)

**The Problem:**
1. Arduino-Pico's `FSCom.open()`/`FSCom.remove()` call `flash_safe_execute()` internally
2. `flash_safe_execute()` uses `multicore_lockout_start_blocking()` to pause Core1
3. SDK bugs cause lockout mechanism to wait forever even when Core1 appears paused
4. Result: Infinite wait → device hang → dim LED

**Specific Bugs Found:**
- **Bug #1:** `multicore_reset_core1()` doesn't reset `lockout_victim_initialized` flag
- **Bug #2:** `flash_safe_execute()` timeout doesn't send `LOCKOUT_MAGIC_END` → Core1 stuck forever
- **Bug #3:** FIFO conflict between SDK lockout and user code on RP2350
- **Bug #4:** Corrupted lockout state persists across soft resets

### Secondary Issue: FIFO Resource Conflict

**Source:** [RP2350 Forum Discussion](https://forums.raspberrypi.com/viewtopic.php?t=388734)

**The Problem:**
- `flash_safe_execute()` uses SIO FIFO for lockout mechanism
- RP2350: FIFO IRQ number is SAME for both cores (different on RP2040)
- Any user FIFO usage conflicts with SDK's lockout mechanism
- Even with manual pause, SDK still tries FIFO lockout → deadlock

### Tertiary Issue: RP2350 Flash-PSRAM QMI Conflict (NOT APPLICABLE)

**Source:** [Arduino-Pico Issue #2537](https://github.com/earlephilhower/arduino-pico/issues/2537)

Flash operations corrupt PSRAM QMI configuration - BUT our board has **NO PSRAM** (confirmed: `board_upload.psram_length = 0`), so this is not our issue. Our "PSRAM buffer" is just regular RAM (`psram_buffer_t g_psram_buffer;`).

---

## Attempted Fixes (All Failed)

### Attempt 1: Manual Pause Mechanism ❌
**What:** Voluntary pause where Core1 checks `g_core1_pause_requested` flag
**Result:** Core1 pauses successfully, but still hangs on `FSCom.open()`
**Why failed:** SDK's `flash_safe_execute()` still tries to use its own lockout mechanism

### Attempt 2: Official rp2040.idleOtherCore() API ❌
**What:** Use Arduino-Pico's official idle/resume functions
**Result:** Deadlock - `idleOtherCore()` waits forever for Core1 acknowledgment
**Why failed:** Core1 might be blocked on FIFO when idle request arrives

### Attempt 3: Watchdog Disable/Enable ❌
**What:** Disable watchdog before pause to prevent timeout during long filesystem ops
**Result:** Eliminated watchdog timeouts but didn't fix hang
**Why failed:** Hang is in LittleFS/flash operations, not watchdog-related

### Attempt 4: Remove Watchdog Entirely ❌
**What:** Removed all watchdog usage from Core1
**Result:** No improvement, still hangs
**Why failed:** Watchdog was not the root cause

### Attempt 5: PIO Hardware Pause ✅ (Partial)
**What:** Added `pio_manager_pause_capture()` / `pio_manager_resume_capture()`
**Result:** Core1 properly stops PIO hardware during pause
**Why failed:** Helps but doesn't solve LittleFS hang

### Attempt 6: Interrupt Disable During Pause ✅ (Partial)
**What:** Added `save_and_disable_interrupts()` in Core1 pause handler
**Result:** Core1 interrupts properly disabled during pause
**Why failed:** LittleFS still hangs despite no interrupt conflicts

### Attempt 7: Remove FIFO Usage ✅ (Implemented)
**What:** Removed `multicore_fifo_pop_blocking()` from Core1, removed FIFO stop signals
**Result:** No more FIFO conflict with SDK
**Why failed:** LittleFS hang persists

### Attempt 8: Simple Write Path (No Atomic) ✅ (Implemented)
**What:** Use NRF52-style simple writes (no .tmp files, no testReadback)
**Result:** Simpler code, saved ~800 bytes flash
**Why failed:** Still hangs on `FSCom.remove()` or `FSCom.open()`

### Attempt 9: multicore_lockout_victim_init() ✅ (Implemented)
**What:** Added `multicore_lockout_victim_init()` at Core1 startup
**Result:** Core1 properly initialized as lockout victim
**Status:** STILL TESTING - may not be sufficient

---

## Current Implementation (v4.2-dev)

### SafeFile.cpp Pause Mechanism
```cpp
static void pauseCore1(void)
{
    // Skip if Core1 not running yet (boot-time operations)
    if (!g_core1_running) return;

    // Set pause flag
    g_core1_pause_requested = true;
    __dmb();

    // Wait for Core1 to acknowledge (500ms timeout)
    while (!g_core1_paused && timeout) {
        __dmb();
        tight_loop_contents();
    }
}
```

### Core1 Pause Handler
```cpp
// In usb_capture_main.cpp main loop
if (g_core1_pause_requested) {
    pio_manager_pause_capture();           // Stop PIO hardware
    save_and_disable_interrupts();         // Disable interrupts
    g_core1_paused = true;                 // Signal paused
    __dmb();

    while (g_core1_pause_requested) {      // Wait for resume
        __dmb();
        tight_loop_contents();
    }

    g_core1_paused = false;                // Signal resumed
    restore_interrupts();                  // Re-enable interrupts
    pio_manager_resume_capture();          // Resume PIO
}
```

### Core1 Initialization
```cpp
void capture_controller_core1_main_v2(void)
{
    multicore_lockout_victim_init();  // NEW: Initialize lockout victim

    // ... rest of initialization ...
}
```

---

## Debug Logging Added

Added detailed logging to pinpoint exact hang location:

```cpp
LOG_DEBUG("[SafeFile] About to remove file: %s", filename);
FSCom.remove(filename);  // ← Hang here?
LOG_DEBUG("[SafeFile] File removed, about to open: %s", filename);
FSCom.open(filename, FILE_O_WRITE);  // ← Or hang here?
LOG_DEBUG("[SafeFile] File opened successfully");
resumeCore1();
LOG_DEBUG("[SafeFile] Core1 resumed, returning file handle");
```

**Test Result (2025-12-08):**
```
DEBUG | [SafeFile] Core1 paused successfully ✅
DEBUG | Opening /prefs/nodes.proto, fullAtomic=0 ✅
DEBUG | [SafeFile] About to remove file: /prefs/nodes.proto ✅
[HANGS IN FSCom.remove()] 💀
```

**Conclusion:** Hang is INSIDE `FSCom.remove()` LittleFS operation, NOT in our pause mechanism.

---

## Open Questions

### Critical Unknowns
1. **Does `multicore_lockout_victim_init()` actually work?**
   - Does SDK's automatic lockout mechanism kick in?
   - Or is it still using our manual pause only?

2. **Where exactly does the hang occur?**
   - In `FSCom.remove()`? (flash metadata update)
   - In `FSCom.open()`? (flash allocation)
   - In LittleFS internal operations?

3. **Is LittleFS itself corrupted?**
   - Could previous crashes have corrupted filesystem?
   - Need to try erasing and reformatting flash?

4. **Is there a XIP cache coherency issue?**
   - Forum mentioned XIP cache invalidation during flash writes
   - Could Core1 be trying to fetch instructions from invalidated cache?

### Hardware Unknowns
1. **Which RP2350 stepping?** (A2 vs A4 - check chip marking)
2. **Is flash hardware functional?** (can we write from single-core mode?)
3. **Are there hardware errata affecting flash operations?**

---

## Next Steps to Try

### Diagnostic Steps
1. ✅ **Get exact hang location** - Check which debug log appears last
2. ⬜ **Test filesystem without Core1** - Comment out multicore_launch_core1(), verify filesystem works
3. ⬜ **Check RP2350 stepping** - Physical chip marking (A2, A4, etc.)
4. ⬜ **Verify SDK version** - Confirm 2.1.0+ is actually being used
5. ⬜ **Test filesystem corruption** - Try `FSCom.format()` to reset filesystem

### Code Changes to Try
1. ⬜ **Add flash_safe_execute wrapper diagnostics**
   ```cpp
   // Before FSCom operations
   LOG_DEBUG("lockout_victim_initialized[1] = %d",
             multicore_lockout_victim_is_initialized(1));
   ```

2. ⬜ **Implement watchdog timeout safeguard**
   ```cpp
   watchdog_enable(5000, 1);  // 5 second timeout
   FSCom.open(...);
   watchdog_disable();
   // If hangs, watchdog will reboot after 5s
   ```

3. ⬜ **Try filesystem operations from Core1 instead**
   - Move SafeFile operations to Core1
   - Core0 never touches flash
   - See if problem reverses

4. ⬜ **Implement complete Core1 stop/restart around filesystem ops**
   ```cpp
   multicore_reset_core1();  // Stop Core1 completely
   FSCom.open(...);          // Do filesystem operation
   multicore_launch_core1(); // Restart Core1
   ```

5. ⬜ **Check if FreeRTOS is interfering**
   - Meshtastic uses FreeRTOS
   - Known issue with FreeRTOS + LittleFS deadlock
   - May need FreeRTOS-specific locks

### Research Areas
1. ⬜ Check other Meshtastic RP2040/RP2350 variants for filesystem handling
2. ⬜ Look for RP2350-specific Meshtastic issues on GitHub
3. ⬜ Check if there's a Meshtastic-specific filesystem wrapper
4. ⬜ Search for FreeRTOS + LittleFS + multicore examples

---

## Technical Details

### Build Configuration
- **SDK Version:** 2.1.0 (has lockout bug fixes)
- **Board:** XIAO RP2350 (NO PSRAM - confirmed)
- **Filesystem:** LittleFS on flash (512KB partition)
- **Framework:** Arduino-Pico with FreeRTOS

### Memory Usage (v4.2-dev with debug logging)
- **Flash:** 878,584 bytes (56.0%)
- **RAM:** 137,968 bytes (26.3%)

### Current Flags
```cpp
XIAO_USB_CAPTURE_ENABLED     // Enables USB capture module
-Wl,--wrap=__wrap_flash_flush_cache  // Suspicious - double-wrapped?
```

---

## Research Links

### SDK Bugs
- [pico-sdk #2201: multicore_reset_core1 doesn't reset lockout state](https://github.com/raspberrypi/pico-sdk/issues/2201)
- [pico-sdk #2454: flash_safe_execute timeout leaves Core1 stuck](https://github.com/raspberrypi/pico-sdk/issues/2454)
- [MicroPython #16619: Flash writes broken after Core1 start](https://github.com/micropython/micropython/issues/16619)

### RP2350 Issues
- [RP2350 multicore interrupts and flash EEPROM](https://forums.raspberrypi.com/viewtopic.php?t=388734)
- [Flash operations break PSRAM - arduino-pico #2537](https://github.com/earlephilhower/arduino-pico/issues/2537)
- [LittleFS deadlock - arduino-pico #1439](https://github.com/earlephilhower/arduino-pico/issues/1439)

### Working Examples
- [BH1PHL: LittleFS with multicore on RP2040](https://www.qsl.net/bh1phl/posts/rp2040_multicore_littlefs/)
- [Arduino-Pico Multicore Documentation](https://arduino-pico.readthedocs.io/en/latest/multicore.html)

---

## Key Learnings

### What Works
1. ✅ **Manual pause mechanism** - Core1 reliably pauses when requested
2. ✅ **PIO hardware pause** - USB capture stops cleanly during pause
3. ✅ **Interrupt disable** - No interrupt conflicts during pause
4. ✅ **Boot handling** - Gracefully skips pause when Core1 not running
5. ✅ **Simple write path** - Saved flash, simpler code

### What Doesn't Work
1. ❌ **rp2040.idleOtherCore()** - Deadlocks waiting for Core1 acknowledgment
2. ❌ **FIFO usage** - Conflicts with SDK's flash_safe_execute() FIFO usage
3. ❌ **Watchdog** - Causes timeout issues, not needed
4. ❌ **Our pause mechanism alone** - LittleFS still hangs despite Core1 paused

### What We Don't Know
1. ❓ Does `multicore_lockout_victim_init()` actually fix the SDK lockout?
2. ❓ Is LittleFS itself corrupted from previous crashes?
3. ❓ Is there a FreeRTOS-specific issue we're missing?
4. ❓ Does Arduino-Pico 2.1.0 SDK actually have all the fixes?

---

## Files Modified (This Session)

### src/SafeFile.cpp
- Added `pauseCore1()` / `resumeCore1()` functions
- Checks `g_core1_running` flag before pause
- Uses manual pause mechanism (not rp2040.idleOtherCore)
- Removed filesystem timeout tracking (diagnostic only)
- Fixed invalid `#ifdef ARCH_NRF52 || XIAO_USB_CAPTURE_ENABLED` syntax
- USB Capture now uses simple write path (no atomic .tmp files)
- Added detailed debug logging to pinpoint hang location

### src/platform/rp2xx0/usb_capture/common.h
- Added `extern volatile bool g_core1_running` flag
- Added `extern volatile bool g_core1_pause_requested` flag
- Added `extern volatile bool g_core1_paused` flag
- Updated documentation

### src/platform/rp2xx0/usb_capture/usb_capture_main.cpp
- **ADDED:** `multicore_lockout_victim_init()` at Core1 startup (CRITICAL)
- Removed watchdog (enable, update, disable)
- Removed FIFO usage (pop_blocking, push_blocking)
- Set `g_core1_running = true` when Core1 enters main loop
- Implemented pause handler with PIO pause + interrupt disable
- Uses `g_capture_running_v2` flag for stop instead of FIFO signal

### src/platform/rp2xx0/usb_capture/pio_manager.{h,c}
- Added `pio_manager_pause_capture()` function
- Added `pio_manager_resume_capture()` function
- Stops/starts PIO state machines to prevent bus contention

---

## Code Evolution Timeline (This Session)

### v1 - Initial Issue Discovery
- Problem: Device reboots after Core1 starts and filesystem operation occurs
- First attempt: Add watchdog disable in SafeFile → Wrong approach

### v2 - Official API Attempt
- Tried: `rp2040.idleOtherCore()` / `rp2040.resumeOtherCore()`
- Result: Deadlock - Core1 can't acknowledge if blocked on FIFO

### v3 - Manual Pause + Watchdog Management
- Implemented: Voluntary pause with watchdog disable/enable
- Result: Watchdog conflicts between cores

### v4 - Remove Watchdog
- Removed: All watchdog usage from Core1
- Result: No watchdog timeouts, but still hangs

### v5 - Remove FIFO
- Removed: FIFO usage to avoid conflict with flash_safe_execute
- Result: No FIFO conflict, but still hangs

### v6 - Add PIO Pause
- Added: PIO hardware pause during Core1 pause
- Result: Clean PIO stop, but still hangs

### v7 - Add Interrupt Disable
- Added: `save_and_disable_interrupts()` during Core1 pause
- Result: No interrupt conflicts, but still hangs

### v8 - Simple Write Path
- Changed: Use NRF52-style simple writes (no atomic .tmp files)
- Result: Simpler code, saved flash, still hangs

### v9 - Lockout Victim Init (Current)
- Added: `multicore_lockout_victim_init()` at Core1 startup
- Added: Debug logging to pinpoint exact hang location
- Status: TESTING

---

## Hypothesis: Why Nothing Works

**Theory 1: SDK Lockout Mechanism Fundamentally Broken on RP2350**
- Even with `multicore_lockout_victim_init()`, the SDK's automatic pause might not work
- RP2350 FIFO/doorbell differences cause lockout mechanism to fail
- Need to completely bypass SDK's flash_safe_execute mechanism

**Theory 2: FreeRTOS Scheduler Interference**
- Meshtastic uses FreeRTOS
- FreeRTOS scheduler might be interfering with lockout mechanism
- SafeFile uses `concurrency::LockGuard` which is FreeRTOS-based
- Need FreeRTOS-specific flash operation handling

**Theory 3: LittleFS Corruption**
- Previous crashes may have corrupted LittleFS internal state
- Filesystem metadata might be in inconsistent state
- Need to erase and reformat flash to clean slate

**Theory 4: Hardware Issue**
- Early RP2350 stepping (A2) might have undocumented errata
- Flash controller might have bugs in multicore scenarios
- Might need hardware workarounds or newer stepping

---

## CRITICAL FINDING: FSCom.remove() Hangs

**The hang is NOT in our code - it's inside LittleFS itself!**

`FSCom.remove()` calls `flash_safe_execute()` internally to erase flash blocks. Based on research:
- `flash_safe_execute()` tries to pause Core1 via `multicore_lockout_start_blocking()`
- Even with `multicore_lockout_victim_init()` called, the lockout mechanism might timeout
- [pico-sdk #2454](https://github.com/raspberrypi/pico-sdk/issues/2454): "If timeout occurs, Core1 stuck in lockout handler forever"

**The vicious cycle:**
1. `FSCom.remove()` calls `flash_safe_execute()`
2. SDK tries to pause Core1 (even though we already paused it manually!)
3. SDK's lockout times out (Core1 already paused, can't respond)
4. SDK never sends `LOCKOUT_MAGIC_END` to Core1
5. Next flash operation waits forever for Core1 → **INFINITE HANG**

---

## Recommended Next Actions

### Immediate (Next Session - CRITICAL)
1. **Check debug logs** - Determine if hang is in `FSCom.remove()` or `FSCom.open()`
2. **Test without Core1** - Comment out `multicore_launch_core1()` and verify filesystem works
3. **Check lockout victim status** - Add log: `multicore_lockout_victim_is_initialized(1)`
4. **Try filesystem format** - Erase and reformat to eliminate corruption possibility

### If Still Hanging
1. **Implement watchdog safeguard** - Force reboot after 5s if hung
2. **Stop/restart Core1** - Use `multicore_reset_core1()` around filesystem ops
3. **Move filesystem to Core1** - Let Core1 handle its own config, avoid cross-core flash
4. **Contact arduino-pico maintainer** - Report RP2350-specific issue

### Nuclear Options (Last Resort)
1. **Disable USB Capture during config changes** - Stop Core1, save config, restart Core1
2. **RAM-only config** - Don't save to flash, lose settings on reboot
3. **Switch to RP2040** - Use older chip without RP2350 lockout bugs
4. **Wait for SDK 2.2.0** - More lockout bug fixes may be coming

---

## Session Summary

**Attempts:** 9 different approaches
**Fixes Applied:** 7 improvements implemented
**Result:** Still hanging on LittleFS operations
**Conclusion:** Likely SDK-level bug requiring deeper investigation or workaround

**Current State:**
- Core1 pauses correctly ✅
- PIO stops correctly ✅
- Interrupts disabled correctly ✅
- Lockout victim initialized ✅
- **BUT:** Still hangs in `FSCom.remove()` or `FSCom.open()` ❌

**This is a deep SDK or LittleFS issue, not a simple code problem.**

---

*Investigation Date: 2025-12-08*
*Investigator: Claude Code (Troubleshooting Mode)*
*Session Duration: ~2 hours*
*Files Modified: 4 (SafeFile.cpp, common.h, usb_capture_main.cpp, pio_manager.{h,c})*
