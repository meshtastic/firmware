#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "power.h"

void powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        LOG_INFO("Rebooting\n");
#if defined(ARCH_ESP32)
        ESP.restart();
#elif defined(ARCH_NRF52)
        NVIC_SystemReset();
#else
        rebootAtMsec = -1;
        LOG_WARN("FIXME implement reboot for this platform. Skipping for now.\n");
#endif
    }

#if defined(ARCH_ESP32) || defined(ARCH_NRF52)
    if (shutdownAtMsec) {
        screen->startShutdownScreen();
    }
#endif

    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        LOG_INFO("Shutting down from admin command\n");
        playShutdownMelody();
#if defined(ARCH_NRF52) || defined(ARCH_ESP32)
        power->shutdown();
#else
        LOG_WARN("FIXME implement shutdown for this platform");
#endif
    }
}
