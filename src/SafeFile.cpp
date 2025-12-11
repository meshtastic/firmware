#include "SafeFile.h"

#ifdef FSCom

// RP2350 multi-core synchronization for filesystem operations
#ifdef XIAO_USB_CAPTURE_ENABLED
#include "platform/rp2xx0/usb_capture/common.h"
#include "hardware/timer.h"
#include "hardware/sync.h"  // For memory barriers
#include "pico/multicore.h" // For FIFO operations

// ========== RECOVERY METHOD SELECTION ==========
// Toggle between recovery methods by uncommenting ONE of these:
#define USE_FIFO_RECOVERY        // Experimental: Send LOCKOUT_MAGIC_END via FIFO
// #define USE_NUCLEAR_OPTION       // Recommended: Stop/restart Core1 completely
// ===============================================
//
// TO SWITCH METHODS:
// 1. Comment out current method (add // before #define)
// 2. Uncomment desired method (remove // from #define)
// 3. Rebuild and flash
// ===============================================

// LOCKOUT_MAGIC_END constant from pico-sdk (for FIFO recovery)
#define LOCKOUT_MAGIC_START 0x73a8831eu
#define LOCKOUT_MAGIC_END (~LOCKOUT_MAGIC_START)

/**
 * @brief Pause Core1 before filesystem operations to prevent bus contention
 *
 * Uses voluntary pause mechanism where Core1 checks a flag and pauses itself.
 * This avoids deadlocks from rp2040.idleOtherCore() when Core1 is blocked on FIFO.
 *
 * IMPORTANT: If Core1 hasn't started yet (during boot), skip the pause.
 *
 * @note Only compiled when USB Capture Module is enabled
 * @note Core1 voluntarily pauses when it sees g_core1_pause_requested
 */
static void pauseCore1(void)
{
    // Check if Core1 has actually started - if not, skip pause
    __dmb();  // Memory barrier - ensure we see Core1's write
    if (!g_core1_running) {
        LOG_DEBUG("[SafeFile] Core1 not running yet, skipping pause");
        return;
    }

    LOG_DEBUG("[SafeFile] Requesting Core1 pause...");
    g_core1_pause_requested = true;
    __dmb();  // Data Memory Barrier - ensure write visible to Core1

    // Wait for Core1 to acknowledge pause (max 500ms)
    uint32_t start = millis();
    while (!g_core1_paused && (millis() - start) < 500)
    {
        __dmb();  // Ensure we see Core1's write
        tight_loop_contents();
    }
    __dmb();  // Final barrier before proceeding

    if (g_core1_paused) {
        LOG_DEBUG("[SafeFile] Core1 paused successfully");
    } else {
        LOG_WARN("[SafeFile] Core1 pause TIMEOUT after 500ms!");
    }
}

/**
 * @brief Resume Core1 after filesystem operations complete
 *
 * Uses voluntary resume mechanism where Core1 sees flag cleared and resumes.
 *
 * IMPORTANT: If Core1 wasn't paused (not running), skip the resume.
 *
 * @note Only compiled when USB Capture Module is enabled
 * @note Core1 voluntarily resumes when it sees g_core1_pause_requested cleared
 */
static void resumeCore1(void)
{
    // If Core1 not running, pauseCore1() skipped the pause, so skip resume too
    __dmb();  // Memory barrier - ensure we see Core1's write
    if (!g_core1_running) {
        LOG_DEBUG("[SafeFile] Core1 not running yet, skipping resume");
        return;
    }

    LOG_DEBUG("[SafeFile] Requesting Core1 resume...");
    g_core1_pause_requested = false;
    __dmb();  // Data Memory Barrier - ensure write visible to Core1

    // Wait for Core1 to resume (max 100ms)
    uint32_t start = millis();
    while (g_core1_paused && (millis() - start) < 100)
    {
        __dmb();  // Ensure we see Core1's write
        tight_loop_contents();
    }
    __dmb();  // Final barrier

    if (!g_core1_paused) {
        LOG_DEBUG("[SafeFile] Core1 resumed successfully");
    } else {
        LOG_WARN("[SafeFile] Core1 resume TIMEOUT after 100ms!");
    }
}

#ifdef USE_FIFO_RECOVERY
/**
 * @brief Attempt to recover Core1 from stuck lockout state
 *
 * Sends LOCKOUT_MAGIC_END via FIFO to unblock Core1 if it's stuck waiting
 * for the end signal from a timed-out flash_safe_execute() call.
 *
 * @note This is EXPERIMENTAL on RP2350 due to FIFO IRQ conflicts (Issue #2222)
 * @note See: https://github.com/raspberrypi/pico-sdk/issues/2454
 */
