#include "SafeFile.h"

#ifdef FSCom

// RP2350 multi-core synchronization for filesystem operations
#ifdef XIAO_USB_CAPTURE_ENABLED
#include "platform/rp2xx0/usb_capture/common.h"
#include "hardware/timer.h"
#include "hardware/sync.h"  // For memory barriers
#include "hardware/watchdog.h"  // For filesystem operation timeout

/**
 * @brief Filesystem operation timeout tracking
 *
 * Used to detect and recover from hung LittleFS operations.
 * Core0's watchdog scratch register is used for timeout detection.
 */
static volatile uint32_t g_fs_operation_start = 0;
static constexpr uint32_t FS_OPERATION_TIMEOUT_MS = 10000;  // 10 seconds max per operation

/**
 * @brief Start tracking a filesystem operation with timeout
 */
static void startFSOperationTimeout(void)
{
    g_fs_operation_start = millis();
}

/**
 * @brief Check if filesystem operation has timed out
 *
 * @return true if operation exceeded timeout, false otherwise
 */
static bool checkFSOperationTimeout(void)
{
    if (g_fs_operation_start == 0)
        return false;

    uint32_t elapsed = millis() - g_fs_operation_start;
    if (elapsed > FS_OPERATION_TIMEOUT_MS) {
        LOG_ERROR("[SafeFile] FILESYSTEM TIMEOUT after %lu ms! Possible FS corruption or hardware issue.", elapsed);
        return true;
    }
    return false;
}

/**
 * @brief Clear filesystem operation timeout tracking
 */
static void clearFSOperationTimeout(void)
{
    g_fs_operation_start = 0;
}

/**
 * @brief Pause Core1 before filesystem operations to prevent bus contention
 *
 * This function requests Core1 to pause and waits for acknowledgment.
 * Uses ARM memory barriers for proper cross-core visibility.
 * Timeout prevents indefinite hang if Core1 doesn't respond.
 *
 * @note Only compiled when USB Capture Module is enabled
 * @note CRITICAL: Memory barriers required for Cortex-M33 dual-core
 */
static void pauseCore1(void)
{
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
 * This function signals Core1 to resume and waits for acknowledgment.
 * Uses ARM memory barriers for proper cross-core visibility.
 *
 * @note Only compiled when USB Capture Module is enabled
 * @note CRITICAL: Memory barriers required for Cortex-M33 dual-core
 */
static void resumeCore1(void)
{
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
#endif // XIAO_USB_CAPTURE_ENABLED

// Only way to work on both esp32 and nrf52
static File openFile(const char *filename, bool fullAtomic)
{
#ifdef XIAO_USB_CAPTURE_ENABLED
    // Pause Core1 to prevent memory bus contention during flash operations
    pauseCore1();

    // Start timeout tracking for filesystem operation
    // If operation hangs, this helps diagnose the issue
    startFSOperationTimeout();
#endif

    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Opening %s, fullAtomic=%d", filename, fullAtomic);
#ifdef ARCH_NRF52
    FSCom.remove(filename);
    File f = FSCom.open(filename, FILE_O_WRITE);
#ifdef XIAO_USB_CAPTURE_ENABLED
    if (checkFSOperationTimeout()) {
        LOG_ERROR("[SafeFile] Filesystem open() operation hung! (NRF52 path)");
    }
    clearFSOperationTimeout();
    resumeCore1();
#endif
    return f;
#endif
    if (!fullAtomic) {
        FSCom.remove(filename); // Nuke the old file to make space (ignore if it !exists)
    }

    String filenameTmp = filename;
    filenameTmp += ".tmp";

    // FIXME: If we are doing a full atomic write, we may need to remove the old tmp file now
    // if (fullAtomic) {
    //     FSCom.remove(filename);
    // }

    // clear any previous LFS errors
    File f = FSCom.open(filenameTmp.c_str(), FILE_O_WRITE);

#ifdef XIAO_USB_CAPTURE_ENABLED
    // Check for filesystem operation timeout
    if (checkFSOperationTimeout()) {
        LOG_ERROR("[SafeFile] Filesystem open() operation hung! File: %s", filenameTmp.c_str());
    }
    clearFSOperationTimeout();

    // Resume Core1 after file opened (or timed out)
    resumeCore1();
#endif

    return f;
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

    // Start timeout tracking for close operations
    startFSOperationTimeout();
#endif

    spiLock->lock();
    f.close();
    spiLock->unlock();

#ifdef ARCH_NRF52
#ifdef XIAO_USB_CAPTURE_ENABLED
    if (checkFSOperationTimeout()) {
        LOG_ERROR("[SafeFile] Filesystem close() operation hung!");
    }
    clearFSOperationTimeout();
    resumeCore1();
#endif
    return true;
#endif
    if (!testReadback()) {
#ifdef XIAO_USB_CAPTURE_ENABLED
        if (checkFSOperationTimeout()) {
            LOG_ERROR("[SafeFile] Filesystem testReadback() operation hung!");
        }
        clearFSOperationTimeout();
        resumeCore1();
#endif
        return false;
    }

    { // Scope for lock
        concurrency::LockGuard g(spiLock);
        // brief window of risk here ;-)
        if (fullAtomic && FSCom.exists(filename.c_str()) && !FSCom.remove(filename.c_str())) {
            LOG_ERROR("Can't remove old pref file");
#ifdef XIAO_USB_CAPTURE_ENABLED
            if (checkFSOperationTimeout()) {
                LOG_ERROR("[SafeFile] Filesystem remove() operation hung!");
            }
            clearFSOperationTimeout();
            resumeCore1();
#endif
            return false;
        }
    }

    String filenameTmp = filename;
    filenameTmp += ".tmp";
    if (!renameFile(filenameTmp.c_str(), filename.c_str())) {
        LOG_ERROR("Error: can't rename new pref file");
#ifdef XIAO_USB_CAPTURE_ENABLED
        if (checkFSOperationTimeout()) {
            LOG_ERROR("[SafeFile] Filesystem renameFile() operation hung!");
            }
        clearFSOperationTimeout();
        resumeCore1();
#endif
        return false;
    }

#ifdef XIAO_USB_CAPTURE_ENABLED
    // Check for timeout on successful path
    if (checkFSOperationTimeout()) {
        LOG_ERROR("[SafeFile] Filesystem operations took excessive time but completed");
    }
    clearFSOperationTimeout();

    // Resume Core1 after all operations complete
    resumeCore1();
#endif

    return true;
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