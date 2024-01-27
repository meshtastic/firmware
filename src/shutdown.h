#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "power.h"
#if defined(ARCH_PORTDUINO)
#include "api/WiFiServerAPI.h"
#include "input/LinuxInputImpl.h"

#endif

void powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        LOG_INFO("Rebooting\n");
#if defined(ARCH_ESP32)
        ESP.restart();
#elif defined(ARCH_NRF52)
        NVIC_SystemReset();
#elif defined(ARCH_RP2040)
        rp2040.reboot();
#elif defined(ARCH_PORTDUINO)
        deInitApiServer();
        if (aLinuxInputImpl)
            aLinuxInputImpl->deInit();
        SPI.end();
        Wire.end();
        Serial1.end();
        reboot();
#else
        rebootAtMsec = -1;
        LOG_WARN("FIXME implement reboot for this platform. Note that some settings require a restart to be applied.\n");
#endif
    }

#if defined(ARCH_ESP32) || defined(ARCH_NRF52)
    if (shutdownAtMsec) {
        screen->startShutdownScreen();
    }
#endif

    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        LOG_INFO("Shutting down from admin command\n");
#if defined(ARCH_NRF52) || defined(ARCH_ESP32)
        playShutdownMelody();
        power->shutdown();
#elif defined(ARCH_PORTDUINO)
        exit(EXIT_SUCCESS);
#else
        LOG_WARN("FIXME implement shutdown for this platform");
#endif
    }
}