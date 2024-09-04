#pragma once

#include "configuration.h"
#include <vector>

// Cross platform filesystem API

#if defined(ARCH_PORTDUINO)
// Portduino version
#include "PortduinoFS.h"
#define FSCom PortduinoFS
#define FSBegin() true
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#endif

#if defined(ARCH_STM32WL)
// STM32WL series 2 Kbytes (8 rows of 256 bytes)
#include <EEPROM.h>
#include <OSFS.h>

// Useful consts
const OSFS::result noerr = OSFS::result::NO_ERROR;
const OSFS::result notfound = OSFS::result::FILE_NOT_FOUND;
#endif

#if defined(ARCH_RP2040)
// RP2040
#include "LittleFS.h"
#define FSCom LittleFS
#define FSBegin() FSCom.begin() // set autoformat
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#endif

#if defined(ARCH_ESP32)
// ESP32 version
#include "LittleFS.h"
#define FSCom LittleFS
#define FSBegin() FSCom.begin(true) // format on failure
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#endif

#if defined(ARCH_NRF52)
// NRF52 version
#include "InternalFileSystem.h"
#define FSCom InternalFS
#define FSBegin() FSCom.begin() // InternalFS formats on failure
using namespace Adafruit_LittleFS_Namespace;
#endif

void fsInit();
void fsListFiles();
bool copyFile(const char *from, const char *to);
bool renameFile(const char *pathFrom, const char *pathTo);
std::vector<meshtastic_FileInfo> getFiles(const char *dirname, uint8_t levels);
void listDir(const char *dirname, uint8_t levels, bool del = false);
void rmDir(const char *dirname);
void setupSDCard();

extern bool lfs_assert_failed; // Note: we use this global on all platforms, though it can only be set true on nrf52 (in our
                               // modified lfs_util.h)
