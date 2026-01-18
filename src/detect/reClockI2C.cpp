#include "reClockI2C.h"
#include "ScanI2CTwoWire.h"

uint32_t reClockI2C(uint32_t desiredClock, TwoWire *i2cBus, bool force) {

    uint32_t currentClock = 0;

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

// TODO add getClock function or return a predefined clock speed per variant?
#ifdef CAN_RECLOCK_I2C
    currentClock = i2cBus->getClock();
#endif

    if (currentClock != desiredClock || force){
        LOG_DEBUG("Changing I2C clock to %u", desiredClock);
        i2cBus->setClock(desiredClock);
    }

    return currentClock;
}

