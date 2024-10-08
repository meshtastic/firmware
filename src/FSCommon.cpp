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
#include "configuration.h"

#ifdef HAS_SDCARD
#include <SD.h>
#include <SPI.h>

#ifdef SDCARD_USE_SPI1
SPIClass SPI1(HSPI);
#define SDHandler SPI1
#else
#define SDHandler SPI
#endif

#endif // HAS_SDCARD

#if defined(ARCH_STM32WL)

uint16_t OSFS::startOfEEPROM = 1;
uint16_t OSFS::endOfEEPROM = 2048;

// 3) How do I read from the medium?
void OSFS::readNBytes(uint16_t address, unsigned int num, byte *output)
{
    for (uint16_t i = address; i < address + num; i++) {
        *output = EEPROM.read(i);
        output++;
    }
}

// 4) How to I write to the medium?
void OSFS::writeNBytes(uint16_t address, unsigned int num, const byte *input)
{
    for (uint16_t i = address; i < address + num; i++) {
        EEPROM.update(i, *input);
        input++;
    }
}
#endif

bool lfs_assert_failed =
    false; // Note: we use this global on all platforms, though it can only be set true on nrf52 (in our modified lfs_util.h)

extern "C" void lfs_assert(const char *reason)
{
    LOG_ERROR("LFS assert: %s\n", reason);
    lfs_assert_failed = true;
}

/**
 * @brief Copies a file from one location to another.
 *
 * @param from The path of the source file.
 * @param to The path of the destination file.
 * @return true if the file was successfully copied, false otherwise.
 */
bool copyFile(const char *from, const char *to)
{
#ifdef ARCH_STM32WL
    unsigned char cbuffer[2048];

    // Var to hold the result of actions
    OSFS::result r;

    r = OSFS::getFile(from, cbuffer);

    if (r == notfound) {
        LOG_ERROR("Failed to open source file %s\n", from);
        return false;
    } else if (r == noerr) {
        r = OSFS::newFile(to, cbuffer, true);
        if (r == noerr) {
            return true;
        } else {
            LOG_ERROR("OSFS Error %d\n", r);
            return false;
        }

    } else {
        LOG_ERROR("OSFS Error %d\n", r);
        return false;
    }
    return true;

#elif defined(FSCom)
    unsigned char cbuffer[16];

    File f1 = FSCom.open(from, FILE_O_READ);
    if (!f1) {
        LOG_ERROR("Failed to open source file %s\n", from);
        return false;
    }

    File f2 = FSCom.open(to, FILE_O_WRITE);
    if (!f2) {
        LOG_ERROR("Failed to open destination file %s\n", to);
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
#ifdef ARCH_STM32WL
    if (copyFile(pathFrom, pathTo) && (OSFS::deleteFile(pathFrom) == OSFS::result::NO_ERROR)) {
        return true;
    } else {
        return false;
    }
#elif defined(FSCom)
#ifdef ARCH_ESP32
    // rename was fixed for ESP32 IDF LittleFS in April
    return FSCom.rename(pathFrom, pathTo);
#else
    if (copyFile(pathFrom, pathTo) && FSCom.remove(pathFrom)) {
        return true;
    } else {
        return false;
    }
#endif
#endif
}

#include <vector>

/**
 * @brief Get the list of files in a directory.
 *
 * This function returns a list of files in a directory. The list includes the full path of each file.
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
            meshtastic_FileInfo fileInfo = {"", file.size()};
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
 *
 * @param dirname The name of the directory to list.
 * @param levels The number of levels of subdirectories to list.
 * @param del Whether or not to delete the contents of the directory after listing.
 */
void listDir(const char *dirname, uint8_t levels, bool del)
{
#ifdef FSCom
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
                    LOG_DEBUG("Removing %s\n", file.path());
                    strncpy(buffer, file.path(), sizeof(buffer));
                    file.close();
                    FSCom.rmdir(buffer);
                } else {
                    file.close();
                }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
                listDir(file.name(), levels - 1, del);
                if (del) {
                    LOG_DEBUG("Removing %s\n", file.name());
                    strncpy(buffer, file.name(), sizeof(buffer));
                    file.close();
                    FSCom.rmdir(buffer);
                } else {
                    file.close();
                }
#else
                LOG_DEBUG(" %s (directory)\n", file.name());
                listDir(file.name(), levels - 1, del);
                file.close();
#endif
            }
        } else {
#ifdef ARCH_ESP32
            if (del) {
                LOG_DEBUG("Deleting %s\n", file.path());
                strncpy(buffer, file.path(), sizeof(buffer));
                file.close();
                FSCom.remove(buffer);
            } else {
                LOG_DEBUG(" %s (%i Bytes)\n", file.path(), file.size());
                file.close();
            }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
            if (del) {
                LOG_DEBUG("Deleting %s\n", file.name());
                strncpy(buffer, file.name(), sizeof(buffer));
                file.close();
                FSCom.remove(buffer);
            } else {
                LOG_DEBUG(" %s (%i Bytes)\n", file.name(), file.size());
                file.close();
            }
#else
            LOG_DEBUG("   %s (%i Bytes)\n", file.name(), file.size());
            file.close();
#endif
        }
        file = root.openNextFile();
    }
#ifdef ARCH_ESP32
    if (del) {
        LOG_DEBUG("Removing %s\n", root.path());
        strncpy(buffer, root.path(), sizeof(buffer));
        root.close();
        FSCom.rmdir(buffer);
    } else {
        root.close();
    }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    if (del) {
        LOG_DEBUG("Removing %s\n", root.name());
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
#ifdef FSCom
#if (defined(ARCH_ESP32) || defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    listDir(dirname, 10, true);
#elif defined(ARCH_NRF52)
    // nRF52 implementation of LittleFS has a recursive delete function
    FSCom.rmdir_r(dirname);
#endif
#endif
}

void fsInit()
{
#ifdef FSCom
    if (!FSBegin()) {
        LOG_ERROR("Filesystem mount Failed.\n");
        // assert(0); This auto-formats the partition, so no need to fail here.
    }
#if defined(ARCH_ESP32)
    LOG_DEBUG("Filesystem files (%d/%d Bytes):\n", FSCom.usedBytes(), FSCom.totalBytes());
#else
    LOG_DEBUG("Filesystem files:\n");
#endif
    listDir("/", 10);
#endif
}

/**
 * Initializes the SD card and mounts the file system.
 */
void setupSDCard()
{
#ifdef HAS_SDCARD
    SDHandler.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

    if (!SD.begin(SDCARD_CS, SDHandler)) {
        LOG_DEBUG("No SD_MMC card detected\n");
        return;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        LOG_DEBUG("No SD_MMC card attached\n");
        return;
    }
    LOG_DEBUG("SD_MMC Card Type: ");
    if (cardType == CARD_MMC) {
        LOG_DEBUG("MMC\n");
    } else if (cardType == CARD_SD) {
        LOG_DEBUG("SDSC\n");
    } else if (cardType == CARD_SDHC) {
        LOG_DEBUG("SDHC\n");
    } else {
        LOG_DEBUG("UNKNOWN\n");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    LOG_DEBUG("SD Card Size: %lu MB\n", (uint32_t)cardSize);
    LOG_DEBUG("Total space: %lu MB\n", (uint32_t)(SD.totalBytes() / (1024 * 1024)));
    LOG_DEBUG("Used space: %lu MB\n", (uint32_t)(SD.usedBytes() / (1024 * 1024)));
#endif
}