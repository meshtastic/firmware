#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "power.h"

void powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        DEBUG_MSG("Rebooting\n");
#ifndef NO_ESP32
        ESP.restart();
#elif NRF52_SERIES
        NVIC_SystemReset();
#else
        DEBUG_MSG("FIXME implement reboot for this platform");
#endif
    }

#if NRF52_SERIES
    if (shutdownAtMsec) {
        screen->startShutdownScreen();
        playBeep();
        ledOff(PIN_LED1);
        ledOff(PIN_LED2);
    }
#endif

    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        DEBUG_MSG("Shutting down from admin command\n");
#ifdef TBEAM_V10
        if (axp192_found == true) {
            playShutdownMelody();
            power->shutdown();
        }
#elif NRF52_SERIES
        playShutdownMelody();
        power->shutdown();
#else
        DEBUG_MSG("FIXME implement shutdown for this platform");
#endif
    }
}
