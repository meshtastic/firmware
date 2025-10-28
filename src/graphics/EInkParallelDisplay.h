#pragma once

#include "configuration.h"

#ifdef USE_EPD
#include <OLEDDisplay.h>

class FASTEPD;

/**
 * Adapter for E-Ink 8-bit parallel displays (EPD), specifically devices supported by FastEPD library
 */
class EInkParallelDisplay : public OLEDDisplay
{
  public:
    enum EpdRotation {
        EPD_ROT_LANDSCAPE = 0,
        EPD_ROT_PORTRAIT = 1,
        EPD_ROT_INVERTED_LANDSCAPE = 2,
        EPD_ROT_INVERTED_PORTRAIT = 3,
    };

    EInkParallelDisplay(uint16_t width, uint16_t height, EpdRotation rotation);
    virtual ~EInkParallelDisplay();

    // OLEDDisplay virtuals
    bool connect() override;
    void sendCommand(uint8_t com) override;
    int getBufferOffset(void) override { return 0; }

    void display(void) override;
    bool forceDisplay(uint32_t msecLimit = 1000);
    void endUpdate();

  protected:
    uint32_t lastDrawMsec = 0;
    FASTEPD *epaper;
};

#endif
