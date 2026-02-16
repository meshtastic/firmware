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
#if defined(USE_EXTERNAL_FLASH)
// nRF52 version with external flash (LittleFS backend)
#include "Filesystem/ExternalLittleFS.h"
#include <Adafruit_SPIFlash.h>
#include <SPI.h>
#define DISK_LABEL "EXT FLASH"
#if defined(EXTERNAL_FLASH_USE_QSPI)
extern Adafruit_FlashTransport_QSPI flashTransport;
#endif
extern Adafruit_SPIFlash flash;
extern ExternalLittleFS externalFS;
#define FSCom externalFS
#define FSBegin() FSCom.begin(&flash)
using ExternalFSFile = Adafruit_LittleFS_Namespace::File;

#ifndef FILE_O_WRITE
#define FILE_O_WRITE Adafruit_LittleFS_Namespace::FILE_O_WRITE
#endif
#ifndef FILE_O_READ
#define FILE_O_READ Adafruit_LittleFS_Namespace::FILE_O_READ
#endif
#ifndef FILE_WRITE
#define FILE_WRITE FILE_O_WRITE
#endif
#ifndef FILE_READ
#define FILE_READ FILE_O_READ
#endif

extern bool flashInitialized;
extern bool externalFSMounted;
#else
// nRF52 version without external flash
#include "InternalFileSystem.h"
#define FSCom InternalFS
#define FSBegin() FSCom.begin() // InternalFS formats on failure
using namespace Adafruit_LittleFS_Namespace;
#endif
#endif

void fsInit();
void fsListFiles();
bool copyFile(const char *from, const char *to);
bool renameFile(const char *pathFrom, const char *pathTo);
std::vector<meshtastic_FileInfo> getFiles(const char *dirname, uint8_t levels);
void listDir(const char *dirname, uint8_t levels, bool del = false);
void rmDir(const char *dirname);
void setupSDCard();
#ifdef USE_EXTERNAL_FLASH
bool checkExternalFS();
bool formatExternalFS();
#endif