#include "configuration.h"

#if defined(USE_EINK) && defined(NM_EPD_420) && !defined(USE_EINK_PARALLELDISPLAY)

#include "NMEPD420UC8179.h"

NMEPD420UC8179::NMEPD420UC8179(int16_t cs, int16_t dc, int16_t rst, int16_t busy, SPIClass &spi)
    : GxEPD2_EPD(cs, dc, rst, busy, LOW, 25000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate,
                 spi)
{
}

uint8_t NMEPD420UC8179::remapPlaneData(uint8_t command, uint8_t data)
{
    return command == 0x13 ? static_cast<uint8_t>(~data) : data;
}

void NMEPD420UC8179::clearScreen(uint8_t value)
{
    writeScreenBuffer(value);
    refresh(false);
    _initial_write = false;
}

void NMEPD420UC8179::writeScreenBuffer(uint8_t value)
{
    if (!_init_display_done)
        initDisplay();
    writeScreenBuffer(0x10, value);
    writeScreenBuffer(0x13, 0xFF);
    _initial_write = false;
}

void NMEPD420UC8179::writeScreenBuffer(uint8_t command, uint8_t value)
{
    setPartialRamArea(0, 0, WIDTH, HEIGHT);
    _writeCommand(command);
    _startTransfer();
    const uint8_t tx = remapPlaneData(command, value);
    for (uint32_t index = 0; index < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; ++index)
        _transfer(tx);
    _endTransfer();
}

void NMEPD420UC8179::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                                bool mirror_y, bool pgm)
{
    if (!_init_display_done)
        initDisplay();
    if (_initial_write)
        writeScreenBuffer();
    writeScreenBuffer(0x13, 0xFF);
    writeImage(0x10, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void NMEPD420UC8179::writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                bool invert, bool mirror_y, bool pgm)
{
    delay(1);
    const int16_t bitmapWidthBytes = (w + 7) / 8;
    x -= x % 8;
    w = bitmapWidthBytes * 8;
    const int16_t clippedX = x < 0 ? 0 : x;
    const int16_t clippedY = y < 0 ? 0 : y;
    int16_t clippedWidth = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
    int16_t clippedHeight = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
    const int16_t deltaX = clippedX - x;
    const int16_t deltaY = clippedY - y;
    clippedWidth -= deltaX;
    clippedHeight -= deltaY;
    if (clippedWidth <= 0 || clippedHeight <= 0)
        return;

    _writeCommand(0x91);
    setPartialRamArea(clippedX, clippedY, clippedWidth, clippedHeight);
    _writeCommand(command);
    _startTransfer();
    for (int16_t row = 0; row < clippedHeight; ++row) {
        for (int16_t column = 0; column < clippedWidth / 8; ++column) {
            const int16_t index = mirror_y ? column + deltaX / 8 + (h - 1 - (row + deltaY)) * bitmapWidthBytes
                                           : column + deltaX / 8 + (row + deltaY) * bitmapWidthBytes;
            uint8_t data = pgm ? pgm_read_byte(&bitmap[index]) : bitmap[index];
            if (invert)
                data = ~data;
            _transfer(remapPlaneData(command, data));
        }
    }
    _endTransfer();
    _writeCommand(0x92);
    delay(1);
}

void NMEPD420UC8179::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
                                    int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                                    bool mirror_y, bool pgm)
{
    if (!_init_display_done)
        initDisplay();
    if (_initial_write)
        writeScreenBuffer();
    writeScreenBuffer(0x13, 0xFF);
    writeImagePart(0x10, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void NMEPD420UC8179::writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                    int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
                                    bool invert, bool mirror_y, bool pgm)
{
    delay(1);
    if (w_bitmap < 0 || h_bitmap < 0 || w < 0 || h < 0 || x_part < 0 || x_part >= w_bitmap || y_part < 0 ||
        y_part >= h_bitmap)
        return;

    const int16_t bitmapWidthBytes = (w_bitmap + 7) / 8;
    x_part -= x_part % 8;
    w = w_bitmap - x_part < w ? w_bitmap - x_part : w;
    h = h_bitmap - y_part < h ? h_bitmap - y_part : h;
    x -= x % 8;
    w = 8 * ((w + 7) / 8);
    const int16_t clippedX = x < 0 ? 0 : x;
    const int16_t clippedY = y < 0 ? 0 : y;
    int16_t clippedWidth = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
    int16_t clippedHeight = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
    const int16_t deltaX = clippedX - x;
    const int16_t deltaY = clippedY - y;
    clippedWidth -= deltaX;
    clippedHeight -= deltaY;
    if (clippedWidth <= 0 || clippedHeight <= 0)
        return;

    _writeCommand(0x91);
    setPartialRamArea(clippedX, clippedY, clippedWidth, clippedHeight);
    _writeCommand(command);
    _startTransfer();
    for (int16_t row = 0; row < clippedHeight; ++row) {
        for (int16_t column = 0; column < clippedWidth / 8; ++column) {
            const uint32_t index = mirror_y
                                       ? x_part / 8 + column + deltaX / 8 +
                                             uint32_t(h_bitmap - 1 - (y_part + row + deltaY)) * bitmapWidthBytes
                                       : x_part / 8 + column + deltaX / 8 +
                                             uint32_t(y_part + row + deltaY) * bitmapWidthBytes;
            uint8_t data = pgm ? pgm_read_byte(&bitmap[index]) : bitmap[index];
            if (invert)
                data = ~data;
            _transfer(remapPlaneData(command, data));
        }
    }
    _endTransfer();
    _writeCommand(0x92);
    delay(1);
}

void NMEPD420UC8179::refresh(bool partial_update_mode)
{
    (void)partial_update_mode;
    updateFull();
}

void NMEPD420UC8179::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    updateFull();
}

