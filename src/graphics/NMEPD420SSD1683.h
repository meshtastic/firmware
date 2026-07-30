#pragma once

#include <epd/GxEPD2_420_GYE042A87.h>

class NMEPD420SSD1683 : public GxEPD2_420_GYE042A87
{
  public:
    static constexpr uint16_t WIDTH = GxEPD2_420_GYE042A87::WIDTH;
    static constexpr uint16_t WIDTH_VISIBLE = WIDTH;
    static constexpr uint16_t HEIGHT = GxEPD2_420_GYE042A87::HEIGHT;
    static constexpr GxEPD2::Panel panel = GxEPD2::GYE042A87;
    static constexpr bool hasColor = true;
    static constexpr bool hasPartialUpdate = true;
    static constexpr bool hasFastPartialUpdate = true;
    static constexpr uint16_t full_refresh_time = 5000;
    static constexpr uint16_t partial_refresh_time = 1500;

    NMEPD420SSD1683(int16_t cs, int16_t dc, int16_t rst, int16_t busy, SPIClass &spi);

    void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                  bool invert = false, bool mirror_y = false, bool pgm = false) override;
    void refresh(bool partial_update_mode = false) override;
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override;

  private:
    void updateFull();
    void updateFast();
};
