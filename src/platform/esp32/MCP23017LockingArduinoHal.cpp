#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_MCP23017)

#include "MCP23017LockingArduinoHal.h"
#include "SPILock.h"

MCP23017LockingArduinoHal::MCP23017LockingArduinoHal(SPIClass &spi, SPISettings spiSettings, ExtensionIOMCP23017 &expander)
    : LockingArduinoHal(spi, spiSettings), mcp(expander)
{
#if MCP23017_INT_ESP32_PIN < 0
#if defined(LORA_DIO1_SOFTWARE_POLL)
    LOG_INFO("MCP23017 /INT not wired: LoRa DIO1 IRQ simulated by polling the radio IRQ status register");
#else
    LOG_WARN("MCP23017_INT_ESP32_PIN unset and no LORA_DIO1_SOFTWARE_POLL: LoRa DIO1 interrupts will not work");
#endif
#endif
}

bool MCP23017LockingArduinoHal::isMcpPin(uint32_t pin)
{
    return pin >= MCP23017_VPIN_BASE && pin <= MCP23017_VPIN_BASE + 15;
}

void MCP23017LockingArduinoHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (isMcpPin(pin)) {
        uint8_t local = (uint8_t)(pin - MCP23017_VPIN_BASE);
        mcp.pinMode(local, mode == GpioModeOutput ? OUTPUT : INPUT);
        return;
    }
    ArduinoHal::pinMode(pin, mode);
}

void MCP23017LockingArduinoHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (isMcpPin(pin)) {
        uint8_t local = (uint8_t)(pin - MCP23017_VPIN_BASE);
        mcp.digitalWrite(local, value == GpioLevelHigh ? HIGH : LOW);
        return;
    }
    ArduinoHal::digitalWrite(pin, value);
}

uint32_t MCP23017LockingArduinoHal::digitalRead(uint32_t pin)
{
    if (isMcpPin(pin)) {
        uint8_t local = (uint8_t)(pin - MCP23017_VPIN_BASE);
        return (uint32_t)mcp.digitalRead(local);
    }
    return ArduinoHal::digitalRead(pin);
}

void MCP23017LockingArduinoHal::attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode)
{
#if MCP23017_INT_ESP32_PIN >= 0
    uint8_t idx = (uint8_t)(SX126X_DIO1 - MCP23017_VPIN_BASE);
    if (idx <= 15)
        mcp.enablePinChangeInterrupt(idx, true);
    // MCP23017 /INT is open-drain active-low: trigger on the falling edge of the INT line
    // (RadioLib passes the DIO rising-edge mode, which applies to the DIO pin, not /INT).
    ArduinoHal::attachInterrupt(interruptNum, cb, GpioInterruptFalling);
#else
    ArduinoHal::attachInterrupt(interruptNum, cb, mode);
#endif
}

void MCP23017LockingArduinoHal::detachInterrupt(uint32_t interruptNum)
{
    ArduinoHal::detachInterrupt(interruptNum);
#if MCP23017_INT_ESP32_PIN >= 0
    uint8_t idx = (uint8_t)(SX126X_DIO1 - MCP23017_VPIN_BASE);
    if (idx <= 15)
        mcp.enablePinChangeInterrupt(idx, false);
#endif
}

uint32_t MCP23017LockingArduinoHal::pinToInterrupt(uint32_t pin)
{
    if (isMcpPin(pin)) {
#if MCP23017_INT_ESP32_PIN >= 0
        return ::digitalPinToInterrupt((unsigned int)MCP23017_INT_ESP32_PIN);
#else
        return RADIOLIB_NC;
#endif
    }
    return ArduinoHal::pinToInterrupt(pin);
}

#if MCP23017_INT_ESP32_PIN >= 0
void MCP23017LockingArduinoHal::spiBeginTransaction()
{
    spiLock->lock();
    // Clear any latched expander interrupt before talking to the radio, so a stale /INT
    // level doesn't mask the next DIO1 edge.
    mcp.clearInterruptLatches();
    ArduinoHal::spiBeginTransaction();
}

void MCP23017LockingArduinoHal::spiEndTransaction()
{
    ArduinoHal::spiEndTransaction();
    spiLock->unlock();
}
#endif

#endif // ARCH_ESP32 && USE_MCP23017
