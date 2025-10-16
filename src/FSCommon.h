#pragma once

#include "configuration.h"
#include <vector>

enum LoadFileResult {
    // Successfully opened the file
    LOAD_SUCCESS = 1,
    // File does not exist
    NOT_FOUND = 2,
    // Device does not have a filesystem
    NO_FILESYSTEM = 3,
    // File exists, but could not decode protobufs
    DECODE_FAILED = 4,
    // File exists, but open failed for some reason
    OTHER_FAILURE = 5
};

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
// STM32WL
#include "LittleFS.h"
#define FSCom InternalFS
#define FSBegin() FSCom.begin()
using namespace STM32_LittleFS_Namespace;
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
LoadFileResult loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct);

bool saveProto(const char *filename, size_t protoSize, const pb_msgdesc_t *fields, const void *dest_struct,
               bool fullAtomic = true);