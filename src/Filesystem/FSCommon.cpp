/**
 * @file FSCommon.cpp
 * @brief This file contains functions for common filesystem operations such as copying, renaming, listing and deleting files and
 * directories.
 *
 * The functions in this file are used to perform common filesystem operations such as copying, renaming, listing and deleting
 * files and directories. These functions are used in the Meshtastic-device project to manage files and directories on the
 * device's filesystem.
 *
 */
#include "Filesystem/FSCommon.h"
#include "SPILock.h"
#include "configuration.h"
#include <memory>
#ifdef USE_EXTERNAL_FLASH
#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport(PIN_QSPI_SCK, PIN_QSPI_CS, PIN_QSPI_IO0, PIN_QSPI_IO1, PIN_QSPI_IO2, PIN_QSPI_IO3);
#endif
Adafruit_SPIFlash flash(&flashTransport);
bool flashInitialized = false;
bool externalFSMounted = false;
#endif
// External flash is only present on a subset of nRF52 boards, but when enabled we share
// this single external filesystem instance across the entire firmware so every helper touches the same
// QSPI-backed block device via the Adafruit SPIFlash shim above.
// Software SPI is used by MUI so disable SD card here until it's also implemented
#if defined(HAS_SDCARD) && !defined(SDCARD_USE_SOFT_SPI)
#include <SD.h>
#include <SPI.h>

#ifdef SDCARD_USE_SPI1
SPIClass SPI_HSPI(HSPI);
#define SDHandler SPI_HSPI
#else
#define SDHandler SPI
#endif

#ifndef SD_SPI_FREQUENCY
#define SD_SPI_FREQUENCY 4000000U
#endif

#endif // HAS_SDCARD

/**
 * @brief Copies a file from one location to another.
 *
 * @param from The path of the source file.
 * @param to The path of the destination file.
 * @return true if the file was successfully copied, false otherwise.
 */
#ifdef USE_EXTERNAL_FLASH
bool formatExternalFS(void)
{
    concurrency::LockGuard g(spiLock);
    if (!externalFS.prepare(&flash)) {
        LOG_ERROR("Error, external LittleFS prepare failed");
        return false;
    }

    if (!externalFS.format()) {
        LOG_ERROR("Error, external LittleFS format failed, trying full chip erase");

        if (!flash.eraseChip()) {
            LOG_ERROR("Error, external flash chip erase failed");
            return false;
        }

        if (!externalFS.format()) {
            LOG_ERROR("Error, external LittleFS format failed after chip erase");
            return false;
        }
    }
    externalFSMounted = false;
    LOG_INFO("Formatted external flash!");
    return true;
}

bool checkExternalFS(void)
{
    if (!externalFS.begin(&flash)) {
        LOG_ERROR("Error, failed to mount newly formatted filesystem!");
        externalFSMounted = false;
        return false;
    }
    externalFSMounted = true;
    return true;
}
#endif

bool copyFile(const char *from, const char *to)
{
#ifdef USE_EXTERNAL_FLASH
    // take SPI Lock
    concurrency::LockGuard g(spiLock);
    unsigned char cbuffer[16];

    auto f1 = externalFS.open(from, FILE_O_READ);
    if (!f1) {
        LOG_ERROR("Failed to open source file %s", from);
        return false;
    }

    auto f2 = externalFS.open(to, FILE_O_WRITE);
    if (!f2) {
        LOG_ERROR("Failed to open destination file %s", to);
        return false;
    }

    while (f1.available() > 0) {
        byte i = f1.read(cbuffer, 16); // Read up to 16 bytes
        f2.write(cbuffer, i);          // Write the bytes to the destination file
    }

    f2.flush(); // Ensure all data is written to the flash
    f2.close();
    f1.close();
    return true;
#elif defined(FSCom)
    // take SPI Lock
    concurrency::LockGuard g(spiLock);
    unsigned char cbuffer[16];

    File f1 = FSCom.open(from, FILE_O_READ);
    if (!f1) {
        LOG_ERROR("Failed to open source file %s", from);
        return false;
    }

    File f2 = FSCom.open(to, FILE_O_WRITE);
    if (!f2) {
        LOG_ERROR("Failed to open destination file %s", to);
        return false;
    }

    while (f1.available() > 0) {
        byte i = f1.read(cbuffer, 16);
        f2.write(cbuffer, i);
    }

    f2.flush();
    f2.close();
    f1.close();
    return true;
#endif
}

