#pragma once

#include "configuration.h"

#if defined(USE_HUB75)

#include <OLEDDisplay.h>

#ifndef HUB75_BRIGHTNESS_DEFAULT
#define HUB75_BRIGHTNESS_DEFAULT 180
#endif

class MatrixPanel_I2S_DMA;

/**
 * A color display backend that drives a HUB75 RGB LED matrix panel from the
 * BaseUI color path.
 */
class HUB75Display : public OLEDDisplay
{
  public:
    HUB75Display(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus);
    ~HUB75Display();

    // Write the buffer to the matrix (full-frame, region-aware color expansion).
    void display() override;

    void setDisplayBrightness(uint8_t brightness);

  protected:
    int getBufferOffset(void) override { return 0; }

    void sendCommand(uint8_t com) override;

    bool connect() override;

  private:
    MatrixPanel_I2S_DMA *matrix = nullptr;
    uint8_t brightness = HUB75_BRIGHTNESS_DEFAULT;

    bool firstFrame = true;
    bool haveThemeDefaults = false;
    uint16_t lastOnBe = 0;
    uint16_t lastOffBe = 0;
    uint32_t lastColorSig = 0;
};

#endif // USE_HUB75
