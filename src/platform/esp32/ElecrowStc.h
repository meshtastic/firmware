#pragma once

#if defined(HAS_ELECROW_STC)

#include <cstddef>
#include <cstdint>

namespace concurrency
{
class Lock;
}

namespace elecrow_panel
{
constexpr uint8_t STC_ADDRESS = 0x30;
constexpr uint8_t GT911_ADDRESS = 0x5D;
constexpr uint8_t BACKLIGHT_OFF = 245;
constexpr uint8_t BUZZER_ON = 246;
constexpr uint8_t BUZZER_OFF = 247;

concurrency::Lock *sharedI2cLock();
bool writeI2c(uint8_t address, const uint8_t *data, size_t length);
bool readI2cRegister16(uint8_t address, uint16_t reg, uint8_t *data, size_t length);
bool setStcBuzzer(bool on);
} // namespace elecrow_panel

#endif