/**
 * Renames a file from pathFrom to pathTo.
 *
 * @param pathFrom The original path of the file.
 * @param pathTo The new path of the file.
 *
 * @return True if the file was successfully renamed, false otherwise.
 */
bool renameFile(const char *pathFrom, const char *pathTo)
{
#ifdef USE_EXTERNAL_FLASH
    // take SPI Lock
    spiLock->lock();
    // Backend rename manipulates directory entries in place which is much faster than
    // copy/remove on the QSPI-backed external filesystem.
    bool result = externalFS.rename(pathFrom, pathTo);
    spiLock->unlock();
    return result;

#elif defined(FSCom)

#ifdef ARCH_ESP32
    // take SPI Lock
    spiLock->lock();
    // rename was fixed for ESP32 IDF LittleFS in April
    bool result = FSCom.rename(pathFrom, pathTo);
    spiLock->unlock();
    return result;
#else
    // copyFile does its own locking.
    if (copyFile(pathFrom, pathTo) && FSCom.remove(pathFrom)) {
        return true;
    } else {
        return false;
    }
#endif

#endif
}

#include <vector>

#ifdef USE_EXTERNAL_FLASH
// Helper for building full path and detecting truncation
static bool buildPath(char *dest, size_t destLen, const char *parent, const char *child)
{
    int written;
    if (strcmp(parent, "/") == 0) {
        written = snprintf(dest, destLen, "/%s", child);
    } else {
        written = snprintf(dest, destLen, "%s/%s", parent, child);
    }
    // Detect and guard against truncation or encoding errors; abort caller on failure.
    if (written < 0 || static_cast<size_t>(written) >= destLen) {
        LOG_ERROR("Path truncated for %s/%s", parent, child);
        return false;
    }
    return true;
}
#endif
/**
 * @brief Recursively retrieves information about all files in a directory and its subdirectories.
 *
 * This function traverses a directory structure and collects metadata about all files found,
 * including their full paths and sizes. It supports both external flash storage (via ExternalFSFile)
 * and standard filesystem implementations (FSCom).
 *
 * @param dirname The path to the directory to scan. Must be a valid directory path.
 * @param levels The maximum depth of subdirectories to traverse. Set to 0 to scan only
 *               the specified directory without recursion. Each level decrements this value
 *               when recursing into subdirectories.
 *
 * @return A vector of meshtastic_FileInfo structures, where each entry contains:
 *         - file_name: The full path to the file
 *         - file_size: The size of the file in bytes
 *         Returns an empty vector if the directory cannot be opened, is not a valid directory,
 *         or contains no accessible files.
 *
 * @note Directories named "." and ".." are skipped during traversal.
 * @note Files ending with "." are filtered out in the FSCom implementation.
 * @note The behavior differs slightly between platforms:
 *       - ARCH_ESP32: Uses file.path() for full path resolution
 *       - Other architectures: Uses file.name() for path construction
 */
