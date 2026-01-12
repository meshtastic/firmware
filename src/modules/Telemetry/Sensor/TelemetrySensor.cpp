#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "TelemetrySensor.h"
#include "main.h"

#ifdef CAN_RECLOCK_I2C

uint32_t TelemetrySensor::setClock(uint32_t desiredClock) {

    uint32_t currentClock;

    // See https://github.com/arduino/Arduino/issues/11457
    // Currently, only ESP32 can getClock
    // While all cores can setClock()
    // https://github.com/sandeepmistry/arduino-nRF5/blob/master/libraries/Wire/Wire.h#L50
    // https://github.com/earlephilhower/arduino-pico/blob/master/libraries/Wire/src/Wire.h#L60
    // https://github.com/stm32duino/Arduino_Core_STM32/blob/main/libraries/Wire/src/Wire.h#L103

#ifdef ARCH_ESP32
    currentClock = _bus->getClock();
#elif defined(ARCH_NRF52)
    // TODO add getClock function or return a predefined clock speed per variant
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

    if (currentClock != desiredClock){
        LOG_DEBUG("Changing I2C clock to %u", desiredClock);
        _bus->setClock(desiredClock);
    }
    return currentClock;
}
#endif

#endif