void NMEPD420UC8179::powerOff()
{
    if (_power_is_on) {
        _writeCommand(0x02);
        _waitWhileBusy("_PowerOff", power_off_time);
        _power_is_on = false;
    }
}

void NMEPD420UC8179::hibernate()
{
    powerOff();
    if (_rst >= 0) {
        _writeCommand(0x07);
        _writeData(0xA5);
        _hibernating = true;
        _init_display_done = false;
    }
}

void NMEPD420UC8179::setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    const uint16_t endX = (x + w - 1) | 0x0007;
    const uint16_t endY = y + h - 1;
    x &= 0xFFF8;
    _writeCommand(0x90);
    _writeData(x / 256);
    _writeData(x % 256);
    _writeData(endX / 256);
    _writeData(endX % 256);
    _writeData(y / 256);
    _writeData(y % 256);
    _writeData(endY / 256);
    _writeData(endY % 256);
    _writeData(0x00);
}

void NMEPD420UC8179::powerOn()
{
    if (!_power_is_on) {
        _writeCommand(0x04);
        _waitWhileBusy("_PowerOn", power_on_time);
        _power_is_on = true;
    }
}

void NMEPD420UC8179::initDisplay()
{
    if (_hibernating)
        _reset();
    _writeCommand(0x01);
    _writeData(0x07);
    _writeData(0x07);
    _writeData(0x3F);
    _writeData(0x3F);
    _writeCommand(0x06);
    _writeData(0x17);
    _writeData(0x17);
    _writeData(0x28);
    _writeData(0x17);
    _writeCommand(0x00);
    _writeData(0x0F);
    _writeCommand(0x61);
    _writeData(WIDTH / 256);
    _writeData(WIDTH % 256);
    _writeData(HEIGHT / 256);
    _writeData(HEIGHT % 256);
    _writeCommand(0x15);
    _writeData(0x00);
    _writeCommand(0x50);
    _writeData(0x13);
    _writeData(0x07);
    _writeCommand(0x60);
    _writeData(0x22);
    _init_display_done = true;
}

void NMEPD420UC8179::updateFull()
{
    _writeCommand(0x00);
    _writeData(0x0F);
    _writeCommand(0x50);
    _writeData(0x13);
    _writeData(0x07);
    _writeCommand(0xE0);
    _writeData(0x00);
    _writeCommand(0x41);
    _writeData(0x00);
    powerOn();
    _writeCommand(0x12);
    _waitWhileBusy("_Update_Full", full_refresh_time);
}

#endif
