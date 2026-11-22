#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_XL9535)

#include "UptimeClock.h"
#include "XL9535LockingArduinoHal.h"
#include "mesh/Throttle.h"

XL9535LockingArduinoHal::XL9535LockingArduinoHal(SPIClass &spi, SPISettings spiSettings, ExtensionIOXL9535 &expander)
    : LockingArduinoHal(spi, spiSettings), io(expander)
{
    LOG_INFO("XL9535: LoRa RESET/DIO1 are expander pins, DIO1 IRQ simulated by polling the radio IRQ status register");
}

bool XL9535LockingArduinoHal::isXl9535Pin(uint32_t pin)
{
    return pin >= XL9535_VPIN_BASE && pin <= XL9535_VPIN_BASE + 15;
}

void XL9535LockingArduinoHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (isXl9535Pin(pin)) {
        uint8_t local = (uint8_t)(pin - XL9535_VPIN_BASE);
        if (!io.pinMode(local, mode == GpioModeOutput ? OUTPUT : INPUT))
            LOG_WARN("XL9535: pinMode failed on expander pin %u", local);
        return;
    }
    ArduinoHal::pinMode(pin, mode);
}

void XL9535LockingArduinoHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (isXl9535Pin(pin)) {
        uint8_t local = (uint8_t)(pin - XL9535_VPIN_BASE);
        if (!io.digitalWrite(local, value == GpioLevelHigh ? HIGH : LOW))
            LOG_WARN("XL9535: write failed on expander pin %u", local);
        return;
    }
    ArduinoHal::digitalWrite(pin, value);
}

uint32_t XL9535LockingArduinoHal::digitalRead(uint32_t pin)
{
    if (!isXl9535Pin(pin))
        return ArduinoHal::digitalRead(pin);

    int level = io.digitalRead((uint8_t)(pin - XL9535_VPIN_BASE));
    if (level >= 0) {
        readFailing = false;
        return level ? GpioLevelHigh : GpioLevelLow;
    }

    // I2C error. A streak continues only while the failed reads follow each other (one blocking wait).
    const bool newStreak = !readFailing || Throttle::hasElapsed(readFailLastMs, XL9535_READ_ERR_ESCAPE_MS);
    const uint32_t now = Time::getMillis();
    if (newStreak) {
        readFailing = true;
        readFailSinceMs = now;
    }
    readFailLastMs = now;

    // RadioLib's CAD wait on DIO1 has no timeout: after a full window of errors let it through once,
    // the CAD verdict is then taken from the IRQ flags over SPI.
    if (Throttle::hasElapsed(readFailSinceMs, XL9535_READ_ERR_ESCAPE_MS)) {
        readFailing = false;
        LOG_WARN("XL9535: no valid read of expander pin %u for %u ms, releasing the wait",
                 (unsigned)(pin - XL9535_VPIN_BASE), (unsigned)XL9535_READ_ERR_ESCAPE_MS);
        return GpioLevelHigh;
    }

    ::delay(1);
    return GpioLevelLow; // "not yet": the wait retries
}

// No interrupt exists for a virtual pin, and detachInterrupt() can be reached from ISR context: never I2C here.
void XL9535LockingArduinoHal::attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode)
{
    if (interruptNum == RADIOLIB_NC)
        return;
    ArduinoHal::attachInterrupt(interruptNum, cb, mode);
}

void XL9535LockingArduinoHal::detachInterrupt(uint32_t interruptNum)
{
    if (interruptNum == RADIOLIB_NC)
        return;
    ArduinoHal::detachInterrupt(interruptNum);
}

uint32_t XL9535LockingArduinoHal::pinToInterrupt(uint32_t pin)
{
    if (isXl9535Pin(pin))
        return RADIOLIB_NC;
    return ArduinoHal::pinToInterrupt(pin);
}

#endif // ARCH_ESP32 && USE_XL9535
