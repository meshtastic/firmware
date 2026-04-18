/*

Panel profile: single source of truth for how a specific E-Ink panel is wired and brought up.
Variants subclass a per-panel profile only to override differences (SPI bus, pins, rotation, backlight pin,
power-up quirks). The profile's create() constructs and begins the underlying
NicheGraphics::Drivers::EInk subclass exactly once.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "graphics/eink/Drivers/EInk.h"

#include <SPI.h>

namespace NicheGraphics::Panels
{

class PanelProfile
{
  public:
    virtual ~PanelProfile() = default;

    // Produce and begin() the underlying E-Ink driver. Called once per boot.
    virtual NicheGraphics::Drivers::EInk *create() = 0;

    // Public, variant-overridable metadata
    virtual uint8_t rotation() const { return 0; }
    virtual int8_t backlightPin() const;

  protected:
    // Default SPI bring-up. ESP32 uses HSPI with PIN_EINK_SCLK/MOSI; nRF52 uses SPI1 (pins from variant.h).
    // Variants override when using a non-default bus or pin set.
    virtual SPIClass *beginSpi();

    // Pin defaults read the variant's PIN_EINK_* macros. Variants override if mapping differs.
    virtual uint8_t pinDC() const;
    virtual uint8_t pinCS() const;
    virtual uint8_t pinBusy() const;
    virtual int8_t pinReset() const;

    // Hook for variants that need to raise a power rail / observe settle time before SPI traffic.
    virtual void prePowerOn() {}
};

} // namespace NicheGraphics::Panels

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
