#include "configuration.h"

#if defined(USE_EINK) && defined(NM_EPD_420) && !defined(USE_EINK_PARALLELDISPLAY)

#include "NMEPD420SSD1683.h"

NMEPD420SSD1683::NMEPD420SSD1683(int16_t cs, int16_t dc, int16_t rst, int16_t busy, SPIClass &spi)
    : GxEPD2_420_GYE042A87(cs, dc, rst, busy, spi)
{
}

void NMEPD420SSD1683::writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                               bool invert, bool mirror_y, bool pgm)
{
    writeScreenBufferAgain(0x00);
    writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void NMEPD420SSD1683::refresh(bool partial_update_mode)
{
    if (partial_update_mode)
        updateFast();
    else {
        updateFull();
        _initial_refresh = false;
    }
}

void NMEPD420SSD1683::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if (_initial_refresh)
        refresh(false);
    else
        updateFast();
}

void NMEPD420SSD1683::updateFull()
{
    _writeCommand(0x22);
    _writeData(0xF7);
    _writeCommand(0x20);
    _waitWhileBusy("_Update_Full", full_refresh_time);
    _power_is_on = false;
}

void NMEPD420SSD1683::updateFast()
{
    _writeCommand(0x22);
    _writeData(0xDC);
    _writeCommand(0x20);
    _waitWhileBusy("_Update_Part", partial_refresh_time);
    _power_is_on = true;
}

#endif
