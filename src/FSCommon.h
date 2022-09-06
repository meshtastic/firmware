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
#include <LittleFS.h>   //esp32s3 uses the framework's built-in LittleFS
#define FSCom LittleFS
#else
// ESP32 version
#include "LITTLEFS.h"
#define FSCom LITTLEFS
#endif


#define FSBegin() FSCom.begin(true)
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#endif

#if defined(ARCH_NRF52)
// NRF52 version
#include "InternalFileSystem.h"
#define FSCom InternalFS
#define FSBegin() FSCom.begin()
using namespace Adafruit_LittleFS_Namespace;
#endif

void fsInit();
bool renameFile(const char* pathFrom, const char* pathTo);
void listDir(const char * dirname, uint8_t levels);
void rmDir(const char * dirname);
