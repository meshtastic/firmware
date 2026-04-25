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
#include "FSCommon.h"
#include "SPILock.h"
#include "configuration.h"

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
bool copyFile(const char *from, const char *to)
{
#ifdef FSCom
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
#ifdef FSCom
    spiLock->lock();
    bool result = FSCom.rename(pathFrom, pathTo);
    spiLock->unlock();
    return result;
#else
    return false;
#endif
}

#include <vector>

/**
 * @brief Platform-agnostic filesystem format / wipe.
 *
 * On embedded targets (ESP32, NRF52, STM32WL, RP2040) this calls the
 * native FSCom.format() which erases and reinitialises the LittleFS
 * partition.
 *
 * On Portduino the fs::FS backend has no format() method. We instead
 * delete /prefs (the only meshtastic data directory written at runtime)
 * and return. rmDir("/prefs") is already called unconditionally by
 * factoryReset() so this is a proven primitive on Portduino.
 * FSBegin() is a no-op (#define FSBegin() true) on Portduino.
 *
 * @return true on success, false on failure or if no filesystem is configured.
 */
bool fsFormat()
{
#ifdef FSCom
#if defined(ARCH_PORTDUINO)
    rmDir("/prefs");
    return FSBegin();
#else
    return FSCom.format();
#endif
#else
    return false;
#endif
}

/**
 * @brief Get the list of files in a directory.
 *
 * This function returns a list of files in a directory. The list includes the full path of each file.
 * We can't use SPILOCK here because of recursion. Callers of this function should use SPILOCK.
 *
 * @param dirname The name of the directory.
 * @param levels The number of levels of subdirectories to list.
 * @return A vector of strings containing the full path of each file in the directory.
 */
std::vector<meshtastic_FileInfo> getFiles(const char *dirname, uint8_t levels)
{
    std::vector<meshtastic_FileInfo> filenames = {};
#ifdef FSCom
    File root = FSCom.open(dirname, FILE_O_READ);
    if (!root)
        return filenames;
    if (!root.isDirectory())
        return filenames;

    File file = root.openNextFile();
    while (file) {
#ifdef ARCH_ESP32
        const char *filepath = file.path();
#else
        const char *filepath = file.name();
#endif
        if (file.isDirectory() && !String(file.name()).endsWith(".")) {
            if (levels) {
                std::vector<meshtastic_FileInfo> subDirFilenames = getFiles(filepath, levels - 1);
                filenames.insert(filenames.end(), subDirFilenames.begin(), subDirFilenames.end());
                file.close();
            }
        } else {
            meshtastic_FileInfo fileInfo = {"", static_cast<uint32_t>(file.size())};
            strncpy(fileInfo.file_name, filepath, sizeof(fileInfo.file_name) - 1);
            fileInfo.file_name[sizeof(fileInfo.file_name) - 1] = '\0';
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
 * Lists the contents of a directory.
 * We can't use SPILOCK here because of recursion. Callers of this function should use SPILOCK.
 *
 * @param dirname The name of the directory to list.
 * @param levels The number of levels of subdirectories to list.
 * @param del Whether or not to delete the contents of the directory after listing.
 */
void listDir(const char *dirname, uint8_t levels, bool del)
{
#ifdef FSCom
    char buffer[255];
    File root = FSCom.open(dirname, FILE_O_READ);
    if (!root || !root.isDirectory())
        return;

    File file = root.openNextFile();
    while (file && file.name()[0]) { // file.name()[0] check: workaround for Adafruit LittleFS nRF52 bug #4395
#ifdef ARCH_ESP32
        const char *filepath = file.path();
#else
        const char *filepath = file.name();
#endif
        if (file.isDirectory() && !String(file.name()).endsWith(".")) {
            if (levels) {
                listDir(filepath, levels - 1, del);
                if (del) {
                    LOG_DEBUG("Remove %s", filepath);
                    strncpy(buffer, filepath, sizeof(buffer) - 1);
                    buffer[sizeof(buffer) - 1] = '\0';
                    file.close();
                    FSCom.rmdir(buffer);
                } else {
                    file.close();
                }
            }
        } else {
            if (del) {
                LOG_DEBUG("Delete %s", filepath);
                strncpy(buffer, filepath, sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
                file.close();
                FSCom.remove(buffer);
            } else {
                LOG_DEBUG(" %s (%i Bytes)", filepath, file.size());
                file.close();
            }
        }
        file = root.openNextFile();
    }
#ifdef ARCH_ESP32
    const char *rootpath = root.path();
#else
    const char *rootpath = root.name();
#endif
    if (del) {
        LOG_DEBUG("Remove %s", rootpath);
        strncpy(buffer, rootpath, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        root.close();
        FSCom.rmdir(buffer);
    } else {
        root.close();
    }
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
#ifdef FSCom
    listDir(dirname, 10, true);
#endif
}

/**
 * Some platforms (nrf52) might need to do an extra step before FSBegin().
 */
__attribute__((weak, noinline)) void preFSBegin() {}

void fsInit()
{
#ifdef FSCom
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