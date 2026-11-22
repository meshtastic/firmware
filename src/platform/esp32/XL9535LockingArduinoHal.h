#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_XL9535)

#include "mesh/RadioLibInterface.h"
#include "platform/esp32/ExtensionIOXL9535.h"
#include <SPI.h>

#ifndef XL9535_VPIN_BASE
#define XL9535_VPIN_BASE 100
#endif

#if !defined(LORA_DIO1_SOFTWARE_POLL) || !defined(LORA_DIO1_EXTENDED_IO)
#error "USE_XL9535 requires LORA_DIO1_SOFTWARE_POLL and LORA_DIO1_EXTENDED_IO in variant.h"
#endif

// How long a blocking wait on a virtual pin may see nothing but I2C errors before it is let through.
// TODO(hw-verify): 400 ms is above a 4-symbol CAD at SF12/BW125 (about 147 ms); confirm on hardware.
#define XL9535_READ_ERR_ESCAPE_MS 400

/**
 * Routes RadioLib virtual GPIO XL9535_VPIN_BASE..+15 to an XL9535 I2C expander (P00..P07, P10..P17);
 * all other pins fall through to the regular Arduino GPIO HAL.
 *
 * A virtual pin has no interrupt: the radio thread polls the radio's IRQ status register instead
 * (LORA_DIO1_SOFTWARE_POLL). The DIO1 level is only read over I2C by RadioLib's blocking CAD wait.
 */
class XL9535LockingArduinoHal : public LockingArduinoHal
{
  public:
    XL9535LockingArduinoHal(SPIClass &spi, SPISettings spiSettings, ExtensionIOXL9535 &expander);

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    uint32_t pinToInterrupt(uint32_t pin) override;

  private:
    ExtensionIOXL9535 &io;
    bool readFailing = false;     // the last virtual-pin read failed and the streak has not been let through yet
    uint32_t readFailSinceMs = 0; // start of that streak of failed reads
    uint32_t readFailLastMs = 0;  // most recent failed read
    static bool isXl9535Pin(uint32_t pin);
};

#endif // ARCH_ESP32 && USE_XL9535
