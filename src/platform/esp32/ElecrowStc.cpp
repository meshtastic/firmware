#include "platform/esp32/ElecrowStc.h"

#if defined(HAS_ELECROW_STC)

#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include <Wire.h>

namespace elecrow_panel
{
static concurrency::Lock i2cLock;

concurrency::Lock *sharedI2cLock()
{
    return &i2cLock;
}

bool writeI2c(uint8_t address, const uint8_t *data, size_t length)
{
    concurrency::LockGuard guard(sharedI2cLock());
    Wire.beginTransmission(address);
    Wire.write(data, length);
    return Wire.endTransmission() == 0;
}

bool readI2cRegister16(uint8_t address, uint16_t reg, uint8_t *data, size_t length)
{
    concurrency::LockGuard guard(sharedI2cLock());
    Wire.beginTransmission(address);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, length) != length)
        return false;
    for (size_t i = 0; i < length; ++i)
        data[i] = Wire.read();
    return true;
}

bool setStcBuzzer(bool on)
{
    const uint8_t command = on ? BUZZER_ON : BUZZER_OFF;
    return writeI2c(STC_ADDRESS, &command, 1);
}
} // namespace elecrow_panel

#endif
