#pragma once

#include <GxEPD2_EPD.h>

class NMEPD420UC8179 : public GxEPD2_EPD
{
  public:
    static constexpr uint16_t WIDTH = 400;
    static constexpr uint16_t WIDTH_VISIBLE = WIDTH;
    static constexpr uint16_t HEIGHT = 300;
    static constexpr GxEPD2::Panel panel = GxEPD2::GYE042A87;
    static constexpr bool hasColor = true;
    static constexpr bool hasPartialUpdate = false;
    static constexpr bool hasFastPartialUpdate = false;
    static constexpr uint16_t power_on_time = 150;
    static constexpr uint16_t power_off_time = 50;
    static constexpr uint16_t full_refresh_time = 18000;
    static constexpr uint16_t partial_refresh_time = full_refresh_time;

    NMEPD420UC8179(int16_t cs, int16_t dc, int16_t rst, int16_t busy, SPIClass &spi);

    void clearScreen(uint8_t value = 0xFF) override;
    void writeScreenBuffer(uint8_t value = 0xFF) override;
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
                    bool mirror_y = false, bool pgm = false) override;
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
                        bool pgm = false) override;
    void refresh(bool partial_update_mode = false) override;
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override;
    void powerOff() override;
    void hibernate() override;

  private:
    static uint8_t remapPlaneData(uint8_t command, uint8_t data);
    void writeScreenBuffer(uint8_t command, uint8_t value);
    void writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                    bool mirror_y, bool pgm);
    void writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
                        int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
                        bool pgm);
    void setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void powerOn();
    void initDisplay();
    void updateFull();
};
