#pragma once
#ifndef Stm32wlFS_H
#define Stm32wlFS_H

#include "File.h"
#include <EEPROM.h>
#include <OSFS.h>

extern uint16_t OSFS::startOfEEPROM;
extern uint16_t OSFS::endOfEEPROM;

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

class Stm32wlFS
{
  public:
    bool begin();
    void mkdir(const char *dirname);
    bool remove(const char *filename);
    bool exists(const char *filename);
    File open(char const *path, char const *mode = FILE_READ, const bool create = false);
    File open(const String &path, const char *mode = FILE_READ, const bool create = false);
};

extern Stm32wlFS STM32WLFS;

#endif // Stm32wlFS_H