std::vector<meshtastic_FileInfo> getFiles(const char *dirname, uint8_t levels)
{
    std::vector<meshtastic_FileInfo> filenames = {};
#ifdef USE_EXTERNAL_FLASH
    auto root = externalFS.open(dirname, FILE_O_READ);
    if (!root) {          // Failed to open directory
        return filenames; // Return empty list
    }
    if (!root.isDirectory()) { // Not a directory
        return filenames;      // Return empty list
    }

    auto file = root.openNextFile();
    // Keep local buffers aligned with the on-wire field width to avoid mismatched truncation handling.
    constexpr size_t FileNameFieldLen = sizeof(((meshtastic_FileInfo *)nullptr)->file_name);
    constexpr size_t NameBufLen = FileNameFieldLen;
    constexpr size_t PathBufLen = FileNameFieldLen;
    char name[NameBufLen] = {0}; // Buffer for file name
    char path[PathBufLen] = {0}; // Buffer for full path
    while (file) {               // Iterate through directory entries
        memset(name, 0, sizeof(name));
        strncpy(name, file.name(), sizeof(name) - 1);          // Get file name
        if (strnlen(name, sizeof(name)) >= sizeof(name) - 1) { // Detect truncation
            LOG_ERROR("Name truncated in getFiles: %s", name);
            file.close();
            return filenames; // Abort traversal on truncation
        }
        if (file.isDirectory() && strcmp(name, ".") != 0 &&
            strcmp(name, "..") != 0) {                               // if it's a directory and name is not . or ..
            if (levels) {                                            // Recurse into subdirectory
                if (!buildPath(path, sizeof(path), dirname, name)) { // Build full path
                    file.close();
                    return filenames; // Abort traversal on truncation
                }
                std::vector<meshtastic_FileInfo> subDirFilenames = getFiles(path, levels - 1);     // Recurse
                filenames.insert(filenames.end(), subDirFilenames.begin(), subDirFilenames.end()); // Append results
            }
        } else if (!file.isDirectory()) {                                            // if it's a file
            meshtastic_FileInfo fileInfo = {"", static_cast<uint32_t>(file.size())}; // Create file info struct
            if (!buildPath(fileInfo.file_name, sizeof(fileInfo.file_name), dirname, name)) {
                file.close();
                return filenames; // Abort traversal on truncation
            }
            filenames.push_back(fileInfo); // Add to list
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
#elif FSCom
    File root = FSCom.open(dirname, FILE_O_READ);
    if (!root)
        return filenames;
    if (!root.isDirectory())
        return filenames;

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory() && !String(file.name()).endsWith(".")) {
            if (levels) {
#ifdef ARCH_ESP32
                std::vector<meshtastic_FileInfo> subDirFilenames = getFiles(file.path(), levels - 1);
#else
                std::vector<meshtastic_FileInfo> subDirFilenames = getFiles(file.name(), levels - 1);
#endif
                filenames.insert(filenames.end(), subDirFilenames.begin(), subDirFilenames.end());
                file.close();
            }
        } else {
            meshtastic_FileInfo fileInfo = {"", static_cast<uint32_t>(file.size())};
#ifdef ARCH_ESP32
            strcpy(fileInfo.file_name, file.path());
#else
            strcpy(fileInfo.file_name, file.name());
#endif
            if (!String(fileInfo.file_name).endsWith(".")) {
                filenames.push_back(fileInfo);
            }
            file.close();
        }
        file = root.openNextFile();
    }
    root.close();
#endif
    return filenames;
}

/**
 * Recursively iterate over a directory tree and optionally delete its contents.
 *
 * We can't use SPILOCK here because of recursion. Callers of this function should use SPILOCK.
 * The function enumerates files and subdirectories within the specified
 * `dirname`, logging each entry. When `levels` is greater than zero, it will
 * descend into subdirectories up to the given depth. If `del` is true, it
 * performs a depth-first traversal to remove files and directories after
 * processing their contents, ensuring directory deletions occur after all
 * child entries are handled. Platform-specific filesystem backends are used
 * for both listing and deletion.
 *
 * @param dirname Path to the directory to process.
 * @param levels  Maximum recursion depth for traversing subdirectories.
 * @param del     When true, delete files and directories instead of only listing.
 */
