// InternalFileSystem.h — stub for nRF54L15/Zephyr
// FSCommon.h includes this when ARCH_NRF52 is defined.
// Phase 2: compile-only stub.  Real Zephyr FS backend follows in Phase 3.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Arduino FS mode strings (Adafruit InternalFileSystem convention)
#ifndef FILE_O_READ
#define FILE_O_READ  "r"
#define FILE_O_WRITE "w"
#endif

namespace Adafruit_LittleFS_Namespace {

class InternalFileSystem;  // forward

class File {
  public:
    File() {}
    explicit File(InternalFileSystem &) {}  // nRF52 constructor pattern
    explicit operator bool() const { return false; }
    int      read()                          { return -1; }
    int      read(void *, uint16_t)          { return -1; }
    size_t   write(uint8_t)                  { return 0; }
    size_t   write(const uint8_t *, size_t)  { return 0; }
    void     flush()                         {}
    void     close()                         {}
    size_t   size()                          { return 0; }
    bool     isDirectory()                   { return false; }
    const char *name()                       { return ""; }
    File     openNextFile()                  { return File(); }
    void     rewindDirectory()               {}
    bool     seek(uint32_t)                  { return false; }
    int      available()                     { return 0; }
    int      peek()                          { return -1; }
};

class InternalFileSystem {
  public:
    bool begin()                                       { return false; }
    File open(const char *, const char *)              { return File(); }
    bool exists(const char *)                          { return false; }
    bool remove(const char *)                          { return false; }
    bool rename(const char *, const char *)            { return false; }
    bool mkdir(const char *)                           { return false; }
    bool rmdir(const char *)                           { return false; }
    bool rmdir_r(const char *)                         { return false; }
    uint32_t usedBytes()                               { return 0; }
    uint32_t totalBytes()                              { return 0; }
    bool format()                                      { return false; }
};

} // namespace Adafruit_LittleFS_Namespace

// Global singleton used by FSCommon.h as FSCom
extern Adafruit_LittleFS_Namespace::InternalFileSystem InternalFS;
