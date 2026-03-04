#pragma once

#include "mapps/FileBackend.h"

#if defined(HAS_SDCARD) && !defined(SDCARD_USE_SOFT_SPI)

#include "SPILock.h"
#include "DebugConfiguration.h"
#include <SD.h>

// FileBackend implementation wrapping SD card access with SPI lock.
class SDFileBackend : public FileBackend
{
  public:
    std::vector<DirEntry> listDir(const char *path) override
    {
        std::vector<DirEntry> result;

        concurrency::LockGuard g(spiLock);

        File root = SD.open(path);
        if (!root) {
            LOG_WARN("[SDFileBackend] SD.open('%s') failed", path);
            return result;
        }
        if (!root.isDirectory()) {
            LOG_WARN("[SDFileBackend] '%s' is not a directory", path);
            root.close();
            return result;
        }

        File entry;
        while ((entry = root.openNextFile())) {
            DirEntry de;
            de.name = entry.name();
            de.isDirectory = entry.isDirectory();
            LOG_DEBUG("[SDFileBackend] entry: '%s' isDir=%d", de.name.c_str(), de.isDirectory);
            result.push_back(std::move(de));
            entry.close();
        }
        root.close();

        LOG_INFO("[SDFileBackend] listDir('%s') returned %u entries", path, (unsigned)result.size());
        return result;
    }

    std::string readFile(const char *path) override
    {
        concurrency::LockGuard g(spiLock);

        File file = SD.open(path);
        if (!file) {
            LOG_WARN("[SDFileBackend] readFile('%s') open failed", path);
            return "";
        }

        size_t size = file.size();
        if (size == 0) {
            LOG_WARN("[SDFileBackend] readFile('%s') size=0", path);
            file.close();
            return "";
        }

        std::string content;
        content.resize(size);
        file.read((uint8_t *)content.data(), size);
        file.close();
        LOG_DEBUG("[SDFileBackend] readFile('%s') OK, %u bytes", path, (unsigned)size);
        return content;
    }

    bool writeFile(const char *path, const std::string &data) override
    {
        concurrency::LockGuard g(spiLock);

        File file = SD.open(path, FILE_WRITE);
        if (!file)
            return false;

        file.write((const uint8_t *)data.data(), data.size());
        file.close();
        return true;
    }
};

#endif // HAS_SDCARD
