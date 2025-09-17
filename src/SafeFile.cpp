#include "SafeFile.h"

#ifdef FSCom

// Only way to work on both esp32 and nrf52
static File openFile(const char *filename, bool fullAtomic)
{
    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Opening %s, fullAtomic=%d", filename, fullAtomic);
#ifdef ARCH_NRF52
    FSCom.remove(filename);
    return FSCom.open(filename, FILE_O_WRITE);
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
    return FSCom.open(filenameTmp.c_str(), FILE_O_WRITE);
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

    spiLock->lock();
    f.close();
    spiLock->unlock();

#ifdef ARCH_NRF52
    return true;
#endif
    if (!testReadback())
        return false;

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