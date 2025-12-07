#include "SafeFile.h"

#ifdef FSCom

// RP2350 multi-core synchronization for filesystem operations
#ifdef XIAO_USB_CAPTURE_ENABLED
#include "platform/rp2xx0/usb_capture/common.h"
#include "hardware/timer.h"

/**
 * @brief Pause Core1 before filesystem operations to prevent bus contention
 *
 * This function requests Core1 to pause and waits for acknowledgment.
 * Timeout prevents indefinite hang if Core1 doesn't respond.
 *
 * @note Only compiled when USB Capture Module is enabled
 */
static void pauseCore1(void)
{
    g_core1_pause_requested = true;

    // Wait for Core1 to acknowledge pause (max 500ms)
    uint32_t start = millis();
    while (!g_core1_paused && (millis() - start) < 500)
    {
        tight_loop_contents();
    }
}

/**
 * @brief Resume Core1 after filesystem operations complete
 *
 * This function signals Core1 to resume and waits for acknowledgment.
 *
 * @note Only compiled when USB Capture Module is enabled
 */
static void resumeCore1(void)
{
    g_core1_pause_requested = false;

    // Wait for Core1 to resume (max 100ms)
    uint32_t start = millis();
    while (g_core1_paused && (millis() - start) < 100)
    {
        tight_loop_contents();
    }
}
#endif // XIAO_USB_CAPTURE_ENABLED

// Only way to work on both esp32 and nrf52
static File openFile(const char *filename, bool fullAtomic)
{
#ifdef XIAO_USB_CAPTURE_ENABLED
    // Pause Core1 to prevent memory bus contention during flash operations
    pauseCore1();
#endif

    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Opening %s, fullAtomic=%d", filename, fullAtomic);
#ifdef ARCH_NRF52
    FSCom.remove(filename);
    File f = FSCom.open(filename, FILE_O_WRITE);
#ifdef XIAO_USB_CAPTURE_ENABLED
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
    // Resume Core1 after file opened
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
#endif

    spiLock->lock();
    f.close();
    spiLock->unlock();

#ifdef ARCH_NRF52
#ifdef XIAO_USB_CAPTURE_ENABLED
    resumeCore1();
#endif
    return true;
#endif
    if (!testReadback()) {
#ifdef XIAO_USB_CAPTURE_ENABLED
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
        resumeCore1();
#endif
        return false;
    }

#ifdef XIAO_USB_CAPTURE_ENABLED
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