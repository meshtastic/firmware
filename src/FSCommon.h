#pragma once

#include "configuration.h"

// Cross platform filesystem API

#if defined(ARCH_PORTDUINO)
// Portduino version
#include "PortduinoFS.h"
#define FSCom PortduinoFS
#define FSBegin() true
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#endif

#if defined(ARCH_RP2040)
// RP2040
#include "LittleFS.h"
#define FSCom LittleFS
#define FSBegin() FSCom.begin()
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#endif

#if defined(ARCH_ESP32)

#if CONFIG_IDF_TARGET_ESP32S3
// ESP32S3 version
#include "FFat.h"
#define FSCom FFat
#define FSBegin() FSCom.begin(true)
#define FILE_O_WRITE "w"
#define FILE_O_READ  "r"
#else
// ESP32 version
#include "LittleFS.h"
#define FSCom LittleFS
#define FSBegin() FSCom.begin(true)
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#endif
#endif /*CONFIG_IDF_TARGET_ESP32S3*/


#if defined(ARCH_NRF52)
// NRF52 version
#include "InternalFileSystem.h"
#define FSCom InternalFS
#define FSBegin() FSCom.begin()
using namespace Adafruit_LittleFS_Namespace;
#endif

void fsInit();
bool copyFile(const char* from, const char* to);
bool renameFile(const char* pathFrom, const char* pathTo);
void listDir(const char * dirname, uint8_t levels, boolean del);
void rmDir(const char * dirname);
void setupSDCard();