#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_MCP23017)

#include "mesh/RadioLibInterface.h"
#include "platform/esp32/ExtensionIOMCP23017.h"
#include <SPI.h>

#ifndef MCP23017_VPIN_BASE
#define MCP23017_VPIN_BASE 100
#endif

#ifndef MCP23017_INT_ESP32_PIN
#define MCP23017_INT_ESP32_PIN (-1)
#endif

/**
 * Routes RadioLib virtual GPIO MCP23017_VPIN_BASE..+15 to an MCP23017 I2C expander (GPA0-GPB7);
 * all other pins fall through to the regular Arduino GPIO HAL.
 *
 * DIO1 interrupts: if the expander /INT line is wired to an ESP32 GPIO, set MCP23017_INT_ESP32_PIN
 * and a real edge interrupt is used. If not (MCP23017_INT_ESP32_PIN < 0), define
 * LORA_DIO1_SOFTWARE_POLL so the radio thread polls the radio's IRQ status register instead.
 */
class MCP23017LockingArduinoHal : public LockingArduinoHal
{
  public:
    MCP23017LockingArduinoHal(SPIClass &spi, SPISettings spiSettings, ExtensionIOMCP23017 &expander);

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    uint32_t pinToInterrupt(uint32_t pin) override;
#if MCP23017_INT_ESP32_PIN >= 0
    void spiBeginTransaction() override;
    void spiEndTransaction() override;
#endif

  private:
    ExtensionIOMCP23017 &mcp;
    static bool isMcpPin(uint32_t pin);
};

#endif // ARCH_ESP32 && USE_MCP23017
