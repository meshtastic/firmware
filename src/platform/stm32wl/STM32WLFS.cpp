#include <FatFs.h>
#include <STM32WLFS.h>

uint16_t OSFS::startOfEEPROM = 1;
uint16_t OSFS::endOfEEPROM = E2END;

Stm32wlFS STM32WLFS;

void OSFS::readNBytes(uint16_t address, unsigned int num, byte *output)
{
    for (uint16_t i = address; i < address + num; i++) {
        *output = EEPROM.read(i);
        output++;
    }
}

void OSFS::writeNBytes(uint16_t address, unsigned int num, const byte *input)
{
    for (uint16_t i = address; i < address + num; i++) {
        EEPROM.write(i, *input);
        input++;
    }
}

bool Stm32wlFS::begin()
{
    OSFS::result r = OSFS::checkLibVersion();
    if (r == OSFS::result::UNFORMATTED) {
        OSFS::format();
    }
    return true;
}

void Stm32wlFS::mkdir(const char *dirname) {}

bool Stm32wlFS::remove(const char *filename) {}

bool Stm32wlFS::exists(const char *filename)
{
    return false;
}

File Stm32wlFS::open(const char *path, const char *mode, const bool create)
{
    return File.open(path, mode);
}

File Stm32wlFS::open(const String &path, const char *mode, const bool create)
{
    return open(path.c_str(), mode, create);
}