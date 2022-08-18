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
#ifdef PIN_LED1
        ledOff(PIN_LED1);
#endif
#ifdef PIN_LED2        
        ledOff(PIN_LED2);
#endif
#ifdef PIN_LED3        
        ledOff(PIN_LED3);
#endif
    }
#endif

    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        DEBUG_MSG("Shutting down from admin command\n");
#ifdef HAS_AXP192
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