static void recoverStuckLockout(void)
{
    LOG_DEBUG("[SafeFile] FIFO Recovery: Checking for stuck lockout state...");

    uint32_t interrupts = save_and_disable_interrupts();

    // Check if FIFO is ready to write
    if (multicore_fifo_wready()) {
        LOG_DEBUG("[SafeFile] FIFO Recovery: Sending LOCKOUT_MAGIC_END (0x%08x)", LOCKOUT_MAGIC_END);

        // Clear any stale FIFO data first
        while (multicore_fifo_rvalid()) {
            uint32_t discard = multicore_fifo_pop_blocking();
            LOG_DEBUG("[SafeFile] FIFO Recovery: Discarded stale FIFO data: 0x%08x", discard);
        }

        // Send LOCKOUT_MAGIC_END to unblock Core1
        multicore_fifo_push_blocking(LOCKOUT_MAGIC_END);

        // Wait for acknowledgment with timeout (100ms)
        uint32_t start_us = time_us_32();
        bool got_ack = false;

        while ((time_us_32() - start_us) < 100000) {
            if (multicore_fifo_rvalid()) {
                uint32_t response = multicore_fifo_pop_blocking();
                LOG_DEBUG("[SafeFile] FIFO Recovery: Got response: 0x%08x", response);

                if (response == LOCKOUT_MAGIC_END) {
                    got_ack = true;
                    LOG_INFO("[SafeFile] FIFO Recovery: SUCCESS - Core1 acknowledged unlock");
                    break;
                }
            }
            tight_loop_contents();
        }

        if (!got_ack) {
            LOG_WARN("[SafeFile] FIFO Recovery: TIMEOUT - No acknowledgment from Core1");
            LOG_WARN("[SafeFile] FIFO Recovery: This may indicate RP2350 FIFO IRQ conflict (Issue #2222)");
        }
    } else {
        LOG_WARN("[SafeFile] FIFO Recovery: FIFO not writable - cannot send recovery signal");
    }

    restore_interrupts(interrupts);
}
#endif // USE_FIFO_RECOVERY

#ifdef USE_NUCLEAR_OPTION
/**
 * @brief Nuclear option: Completely stop and restart Core1
 *
 * This avoids the broken SDK lockout mechanism on RP2350 by completely
 * stopping Core1 before filesystem operations and restarting it after.
 *
 * @note Recommended approach for RP2350 until SDK 2.3.0 fixes FIFO lockout
 * @note PSRAM buffer persists in RAM, so no data loss
 * @note Adds ~100ms delay per filesystem operation
 */
static void nuclearResetCore1(void)
{
    LOG_DEBUG("[SafeFile] Nuclear: Stopping Core1 completely...");

    // Completely stop Core1
    multicore_reset_core1();
    g_core1_running = false;

    // Give it a moment to fully stop
    busy_wait_us(1000);

    LOG_DEBUG("[SafeFile] Nuclear: Core1 stopped");
}

static void nuclearRestartCore1(void)
{
    if (g_core1_running) {
        LOG_DEBUG("[SafeFile] Nuclear: Core1 already running, skipping restart");
        return;
    }

    LOG_DEBUG("[SafeFile] Nuclear: Restarting Core1...");

    // Need to declare the Core1 main function
    extern void usb_capture_core1_main(void);

    // Restart Core1 from scratch
    multicore_launch_core1(usb_capture_core1_main, NULL);

    // Wait for Core1 to signal it's running (max 500ms)
    uint32_t start = millis();
    while (!g_core1_running && (millis() - start) < 500) {
        __dmb();
        tight_loop_contents();
    }

    if (g_core1_running) {
        LOG_INFO("[SafeFile] Nuclear: Core1 restarted successfully");
    } else {
        LOG_ERROR("[SafeFile] Nuclear: Core1 restart TIMEOUT!");
    }
}
#endif // USE_NUCLEAR_OPTION

#endif // XIAO_USB_CAPTURE_ENABLED

