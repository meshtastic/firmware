/*

NicheGraphics setup for LILYGO T-Deck Pro V1.1 (GDEQ031T10 3.1" 240x320 on the
default SPI bus).

Same e-ink controller as the original T-Deck Pro, but V1.1 breaks out the panel
reset pin (PIN_EINK_RES). The base SSD16XX driver deep-sleeps the panel after a
full refresh whenever a reset pin is present; the original GxEPD2 build set
EINK_NOT_HIBERNATE to avoid panel wake/refresh quirks on this revision, so we
preserve that by overriding deepSleep() to a no-op.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Drivers/GDEQ031T10.h"
#include "graphics/eink/Panels/PanelProfile.h"

// GDEQ031T10, but skip the controller deep-sleep (preserves the V1.1
// EINK_NOT_HIBERNATE behaviour). Deep-sleep only saves a few µA, so suppressing
// it is a safe trade for panel compatibility.
class TDeckProV11Driver : public NicheGraphics::Drivers::GDEQ031T10 {
protected:
  void deepSleep() override {}
};

class TDeckProV11Panel : public NicheGraphics::Panels::PanelProfile {
public:
  NicheGraphics::Drivers::EInk *create() override {
    prePowerOn();
    SPIClass *spi = beginSpi();
    auto *drv = new TDeckProV11Driver();
    drv->begin(spi, pinDC(), pinCS(), pinBusy(), pinReset());
    return drv;
  }

protected:
  SPIClass *beginSpi() override {
    // Old GxEPD2 path used the global SPI bus on this board.
    SPI.begin();
    return &SPI;
  }
};

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI() {
  auto *panel = new TDeckProV11Panel();
  NicheGraphics::Drivers::EInk *driver = panel->create();
  auto *display =
      new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
  // T-Deck Pro types frequently; allow more FAST refreshes between FULLs than
  // the default.
  display->setDisplayResilience(15, 1.5f);
  return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
