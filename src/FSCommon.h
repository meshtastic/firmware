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
// NRF52 version
#include "InternalFileSystem.h"
#define FSCom InternalFS
#define FSBegin() FSCom.begin() // InternalFS formats on failure
#include "Adafruit_LittleFS.h"
using namespace Adafruit_LittleFS_Namespace;

/**
 * Optional external LittleFS instance (e.g. QSPI flash on boards that define
 * EXTERNAL_FLASH_USE_QSPI in their variant.h).
 *
 * Null by default.  Set by extFSInit() when a board or firmware module
 * initialises an external flash filesystem.  XModem uses this pointer to
 * route "/ext/" paths to external storage instead of InternalFS.
 */
extern Adafruit_LittleFS *extFS;

/**
 * Called from fsInit() to initialise the external filesystem.
 * The default weak implementation is a no-op; override in a platform or
 * firmware module to mount the QSPI (or SPI) flash and assign extFS.
 */
void extFSInit();
#endif

void fsInit();
void fsListFiles();
bool copyFile(const char *from, const char *to);
bool renameFile(const char *pathFrom, const char *pathTo);
std::vector<meshtastic_FileInfo> getFiles(const char *dirname, uint8_t levels);
void listDir(const char *dirname, uint8_t levels, bool del = false);
void rmDir(const char *dirname);
void setupSDCard();

#ifdef FSCom
/**
 * Virtual filesystem mount-point routing.
 *
 * Path-prefix convention (double-underscore delimiters avoid collisions):
 *   /__int__/foo  →  internal flash  (InternalFS / LittleFS)
 *   /__ext__/foo  →  external flash  (QSPI LittleFS if mounted, else internal)
 *   /__sd__/foo   →  SD card         (if mounted, else internal)
 *   /foo          →  internal flash  (bare paths passed through unchanged)
 *
 * This provides a lightweight mount-point convention without a full VFS.
 * All consumers call fsRoute() + the helpers below; when Meshtastic gains a
 * proper VFS layer these functions become the adapter to it.
 */
enum class FsMount { Internal, External, SD };

struct FSRoute {
    FsMount mount = FsMount::Internal;
    char    path[128] = {};   // real path after prefix stripped
};

/** Resolve a path string to an FSRoute (mount + stripped path). */
FSRoute fsRoute(const char *path);

/** Open a file for reading on the routed filesystem. */
File fsOpenRead(const FSRoute &r);

/** Open a file for writing on the routed filesystem. */
File fsOpenWrite(const FSRoute &r);

/** Remove a file on the routed filesystem. Returns true on success. */
bool fsRemove(const FSRoute &r);

/** Create a directory (and parents) on the routed filesystem. */
bool fsMkdir(const FSRoute &r);

/** Return true if the path exists on the routed filesystem. */
bool fsExists(const FSRoute &r);
#endif // FSCom