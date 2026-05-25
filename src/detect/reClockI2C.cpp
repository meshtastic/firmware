#include "reClockI2C.h"
#include "ScanI2CTwoWire.h"

/* See https://github.com/arduino/Arduino/issues/11457
    Currently, only ESP32 can getClock()
    While all cores can setClock()
    https://github.com/sandeepmistry/arduino-nRF5/blob/master/libraries/Wire/Wire.h#L50
    https://github.com/earlephilhower/arduino-pico/blob/master/libraries/Wire/src/Wire.h#L60
    https://github.com/stm32duino/Arduino_Core_STM32/blob/main/libraries/Wire/src/Wire.h#L103
    For cases when I2C speed is different to the ones defined by sensors (see defines in sensor classes)
    we need to reclock I2C and set it back to the previous stablished speed.
    Only for cases where we can know it (ESP32 or known screen) we can do this.
*/

uint32_t reClockI2C(uint32_t desiredClock, TwoWire *i2cBus, ScanI2C::I2CPort port)
{

    uint32_t currentClock = 0;
    uint32_t screenClock = 0;
    ScanI2C::I2CPort screenPort = ScanI2C::I2CPort::NO_I2C;

    // Assume that if we can't getClock, or there is no screen, we can set the clock speed at will
#ifdef CAN_RECLOCK_I2C
    currentClock = i2cBus->getClock();
    LOG_DEBUG("Current I2C frequency: %uHz", currentClock);
#elif HAS_SCREEN
    if (screen) {
        // If we get a non-zero response here, the screen has set a speed
        screenClock = screen->getI2cFrequency();
        screenPort = screen->getI2CPort();
        // Check if i2c port is the same, and that we got a screenClock back (0 means the screen didn't set it)
        if (screenClock && (screenPort == port))
            currentClock = screenClock;
        LOG_DEBUG("Screen defined I2C frequency: %uHz", screenClock);
    }
#endif

    if (currentClock != desiredClock) {
        LOG_DEBUG("Changing I2C clock to %uHz", desiredClock);
        i2cBus->setClock(desiredClock);
    }

    return currentClock;
}
