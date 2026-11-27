#pragma once

#include <GxEPD2_EPD.h>

class W20EInkPanel : public GxEPD2_EPD
{
  public:
    static constexpr uint16_t WIDTH = 240;
    static constexpr uint16_t WIDTH_VISIBLE = WIDTH;
    static constexpr uint16_t HEIGHT = 320;

    W20EInkPanel(int16_t cs, int16_t dc, int16_t reset, int16_t busy);
    void clearScreen(uint8_t value) override;
    void writeScreenBuffer(uint8_t value) override;
    void writeImage(const uint8_t *bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirrorY = false,
                    bool pgm = false) override;
    void writeImagePart(const uint8_t *bitmap, int16_t sourceX, int16_t sourceY, int16_t sourceWidth, int16_t sourceHeight,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirrorY = false,
                        bool pgm = false) override;
    void refresh(bool partial = false) override;
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override;
    void powerOff() override;
    void hibernate() override;

  private:
    void initPanel();
    void setWindow(int16_t x, int16_t y, int16_t w, int16_t h);
};
