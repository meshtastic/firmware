#pragma once

#include "FSCommon.h"
#include "SPILock.h"
#include "configuration.h"

#ifdef FSCom

/**
 * This class provides 'safe'/paranoid file writing.
 *
 * Some of our filesystems (in particular the nrf52) may have bugs beneath our layer.  Therefore we want to
 * be very careful about how we write files.  This class provides a restricted (Stream only) writing API for writing to files.
 *
 * Notably:
 * - we keep a simple xor hash of all characters that were written.
 * - We do not allow seeking (because we want to maintain our hash)
 * - we provide an close() method which is similar to close but returns false if we were unable to successfully write the
 * file.  Also this method
 * - atomically replaces any old version of the file on the disk with our new file (after first rereading the file from the disk
 * to confirm the hash matches)
 * - Some files are super huge so we can't do the full atomic rename/copy (because of filesystem size limits).  If !fullAtomic
 * then we still do the readback to verify file is valid so higher level code can handle failures.
 */
class SafeFile : public Print
{
  public:
    explicit SafeFile(char const *filepath, bool fullAtomic = false);

    virtual size_t write(uint8_t);
    virtual size_t write(const uint8_t *buffer, size_t size);

    /**
     * Atomically close the file (deleting any old versions) and readback the contents to confirm the hash matches
     *
     * @return false for failure
     */
    bool close();

  private:
    /// Read our (closed) tempfile back in and compare the hash
    bool testReadback();

    String filename;
    File f;
    bool fullAtomic;
    uint8_t hash = 0;
};

#endif