void listDir(const char *dirname, uint8_t levels, bool del)
{
#ifdef USE_EXTERNAL_FLASH
    auto root = externalFS.open(dirname, FILE_O_READ);
    if (!root) { // Failed to open directory
        return;
    }
    if (!root.isDirectory()) { // Not a directory
        return;
    }

    auto file = root.openNextFile();
    // Keep local buffers aligned with the on-wire field width to avoid mismatched truncation handling.
    constexpr size_t FileNameFieldLen = sizeof(((meshtastic_FileInfo *)nullptr)->file_name);
    constexpr size_t NameBufLen = FileNameFieldLen;
    constexpr size_t PathBufLen = FileNameFieldLen;
    char name[NameBufLen] = {0};
    char path[PathBufLen] = {0};
    while (file) { // Iterate through directory entries
        memset(name, 0, sizeof(name));
        strncpy(name, file.name(), sizeof(name) - 1); // Get file name
        if (strnlen(name, sizeof(name)) >= sizeof(name) - 1) {
            LOG_ERROR("Name truncated in listDir: %s", name);
            file.close();
            return; // Abort traversal on truncation
        }
        if (file.isDirectory() && strcmp(name, ".") != 0 &&
            strcmp(name, "..") != 0) {                               // if it's a directory and name is not . or ..
            if (levels) {                                            // Recurse into subdirectory
                if (!buildPath(path, sizeof(path), dirname, name)) { // Build full path
                    file.close();
                    return; // Abort traversal on truncation
                }
                listDir(path, levels - 1, del); // Recurse
                if (del) {                      // After recursion, delete directory if requested
                    LOG_DEBUG("Remove %s", path);
                    file.close();
                    // externalFS::rmdir is the only recursive delete we have, so walk depth
                    // first and remove directories once their contents have been handled.
                    externalFS.rmdir(path); // Remove directory
                    continue;
                }
            }
        } else if (!file.isDirectory()) {                        // if it's a file
            if (!buildPath(path, sizeof(path), dirname, name)) { // Build full path
                file.close();
                return; // Abort traversal on truncation
            }
            if (del) { // Delete file
                LOG_DEBUG("Delete %s", path);
                file.close();
                // externalFS::remove issues the backend delete, ensuring we do not
                // leave orphaned clusters on the external flash.
                externalFS.remove(path);
                continue;
            } else {
                LOG_DEBUG("   %s (%lu Bytes)", path, file.size());
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
#else
#if (defined(ARCH_ESP32) || defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    char buffer[255];
#endif
    File root = FSCom.open(dirname, FILE_O_READ);
    if (!root) {
        return;
    }
    if (!root.isDirectory()) {
        return;
    }

    File file = root.openNextFile();
    while (
        file &&
        file.name()[0]) { // This file.name() check is a workaround for a bug in the Adafruit LittleFS nrf52 glue (see issue 4395)
        if (file.isDirectory() && !String(file.name()).endsWith(".")) {
            if (levels) {
#ifdef ARCH_ESP32
                listDir(file.path(), levels - 1, del);
                if (del) {
                    LOG_DEBUG("Remove %s", file.path());
                    strncpy(buffer, file.path(), sizeof(buffer));
                    file.close();
                    FSCom.rmdir(buffer);
                } else {
                    file.close();
                }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
                listDir(file.name(), levels - 1, del);
                if (del) {
                    LOG_DEBUG("Remove %s", file.name());
                    strncpy(buffer, file.name(), sizeof(buffer));
                    file.close();
                    FSCom.rmdir(buffer);
                } else {
                    file.close();
                }
#else
                LOG_DEBUG(" %s (directory)", file.name());
                listDir(file.name(), levels - 1, del);
                file.close();
#endif
            }
        } else {
#ifdef ARCH_ESP32
            if (del) {
                LOG_DEBUG("Delete %s", file.path());
                strncpy(buffer, file.path(), sizeof(buffer));
                file.close();
                FSCom.remove(buffer);
            } else {
                LOG_DEBUG(" %s (%i Bytes)", file.path(), file.size());
                file.close();
            }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
            if (del) {
                LOG_DEBUG("Delete %s", file.name());
                strncpy(buffer, file.name(), sizeof(buffer));
                file.close();
                FSCom.remove(buffer);
            } else {
                LOG_DEBUG(" %s (%i Bytes)", file.name(), file.size());
                file.close();
            }
#else
            LOG_DEBUG("   %s (%i Bytes)", file.name(), file.size());
            file.close();
#endif
        }
        file = root.openNextFile();
    }
#ifdef ARCH_ESP32
    if (del) {
        LOG_DEBUG("Remove %s", root.path());
        strncpy(buffer, root.path(), sizeof(buffer));
        root.close();
        FSCom.rmdir(buffer);
    } else {
        root.close();
    }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    if (del) {
        LOG_DEBUG("Remove %s", root.name());
        strncpy(buffer, root.name(), sizeof(buffer));
        root.close();
        FSCom.rmdir(buffer);
    } else {
        root.close();
    }
#else
    root.close();
#endif
#endif
}

/**
 * @brief Removes a directory and all its contents.
 *
 * This function recursively removes a directory and all its contents, including subdirectories and files.
 *
 * @param dirname The name of the directory to remove.
 */
void rmDir(const char *dirname)
{
#ifdef USE_EXTERNAL_FLASH
    // The external filesystem implementation does not support recursive delete, so we do it manually here
    std::vector<meshtastic_FileInfo> files = getFiles(dirname, 10); // Get all files in directory and subdirectories
    for (auto const &fileInfo : files) {                            // Iterate through files and delete them
        LOG_DEBUG("Delete %s", fileInfo.file_name);
        externalFS.remove(fileInfo.file_name); // Delete file
    }
    // Finally remove the (now empty) directory itself
    LOG_DEBUG("Remove directory %s", dirname);
    externalFS.rmdir(dirname);
#elif defined(FSCom)

#if (defined(ARCH_ESP32) || defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    listDir(dirname, 10, true);
#elif defined(ARCH_NRF52)
    // nRF52 implementation of LittleFS has a recursive delete function
    FSCom.rmdir_r(dirname);
#endif

#endif
}

/**
 * Some platforms (nrf52) might need to do an extra step before FSBegin().
 */
__attribute__((weak, noinline)) void preFSBegin() {}

void fsInit()
{
#ifdef USE_EXTERNAL_FLASH
    if (!flashInitialized) {
        LOG_INFO("Adafruit SPI Flash external FS initialization!");
        if (!flash.begin()) {
            LOG_ERROR("Error, failed to initialize flash chip!");
            flashInitialized = false;
            return;
        }
        flashInitialized = true;
    }

    if (!flashInitialized) {
        LOG_ERROR("External flash is not initialized, skipping external FS init");
        return;
    }

    LOG_INFO("Flash chip JEDEC ID: 0x%X", flash.getJEDECID());
    /*
     * Testing helper: force format on every boot to validate recovery from internal flash mirror.
     * Enable with build flag: -DMESHTASTIC_TEST_FORMAT_EXTERNAL_FS_ON_BOOT
     */
#if defined(MESHTASTIC_TEST_FORMAT_EXTERNAL_FS_ON_BOOT)
    LOG_WARN("MESHTASTIC_TEST_FORMAT_EXTERNAL_FS_ON_BOOT enabled: formatting external flash on boot");
    if (!formatExternalFS()) {
        LOG_ERROR("formatExternalFS failed during fsInit test mode");
        return;
    }
#endif

    if (!checkExternalFS()) {
        LOG_WARN("checkExternalFS failed during fsInit, attempting recovery format");
        if (!formatExternalFS()) {
            LOG_ERROR("formatExternalFS failed during fsInit recovery");
            return;
        }
        if (!checkExternalFS()) {
            LOG_ERROR("checkExternalFS failed during fsInit recovery");
            return;
        }
    }
    if (!externalFSMounted) {
        if (!externalFS.begin(&flash)) {
            LOG_ERROR("Error, failed to mount filesystem!");
            return;
        }
        externalFSMounted = true;
        LOG_INFO("Filesystem mounted!");
    }
#elif defined(FSCom)
    concurrency::LockGuard g(spiLock);
    preFSBegin();
    if (!FSBegin()) {
        LOG_ERROR("Filesystem mount failed");
        // assert(0); This auto-formats the partition, so no need to fail here.
    }
#if defined(ARCH_ESP32)
    LOG_DEBUG("Filesystem files (%d/%d Bytes):", FSCom.usedBytes(), FSCom.totalBytes());
#else
    LOG_DEBUG("Filesystem files:");
#endif
    listDir("/", 10);
#endif
}

/**
 * Initializes the SD card and mounts the file system.
 */
void setupSDCard()
{
#if defined(HAS_SDCARD) && !defined(SDCARD_USE_SOFT_SPI)
    concurrency::LockGuard g(spiLock);
    SDHandler.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (!SD.begin(SDCARD_CS, SDHandler, SD_SPI_FREQUENCY)) {
        LOG_DEBUG("No SD_MMC card detected");
        return;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        LOG_DEBUG("No SD_MMC card attached");
        return;
    }
    LOG_DEBUG("SD_MMC Card Type: ");
    if (cardType == CARD_MMC) {
        LOG_DEBUG("MMC");
    } else if (cardType == CARD_SD) {
        LOG_DEBUG("SDSC");
    } else if (cardType == CARD_SDHC) {
        LOG_DEBUG("SDHC");
    } else {
        LOG_DEBUG("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    LOG_DEBUG("SD Card Size: %lu MB", (uint32_t)cardSize);
    LOG_DEBUG("Total space: %lu MB", (uint32_t)(SD.totalBytes() / (1024 * 1024)));
    LOG_DEBUG("Used space: %lu MB", (uint32_t)(SD.usedBytes() / (1024 * 1024)));
#endif
}