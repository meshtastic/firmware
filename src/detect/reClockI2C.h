#ifdef CAN_RECLOCK_I2C
#include "ScanI2CTwoWire.h"

uint32_t reClockI2C(uint32_t desiredClock, TwoWire *i2cBus)
{

    uint32_t currentClock;

    /* See https://github.com/arduino/Arduino/issues/11457
      Currently, only ESP32 can getClock()
      While all cores can setClock()
      https://github.com/sandeepmistry/arduino-nRF5/blob/master/libraries/Wire/Wire.h#L50
      https://github.com/earlephilhower/arduino-pico/blob/master/libraries/Wire/src/Wire.h#L60
      https://github.com/stm32duino/Arduino_Core_STM32/blob/main/libraries/Wire/src/Wire.h#L103
      For cases when I2C speed is different to the ones defined by sensors (see defines in sensor classes)
      we need to reclock I2C and set it back to the previous desired speed.
      Only for cases where we can know OR predefine the speed, we can do this.
    */

#ifdef ARCH_ESP32
    currentClock = i2cBus->getClock();
#elif defined(ARCH_NRF52)
    // TODO add getClock function or return a predefined clock speed per variant?
    return 0;
#elif defined(ARCH_RP2040)
    // TODO add getClock function or return a predefined clock speed per variant
    return 0;
#elif defined(ARCH_STM32WL)
    // TODO add getClock function or return a predefined clock speed per variant
    return 0;
#else
    return 0;
#endif

    if (currentClock != desiredClock) {
        LOG_DEBUG("Changing I2C clock to %u", desiredClock);
        i2cBus->setClock(desiredClock);
    }
    return currentClock;
}
#endif
