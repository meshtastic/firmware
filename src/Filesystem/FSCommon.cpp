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

FatVolume fatfs;
bool flashInitialized = false;
bool fatfsMounted = false;
#endif
// External flash is only present on a subset of nRF52 boards, but when enabled we share
// this single FatFs instance across the entire firmware so every helper touches the same
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
bool format_fat12(void)
{
    spiLock->lock();
    // This is an emergency formatter that rewrites the external flash with a clean FAT12
    // volume so the higher level preference code can start dropping files immediately

    // Allocate formatting buffer on the heap so it isn't permanently pinned in RAM.
    // Use a smaller buffer (512 bytes) on AVR platforms due to limited RAM constraints,
    // and a larger buffer (4096 bytes) on other platforms for faster formatting performance.
    // The unique_ptr with nothrow ensures automatic cleanup and safe error handling if
    // memory allocation fails.
#ifdef __AVR__
    std::unique_ptr<uint8_t[]> workbuf(new (std::nothrow) uint8_t[512]);
#else
    std::unique_ptr<uint8_t[]> workbuf(new (std::nothrow) uint8_t[4096]);
#endif
    if (!workbuf) {
        LOG_ERROR("Error, failed to allocate format buffer");
        spiLock->unlock();
        return false;
    }

    // Elm Cham's fatfs objects
    FATFS elmchamFatfs;

    // Make filesystem.
    FRESULT r = f_mkfs("", FM_FAT, 0, workbuf.get(),
#ifdef __AVR__
                       512
#else
                       4096
#endif
    );
    if (r != FR_OK) {
        LOG_ERROR("Error, f_mkfs failed");
        spiLock->unlock();
        return false;
    }

    // mount to set disk label
    r = f_mount(&elmchamFatfs, "0:", 1);
    if (r != FR_OK) {
        LOG_ERROR("Error, f_mount failed");
        spiLock->unlock();
        return false;
    }

    // Setting label
    LOG_INFO("Setting disk label to: " DISK_LABEL);
    r = f_setlabel(DISK_LABEL);
    if (r != FR_OK) {
        LOG_ERROR("Error, f_setlabel failed");
        spiLock->unlock();
        return false;
    }

    // unmount
    f_unmount("0:");

    // sync to make sure all data is written to flash
    flash.syncBlocks();

    LOG_INFO("Formatted external flash!");
    spiLock->unlock();
    return true;
}

bool check_fat12(void)
{
    spiLock->lock();
    // After formatting (or on first boot) make sure the freshly created filesystem actually
    // mounts against the shared FatVolume instance before the rest of the stack uses it.
    // Check new filesystem
    if (!fatfs.begin(&flash)) {
        LOG_ERROR("Error, failed to mount newly formatted filesystem!");
        spiLock->unlock();
        return false;
    }
    spiLock->unlock();
    return true;
}
#endif

