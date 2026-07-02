#ifndef RECLOCK_I2C_
#define RECLOCK_I2C_

#include "../graphics/Screen.h"
#include "ScanI2CTwoWire.h"
#include <Wire.h>
#include <stdint.h>

/* Class to set and restore the I2C clock temporarily on a i2cBus
    See https://github.com/arduino/Arduino/issues/11457
    Currently, only ESP32 can getClock()
    While all cores can setClock()
    https://github.com/sandeepmistry/arduino-nRF5/blob/master/libraries/Wire/Wire.h#L50
    https://github.com/earlephilhower/arduino-pico/blob/master/libraries/Wire/src/Wire.h#L60
    https://github.com/stm32duino/Arduino_Core_STM32/blob/main/libraries/Wire/src/Wire.h#L103
    For cases when I2C speed is different to the ones defined by sensors (see defines in sensor classes)
    we need to reclock I2C and set it back to the previous established speed.
    Only for cases where we can know it (ESP32 or known screen) we can do this.
*/

extern graphics::Screen *screen;

class ReClockI2C
{
  public:
    void setup(TwoWire *i2cBus, ScanI2C::I2CPort port)
    {
        this->i2cBus = i2cBus;
        this->port = port;
        this->previousClock = 0;
    }

    bool setClock(uint32_t desiredClock)
    {
        uint32_t currentClock = this->getClock();

        if (currentClock) {
            LOG_DEBUG("Current I2C frequency: %uHz", currentClock);
        }

        if (currentClock != desiredClock) {
            LOG_DEBUG("Changing I2C clock to %uHz", desiredClock);
            this->i2cBus->setClock(desiredClock);
            // If the clock is 0Hz, we still store it
            // We'll check in restoreClock function
            setPreviousClock(currentClock);
            LOG_DEBUG("Stored previous clock I2C clock: %uHz", this->previousClock);
            return true;
        }

        LOG_DEBUG("I2C clock was already %uHz. Skipping", desiredClock);
        setPreviousClock(0);
        return false;
    }

    bool restoreClock()
    {
        if (this->previousClock) {
            LOG_DEBUG("Restoring I2C clock to %uHz", this->previousClock);
            i2cBus->setClock(this->previousClock);
            setPreviousClock(0);
            return true;
        }
        LOG_DEBUG("I2C clock was unknown. Not restored");
        return false;
    }

  private:
    TwoWire *i2cBus{};
    ScanI2C::I2CPort port{};
    uint32_t previousClock = 0;

    void setPreviousClock(uint32_t clock) { this->previousClock = clock; }

    uint32_t getClock()
    {

#ifdef CAN_GET_I2C_CLOCK
        return this->i2cBus->getClock();
#elif HAS_SCREEN
        if (screen) {
            // If we get a non-zero response here, the screen has set a speed
            uint32_t screenClock = 0;
            ScanI2C::I2CPort screenPort = ScanI2C::I2CPort::NO_I2C;
            screenClock = screen->getI2cFrequency();
            screenPort = screen->getI2CPort();
            // Check if i2c port is the same, and that we got a screenClock back (0 means the screen didn't set it)
            if (screenClock && (screenPort == this->port)) {
                LOG_DEBUG("Screen defined I2C frequency: %uHz", screenClock);
                return screenClock;
            }
        }
#endif
        return 0;
    }
};

#endif
