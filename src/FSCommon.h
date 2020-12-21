#pragma once

#include "configuration.h"

// Cross platform filesystem API

#ifdef PORTDUINO
// Portduino version
#include "PortduinoFS.h"
#define FS PortduinoFS
#define FSBegin() true
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#elif !defined(NO_ESP32)
// ESP32 version
#include "SPIFFS.h"
#define FS SPIFFS
#define FSBegin() FS.begin(true)
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"
#else
// NRF52 version
#include "InternalFileSystem.h"
#define FS InternalFS
#define FSBegin() FS.begin()
using namespace Adafruit_LittleFS_Namespace;
#endif

void fsInit();