// Only way to work on both esp32 and nrf52
static File openFile(const char *filename, bool fullAtomic)
{
#ifdef XIAO_USB_CAPTURE_ENABLED
    #ifdef USE_NUCLEAR_OPTION
        // Nuclear option: Stop Core1 completely before filesystem ops
        nuclearResetCore1();
    #else
        // Standard pause mechanism (used with FIFO recovery)
        pauseCore1();
    #endif
#endif

    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Opening %s, fullAtomic=%d", filename, fullAtomic);

#if defined(ARCH_NRF52) || defined(XIAO_USB_CAPTURE_ENABLED)
    // Simple write path for NRF52 and USB Capture (no atomic .tmp files)
    LOG_DEBUG("[SafeFile] About to remove file: %s", filename);
    FSCom.remove(filename);
    LOG_DEBUG("[SafeFile] File removed, about to open: %s", filename);
    File f = FSCom.open(filename, FILE_O_WRITE);
    LOG_DEBUG("[SafeFile] File opened successfully");

#ifdef XIAO_USB_CAPTURE_ENABLED
    #ifdef USE_FIFO_RECOVERY
        // EXPERIMENTAL: Try to recover from any stuck lockout state
        LOG_DEBUG("[SafeFile] Attempting FIFO recovery...");
        recoverStuckLockout();
        LOG_DEBUG("[SafeFile] About to resume Core1...");
        resumeCore1();
    #endif

    #ifdef USE_NUCLEAR_OPTION
        // Restart Core1 after filesystem operations complete
        LOG_DEBUG("[SafeFile] About to restart Core1...");
        nuclearRestartCore1();
    #endif

    LOG_DEBUG("[SafeFile] Core1 management complete, returning file handle");
#endif
    return f;
#else
    // Atomic write path for other platforms (ESP32, etc.)
    if (!fullAtomic) {
        FSCom.remove(filename); // Nuke the old file to make space (ignore if it !exists)
    }

    String filenameTmp = filename;
    filenameTmp += ".tmp";

    // clear any previous LFS errors
    File f = FSCom.open(filenameTmp.c_str(), FILE_O_WRITE);
    return f;
#endif
}

SafeFile::SafeFile(const char *_filename, bool fullAtomic)
    : filename(_filename), f(openFile(_filename, fullAtomic)), fullAtomic(fullAtomic)
{
}

size_t SafeFile::write(uint8_t ch)
{
    if (!f)
        return 0;

    hash ^= ch;
    return f.write(ch);
}

size_t SafeFile::write(const uint8_t *buffer, size_t size)
{
    if (!f)
        return 0;

    for (size_t i = 0; i < size; i++) {
        hash ^= buffer[i];
    }
    return f.write((uint8_t const *)buffer, size); // This nasty cast is _IMPORTANT_ otherwise the correct adafruit method does
                                                   // not get used (they made a mistake in their typing)
}

/**
 * Atomically close the file (deleting any old versions) and readback the contents to confirm the hash matches
 *
 * @return false for failure
 */
bool SafeFile::close()
{
    if (!f)
        return false;

#ifdef XIAO_USB_CAPTURE_ENABLED
    // Pause Core1 during close/rename operations
    pauseCore1();
#endif

    spiLock->lock();
    f.close();
    spiLock->unlock();

#if defined(ARCH_NRF52) || defined(XIAO_USB_CAPTURE_ENABLED)
    // Simple write path - no atomic verification (NRF52 and USB Capture)
#ifdef XIAO_USB_CAPTURE_ENABLED
    resumeCore1();
#endif
    return true;
#else
    // Atomic write path for other platforms (ESP32, etc.)
    if (!testReadback()) {
        return false;
    }

    { // Scope for lock
        concurrency::LockGuard g(spiLock);
        // brief window of risk here ;-)
        if (fullAtomic && FSCom.exists(filename.c_str()) && !FSCom.remove(filename.c_str())) {
            LOG_ERROR("Can't remove old pref file");
            return false;
        }
    }

    String filenameTmp = filename;
    filenameTmp += ".tmp";
    if (!renameFile(filenameTmp.c_str(), filename.c_str())) {
        LOG_ERROR("Error: can't rename new pref file");
        return false;
    }

    return true;
#endif
}

/// Read our (closed) tempfile back in and compare the hash
bool SafeFile::testReadback()
{
    concurrency::LockGuard g(spiLock);

    String filenameTmp = filename;
    filenameTmp += ".tmp";
    auto f2 = FSCom.open(filenameTmp.c_str(), FILE_O_READ);
    if (!f2) {
        LOG_ERROR("Can't open tmp file for readback");
        return false;
    }

    int c = 0;
    uint8_t test_hash = 0;
    while ((c = f2.read()) >= 0) {
        test_hash ^= (uint8_t)c;
    }
    f2.close();

    if (test_hash != hash) {
        LOG_ERROR("Readback failed hash mismatch");
        return false;
    }

    return true;
}

#endif