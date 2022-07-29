#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "power.h"

void powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        DEBUG_MSG("Rebooting\n");
#if defined(ARCH_ESP32)
        ESP.restart();
#elif defined(ARCH_NRF52)
        NVIC_SystemReset();
#else
        DEBUG_MSG("FIXME implement reboot for this platform");
#endif
    }

#if defined(ARCH_NRF52)
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
#elif defined(ARCH_NRF52)
        playShutdownMelody();
        power->shutdown();
#else
        DEBUG_MSG("FIXME implement shutdown for this platform");
#endif
    }
}
