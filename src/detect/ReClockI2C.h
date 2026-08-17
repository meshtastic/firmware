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
    For cases when I2C speed is different to the ones defined by sensors
    (see defines in sensor classes)
    we need to reclock I2C and set it back to the previous established speed.
    Only for cases where we can know it (ESP32 or known screen) we can do this.
*/

extern std::unique_ptr<graphics::Screen> screen;

class ReClockI2C
{
  public:
    void setup(TwoWire *i2cBus, ScanI2C::I2CPort port)
    {
        this->i2cBus = i2cBus;
        this->port = port;
    }

    // Sets the I2C clock to desiredClock and returns whatever clock was active
    // beforehand, so the caller can hand it back to restoreClock() later. The
    // previous clock is returned rather than stored on this object, so callers
    // that nest calls (see ReClockI2CGuard) each keep their own restoration
    // value instead of clobbering a single shared one.
    // Returns 0 if the clock was already at desiredClock, or if the previous
    // clock couldn't be determined - in both cases there's nothing to restore.
    uint32_t setClock(uint32_t desiredClock)
    {
        uint32_t currentClock = this->getClock();

        if (currentClock) {
            LOG_TRACE("Current I2C frequency: %uHz", currentClock);
        }

        if (currentClock != desiredClock) {
            LOG_TRACE("Changing I2C clock to %uHz", desiredClock);
            this->i2cBus->setClock(desiredClock);
            LOG_TRACE("Previous I2C clock: %uHz", currentClock);
            return currentClock;
        }

        LOG_TRACE("I2C clock was already %uHz. Skipping", desiredClock);
        return 0;
    }

    void restoreClock(uint32_t previousClock)
    {
        if (previousClock) {
            LOG_TRACE("Restoring I2C clock to %uHz", previousClock);
            i2cBus->setClock(previousClock);
            return;
        }
        LOG_TRACE("I2C clock was unknown. Not restored");
    }

  private:
    TwoWire *i2cBus{};
    ScanI2C::I2CPort port{};

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

/* Helper for ReClockI2C: sets the clock on construction and restores it on
   destruction, so a caller with multiple early-return paths doesn't need to
   remember to call restoreClock() on each one.
 */
class ReClockI2CGuard
{
  public:
    ReClockI2CGuard(ReClockI2C &reClock, uint32_t desiredClock) : reClock(reClock), previousClock(reClock.setClock(desiredClock))
    {
    }
    ~ReClockI2CGuard() { reClock.restoreClock(previousClock); }
    ReClockI2CGuard(const ReClockI2CGuard &) = delete;
    ReClockI2CGuard &operator=(const ReClockI2CGuard &) = delete;

  private:
    ReClockI2C &reClock;
    uint32_t previousClock;
};

#endif
