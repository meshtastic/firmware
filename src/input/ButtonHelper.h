#pragma once

#include "configuration.h"
#include "mesh/NodeDB.h"

#ifdef ARCH_ESP32
#include <driver/gpio.h>
#endif

inline uint32_t getResolvedButtonPin()
{
    uint32_t btnPin = 0xFF;
#if defined(USERPREFS_BUTTON_PIN)
    btnPin = USERPREFS_BUTTON_PIN;
#elif defined(BUTTON_PIN)
    btnPin = BUTTON_PIN;
#endif
    if (config.device.button_gpio != 0) {
        btnPin = config.device.button_gpio;
    }

    if (btnPin != 0xFF) {
#ifdef ARCH_ESP32
        if (!GPIO_IS_VALID_GPIO(btnPin)) {
            btnPin = 0xFF;
        }
#endif
    }
    return btnPin;
}
