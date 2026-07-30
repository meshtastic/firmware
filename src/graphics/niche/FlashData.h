#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

/*

Re-usable NicheGraphics tool

Save settings / data to flash, without use of the Meshtastic Protobufs
Avoid bloating everyone's protobuf code for our one-off UI implementations

*/

#pragma once

#include "configuration.h"

#include "SPILock.h"
#include "SafeFile.h"

namespace NicheGraphics
{

template <typename T> class FlashData
{
  private:
    static std::string getFilename(const char *label)
    {
        std::string filename;
        filename += "/NicheGraphics";
        filename += "/";
        filename += label;
        filename += ".data";

        return filename;
    }

    static uint32_t getHash(T *data)
    {
        uint32_t hash = 0;

        // Sum all bytes of the image buffer together
        for (uint32_t i = 0; i < sizeof(T); i++)
            hash ^= ((uint8_t *)data)[i] + 1;

        return hash;
    }

  public:
    static bool load(T *data, const char *label)
    {
        // Take firmware's SPI lock
        concurrency::LockGuard guard(spiLock);

        // Set false if we run into issues
        bool okay = true;

        // Get a filename based on the label
        std::string filename = getFilename(label);

#ifdef FSCom

        // Check that the file *does* actually exist
        if (!FSCom.exists(filename.c_str())) {
            LOG_WARN("'%s' not found. Using default values", filename.c_str());
            okay = false;
            return okay;
        }

        // Open the file
        auto f = FSCom.open(filename.c_str(), FILE_O_READ);

        // If opened, start reading
        if (f) {
            LOG_INFO("Loading NicheGraphics data '%s'", filename.c_str());

            // Create an object which will received data from flash
            // We read here first, so we can verify the checksum, without committing to overwriting the *data object
            // Allows us to retain any defaults that might be set after we declared *data, but before loading settings,
            // in case the flash values are corrupt
            T flashData;

            // Read the actual data
            f.readBytes((char *)&flashData, sizeof(T));

            // Read the hash
            uint32_t savedHash = 0;
            f.readBytes((char *)&savedHash, sizeof(savedHash));

            // Calculate hash of the loaded data, then compare with the saved hash
            // If hash looks good, copy the values to the main data object
            uint32_t calculatedHash = getHash(&flashData);
            if (savedHash != calculatedHash) {
                LOG_WARN("'%s' is corrupt (hash mismatch). Using default values", filename.c_str());
                okay = false;
            } else
                *data = flashData;

            f.close();
        } else {
            LOG_ERROR("Could not open / read %s", filename.c_str());
            okay = false;
        }
#else
        LOG_ERROR("Filesystem not implemented");
        state = LoadFileState::NO_FILESYSTEM;
        okay = false;
#endif
        return okay;
    }

    // Save module's custom data (settings?) to flash. Doesn't use protobufs
    // Takes the firmware's SPI lock, in case the files are stored on SD card
    // Need to lock and unlock around specific FS methods, as the SafeFile class takes the lock for itself internally.
    static void save(T *data, const char *label)
    {
        // Get a filename based on the label
        std::string filename = getFilename(label);

#ifdef FSCom
        spiLock->lock();
        FSCom.mkdir("/NicheGraphics");
        spiLock->unlock();

        auto f = SafeFile(filename.c_str(), true); // "true": full atomic. Write new data to temp file, then rename.

        LOG_INFO("Saving %s", filename.c_str());

        // Calculate a hash of the data
        uint32_t hash = getHash(data);

        spiLock->lock();
        f.write((uint8_t *)data, sizeof(T));     // Write the actual data
        f.write((uint8_t *)&hash, sizeof(hash)); // Append the hash
        spiLock->unlock();

        bool writeSucceeded = f.close();

        if (!writeSucceeded) {
            LOG_ERROR("Can't write data!");
        }
#else
        LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
    }
};

// Erase contents of the NicheGraphics data directory
inline void clearFlashData()
{

    // Take firmware's SPI lock, in case the files are stored on SD card
    concurrency::LockGuard guard(spiLock);

#ifdef FSCom
    File dir = FSCom.open("/NicheGraphics"); // Open the directory
    File file = dir.openNextFile();          // Attempt to open the first file in the directory

    // While the directory still contains files
    while (file) {
        std::string path = "/NicheGraphics/";
        path += file.name();
        LOG_DEBUG("Erasing %s", path.c_str());
        file.close();
        FSCom.remove(path.c_str());

        file = dir.openNextFile();
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
}

} // namespace NicheGraphics

#endif