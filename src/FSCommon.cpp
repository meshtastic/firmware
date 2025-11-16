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
#include "SdFat_Adafruit_Fork.h"
#include <SPI.h>
#include <Adafruit_SPIFlash.h>
#include "ff.h"
#include "diskio.h"
// up to 11 characters
#define DISK_LABEL "EXT FLASH"

//extern Adafruit_FlashTransport_QSPI flashTransport;
//extern Adafruit_SPIFlash flash;
extern FatVolume fatfs;
extern bool flashInitialized;
extern bool fatfsMounted;
#define EXTERNAL_FLASH_DEVICE
#define EXTERNAL_FLASH_USE_QSPI
#if defined(EXTERNAL_FLASH_USE_QSPI)
extern Adafruit_FlashTransport_QSPI flashTransport;
#endif
extern Adafruit_SPIFlash flash;

FatVolume fatfs;
bool flashInitialized = false;
bool fatfsMounted = false;
#define FILE_NAME "test2.txt"


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


void format_fat12(void) {
// Working buffer for f_mkfs.
#ifdef __AVR__
  uint8_t workbuf[512];
#else
  uint8_t workbuf[4096];
#endif

  // Elm Cham's fatfs objects
  FATFS elmchamFatfs;

  // Make filesystem.
  FRESULT r = f_mkfs("", FM_FAT, 0, workbuf, sizeof(workbuf));
  if (r != FR_OK) {
    LOG_ERROR("Error, f_mkfs failed");
    while (1)
      delay(1);
  }

  // mount to set disk label
  r = f_mount(&elmchamFatfs, "0:", 1);
  if (r != FR_OK) {
    LOG_ERROR("Error, f_mount failed");
    while (1)
      delay(1);
  }

  // Setting label
  LOG_INFO("Setting disk label to: " DISK_LABEL);
  r = f_setlabel(DISK_LABEL);
  if (r != FR_OK) {
    LOG_ERROR("Error, f_setlabel failed");
    while (1)
      delay(1);
  }

  // unmount
  f_unmount("0:");

  // sync to make sure all data is written to flash
  flash.syncBlocks();

  LOG_INFO("Formatted flash!");
}

void check_fat12(void) {
  // Check new filesystem
  if (!fatfs.begin(&flash)) {
    LOG_ERROR("Error, failed to mount newly formatted filesystem!");
    while (1)
      delay(1);
  }
}

bool copyFile(const char *from, const char *to)
{
#ifdef EXTERNAL_FLASH_DEVICE
    // take SPI Lock
    concurrency::LockGuard g(spiLock);
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
        byte i = f1.read(cbuffer, 16);
        f2.write(cbuffer, i);
    }

    f2.flush();
    f2.close();
    f1.close();
    return true;
#elif FSCom
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


// Helper for building full path
static void buildPath(char *dest, size_t destLen, const char *parent, const char *child) {
    if (strcmp(parent, "/") == 0) {
        snprintf(dest, destLen, "/%s", child);
    } else {
        snprintf(dest, destLen, "%s/%s", parent, child);
    }
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
#ifdef EXTERNAL_FLASH_DEVICE
    FatFile root;
    if (!root.open(dirname, O_READ)) {
        return filenames;
    }
    if (!root.isDir()) {
        return filenames;
    }

    FatFile file;
    char name[64];
    char path[128];
    while (file.openNext(&root, O_READ)) {
        file.getName(name, sizeof(name));
        if (file.isDir() && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            if (levels) {
                buildPath(path, sizeof(path), dirname, name);
                std::vector<meshtastic_FileInfo> subDirFilenames = getFiles(path, levels - 1);
                filenames.insert(filenames.end(), subDirFilenames.begin(), subDirFilenames.end());
            }
        } else if (!file.isDir()) {
            meshtastic_FileInfo fileInfo = {"", static_cast<uint32_t>(file.fileSize())};
            buildPath(fileInfo.file_name, sizeof(fileInfo.file_name), dirname, name);
            filenames.push_back(fileInfo);
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
 * Lists the contents of a directory.
 * We can't use SPILOCK here because of recursion. Callers of this function should use SPILOCK.
 *
 * @param dirname The name of the directory to list.
 * @param levels The number of levels of subdirectories to list.
 * @param del Whether or not to delete the contents of the directory after listing.
 */
void listDir(const char *dirname, uint8_t levels, bool del)
{
#ifdef EXTERNAL_FLASH_DEVICE
    FatFile root;
    if (!root.open(dirname, O_READ)) {
        return;
    }
    if (!root.isDir()) {
        return;
    }

    FatFile file;
    char name[64];
    char path[128];
    while (file.openNext(&root, O_READ)) {
        file.getName(name, sizeof(name));
        if (file.isDir() && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            if (levels) {
                buildPath(path, sizeof(path), dirname, name);
                listDir(path, levels - 1, del);
                if (del) {
                    LOG_DEBUG("Remove %s", path);
                    file.close();
                    fatfs.rmdir(path);
                    continue;
                }
            }
        } else if (!file.isDir()) {
            buildPath(path, sizeof(path), dirname, name);
            if (del) {
                LOG_DEBUG("Delete %s", path);
                file.close();
                fatfs.remove(path);
                continue;
            } else {
                LOG_DEBUG("   %s (%lu Bytes)", path, file.fileSize());
            }
        }
        file.close();
    }
    root.close();
#elif FSCom
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
    std::vector<meshtastic_FileInfo> files = getFiles(dirname, 10);
    for (auto const &fileInfo : files) {
        LOG_DEBUG("Delete %s", fileInfo.file_name);
        fatfs.remove(fileInfo.file_name);
    }
    // Finally remove the (now empty) directory itself
    LOG_DEBUG("Remove directory %s", dirname);
    fatfs.rmdir(dirname);
#endif
#ifdef FSCom

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
        if (!flash.begin()) {
            LOG_ERROR("Error, failed to initialize flash chip!");
            while (1) {
                delay(1);
            }
        }
        flashInitialized = true;
    }
    LOG_INFO("Flash chip JEDEC ID: 0x%X", flash.getJEDECID());
    //format_fat12();
    check_fat12();
    //LOG_INFO("Flash chip successfully formatted with new empty filesystem!");
    if (!fatfsMounted) {
        if (!fatfs.begin(&flash)) {
            LOG_ERROR("Error, failed to mount filesystem!");
            while (1) {
                delay(1);
            }
        }
        fatfsMounted = true;
        LOG_INFO("Filesystem mounted!");
    }
#endif
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