bool copyFile(const char *from, const char *to)
{
#ifdef USE_EXTERNAL_FLASH
    // take SPI Lock
    concurrency::LockGuard g(spiLock);
    // External flash path uses the lightweight FatFile API because SdFat works directly
    // on the FatVolume we own above; this avoids instantiating the Arduino FS shim here.
    unsigned char cbuffer[16];

    FatFile f1;
    if (!f1.open(from, O_READ)) { // Open from root
        LOG_ERROR("Failed to open source file %s", from);
        return false;
    }

    FatFile f2;
    if (!f2.open(to, O_WRITE | O_CREAT | O_TRUNC)) { // Open from root
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
    // FatVolume::rename manipulates directory entries in place which is much faster than
    // copy/remove when the QSPI flash already exposes FAT semantics.
    bool result = fatfs.rename(pathFrom, pathTo);
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
 * including their full paths and sizes. It supports both external flash storage (via FatFile)
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
    FatFile root;
    if (!root.open(dirname, O_READ)) { // Failed to open directory
        return filenames;              // Return empty list
    }
    if (!root.isDir()) {  // Not a directory
        return filenames; // Return empty list
    }

    FatFile file;
    // Keep local buffers aligned with the on-wire field width to avoid mismatched truncation handling.
    constexpr size_t FileNameFieldLen = sizeof(((meshtastic_FileInfo *)nullptr)->file_name);
    constexpr size_t NameBufLen = FileNameFieldLen;
    constexpr size_t PathBufLen = FileNameFieldLen;
    char name[NameBufLen] = {0};                                                 // Buffer for file name
    char path[PathBufLen] = {0};                                                 // Buffer for full path
    while (file.openNext(&root, O_READ)) {                                       // Iterate through directory entries
        memset(name, 0, sizeof(name));
        file.getName(name, sizeof(name));                                        // Get file name
        if (strnlen(name, sizeof(name)) >= sizeof(name) - 1) {                   // Detect truncation
            LOG_ERROR("Name truncated in getFiles: %s", name);
            file.close();
            return filenames;                                                    // Abort traversal on truncation
        }
        if (file.isDir() && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) { // if it's a directory and name is not . or ..
            if (levels) {                                                        // Recurse into subdirectory
                if (!buildPath(path, sizeof(path), dirname, name)) {             // Build full path
                    file.close();
                    return filenames;                                            // Abort traversal on truncation
                }
                std::vector<meshtastic_FileInfo> subDirFilenames = getFiles(path, levels - 1);     // Recurse
                filenames.insert(filenames.end(), subDirFilenames.begin(), subDirFilenames.end()); // Append results
            }
        } else if (!file.isDir()) {                                                      // if it's a file
            meshtastic_FileInfo fileInfo = {"", static_cast<uint32_t>(file.fileSize())}; // Create file info struct
            if (!buildPath(fileInfo.file_name, sizeof(fileInfo.file_name), dirname, name)) {
                file.close();
                return filenames;                                                // Abort traversal on truncation
            }
            filenames.push_back(fileInfo);                                               // Add to list
        }
        file.close();
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
    FatFile root;
    if (!root.open(dirname, O_READ)) { // Failed to open directory
        return;
    }
    if (!root.isDir()) { // Not a directory
        return;
    }

    FatFile file;
    // Keep local buffers aligned with the on-wire field width to avoid mismatched truncation handling.
    constexpr size_t FileNameFieldLen = sizeof(((meshtastic_FileInfo *)nullptr)->file_name);
    constexpr size_t NameBufLen = FileNameFieldLen;
    constexpr size_t PathBufLen = FileNameFieldLen;
    char name[NameBufLen] = {0};
    char path[PathBufLen] = {0};
    while (file.openNext(&root, O_READ)) {                                       // Iterate through directory entries
        memset(name, 0, sizeof(name));
        file.getName(name, sizeof(name));                                        // Get file name
        if (strnlen(name, sizeof(name)) >= sizeof(name) - 1) {
            LOG_ERROR("Name truncated in listDir: %s", name);
            file.close();
            return;                                                                // Abort traversal on truncation
        }
        if (file.isDir() && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) { // if it's a directory and name is not . or ..
            if (levels) {                                                        // Recurse into subdirectory
                if (!buildPath(path, sizeof(path), dirname, name)) {             // Build full path
                    file.close();
                    return;                                                      // Abort traversal on truncation
                }
                listDir(path, levels - 1, del);                                  // Recurse
                if (del) {                                                       // After recursion, delete directory if requested
                    LOG_DEBUG("Remove %s", path);
                    file.close();
                    // FatVolume::rmdir is the only recursive delete we have, so walk depth
                    // first and remove directories once their contents have been handled.
                    fatfs.rmdir(path); // Remove directory
                    continue;
                }
            }
        } else if (!file.isDir()) {                       // if it's a file
            if (!buildPath(path, sizeof(path), dirname, name)) { // Build full path
                file.close();
                return;                                                          // Abort traversal on truncation
            }
            if (del) {                                    // Delete file
                LOG_DEBUG("Delete %s", path);
                file.close();
                // FatVolume::remove issues the low level FAT delete, ensuring we do not
                // leave orphaned clusters on the external flash.
                fatfs.remove(path);
                continue;
            } else {
                LOG_DEBUG("   %s (%lu Bytes)", path, file.fileSize());
            }
        }
        file.close();
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
    // Adafruit SPI Flash FatFs implementation does not support recursive delete, so we do it manually here
    std::vector<meshtastic_FileInfo> files = getFiles(dirname, 10); // Get all files in directory and subdirectories
    for (auto const &fileInfo : files) {                            // Iterate through files and delete them
        LOG_DEBUG("Delete %s", fileInfo.file_name);
        fatfs.remove(fileInfo.file_name); // Delete file
    }
    // Finally remove the (now empty) directory itself
    LOG_DEBUG("Remove directory %s", dirname);
    fatfs.rmdir(dirname);
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
        LOG_INFO("Adafruit SPI Flash FatFs initialization!");
        flashInitialized = true;
        if (!flash.begin()) {
            LOG_ERROR("Error, failed to initialize flash chip!");
            flashInitialized = false;
        }
    }
    LOG_INFO("Flash chip JEDEC ID: 0x%X", flash.getJEDECID());
    /*   Uncomment to auto-format on init the external flash to test backup restoring functionality from internal flash.
         If it works correctly, the board should boot normally after this, loosing only the node db but restoring
         all configuration and preferences from internal flash mirror.
    if (!format_fat12()) {
        LOG_ERROR("format_fat12 failed during fsInit");
        return;
    }
    */
    if (!check_fat12()) {
        LOG_ERROR("check_fat12 failed during fsInit");
        return;
    }
    if (!fatfsMounted) {
        if (!fatfs.begin(&flash)) {
            LOG_ERROR("Error, failed to mount filesystem!");
            return;
        }
        fatfsMounted = true;
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