#include "W20EInkPanel.h"

#include <algorithm>

namespace
{
// DEPG0310BxU253 full-refresh waveforms from the W20 display driver.
constexpr uint8_t waveforms[5][42] = {
    {0x01, 0x13, 0x13, 0x00, 0x00, 0x01, 0x00, 0x01, 0x13, 0x13, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x13, 0x53, 0x00, 0x00, 0x01, 0x00, 0x01, 0x13, 0x93, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x13, 0x53, 0x00, 0x00, 0x01, 0x00, 0x01, 0x13, 0x93, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x93, 0x13, 0x00, 0x00, 0x01, 0x00, 0x01, 0x53, 0x13, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x93, 0x13, 0x00, 0x00, 0x01, 0x00, 0x01, 0x53, 0x13, 0x00, 0x00, 0x01, 0x00},
};
constexpr uint8_t previousFrame = 0x10;
constexpr uint8_t currentFrame = 0x13;
} // namespace

W20EInkPanel::W20EInkPanel(int16_t cs, int16_t dc, int16_t reset, int16_t busy)
    : GxEPD2_EPD(cs, dc, reset, busy, LOW, 10000000, WIDTH, HEIGHT, GxEPD2::GDEQ031T10, false, false, false)
{
}

void W20EInkPanel::initPanel()
{
    if (_init_display_done)
        return;

    _reset();
    _power_is_on = false;
    _hibernating = false;
    const uint8_t panelSetting[] = {0x00, 0xf7, 0x0d};
    const uint8_t border[] = {0x50, 0x97};
    const uint8_t tcon[] = {0x60, 0x22};
    const uint8_t pll[] = {0x30, 0x0d};
    const uint8_t powerSetting[] = {0x01, 0x03, 0x00, 0x3f, 0x3f, 0x10};
    const uint8_t vcomDc[] = {0x82, 0x0e};
    const uint8_t frameTiming[] = {0x2a, 0x00, 0x00, 0x00};

    _writeCommandData(panelSetting, sizeof(panelSetting));
    _writeCommandData(border, sizeof(border));
    _writeCommandData(tcon, sizeof(tcon));
    _writeCommandData(pll, sizeof(pll));
    _writeCommandData(powerSetting, sizeof(powerSetting));
    _writeCommandData(vcomDc, sizeof(vcomDc));
    _writeCommandData(frameTiming, sizeof(frameTiming));
    for (uint8_t index = 0; index < 5; ++index) {
        _writeCommand(0x20 + index);
        _writeData(waveforms[index], sizeof(waveforms[index]));
    }
    _init_display_done = true;
}

void W20EInkPanel::setWindow(int16_t x, int16_t y, int16_t w, int16_t h)
{
    const uint16_t bottom = y + h - 1;
    const uint8_t window[] = {0x90,
                              static_cast<uint8_t>(x),
                              static_cast<uint8_t>(x + w - 1),
                              static_cast<uint8_t>(y >> 8),
                              static_cast<uint8_t>(y),
                              static_cast<uint8_t>(bottom >> 8),
                              static_cast<uint8_t>(bottom),
                              0x01};
    _writeCommandData(window, sizeof(window));
}

void W20EInkPanel::writeScreenBuffer(uint8_t value)
{
    initPanel();
    for (uint8_t command : {previousFrame, currentFrame}) {
        _writeCommand(command);
        _startTransfer();
        for (uint16_t i = 0; i < WIDTH * HEIGHT / 8; ++i)
            _transfer(value);
        _endTransfer();
    }
}

void W20EInkPanel::clearScreen(uint8_t value)
{
    writeScreenBuffer(value);
    refresh();
}

void W20EInkPanel::writeImage(const uint8_t *bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirrorY,
                              bool pgm)
{
    writeImagePart(bitmap, 0, 0, w, h, x, y, w, h, invert, mirrorY, pgm);
}

void W20EInkPanel::writeImagePart(const uint8_t *bitmap, int16_t sourceX, int16_t sourceY, int16_t sourceWidth,
                                  int16_t sourceHeight, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirrorY,
                                  bool pgm)
{
    if (!bitmap || sourceX < 0 || sourceY < 0 || sourceX >= sourceWidth || sourceY >= sourceHeight || w <= 0 || h <= 0)
        return;

    sourceX &= ~7;
    x &= ~7;
    const int stride = (sourceWidth + 7) / 8;
    const int left = std::max(0, static_cast<int>(x));
    const int top = std::max(0, static_cast<int>(y));
    const int right = std::min<int>(WIDTH, x + std::min((w + 7) & ~7, stride * 8 - sourceX));
    const int bottom = std::min<int>(HEIGHT, y + std::min<int>(h, sourceHeight - sourceY));
    if (left >= right || top >= bottom)
        return;

    initPanel();
    _writeCommand(0x91);
    setWindow(left, top, right - left, bottom - top);
    for (uint8_t command : {previousFrame, currentFrame}) {
        _writeCommand(command);
        _startTransfer();
        for (int row = top; row < bottom; ++row) {
            const int sourceRow = sourceY + row - y;
            const int rowOffset = (mirrorY ? sourceHeight - 1 - sourceRow : sourceRow) * stride;
            for (int col = left; col < right; col += 8) {
                const int index = rowOffset + (sourceX + col - x) / 8;
                const uint8_t value = pgm ? pgm_read_byte(bitmap + index) : bitmap[index];
                _transfer(invert ? ~value : value);
            }
        }
        _endTransfer();
    }
    _writeCommand(0x92);
}

void W20EInkPanel::refresh(bool)
{
    _writeCommand(0x17);
    _writeData(0xa5);
    _waitWhileBusy("W20 refresh", 1100);
    _init_display_done = false;
}

void W20EInkPanel::refresh(int16_t, int16_t, int16_t, int16_t)
{
    refresh();
}

void W20EInkPanel::powerOff()
{
    if (!_power_is_on)
        return;
    _writeCommand(0x02);
    _waitWhileBusy("W20 power off", 50);
    _power_is_on = false;
}

void W20EInkPanel::hibernate()
{
    if (_hibernating)
        return;
    powerOff();
    _writeCommand(0x07);
    _writeData(0xa5);
    _hibernating = true;
    _init_display_done = false;
}
