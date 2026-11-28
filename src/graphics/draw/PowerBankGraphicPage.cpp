#include "configuration.h"

#if HAS_SCREEN && HAS_POWERBANK && USE_POWER_BANK_GRAPHIC_PAGE
// Layout and drawing from Heltec's Mesh Pocket V2 firmware (HelTecAutomation/firmware 10bb594, GPL-3.0), reading
// PowerBankStatus/PowerBankDetail. Fixed to a 220x128 colour TFT.
#include "PowerBankPage.h"
#include "graphics/ScreenFonts.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include <stdio.h>

namespace graphics
{
namespace PowerBankPage
{
namespace
{
constexpr uint16_t POWER_BANK_BACKGROUND = TFTPalette::rgb565(0, 12, 24);
constexpr uint16_t POWER_BANK_INK = TFTPalette::White;
constexpr uint16_t POWER_BANK_BLUE = TFTPalette::rgb565(98, 167, 234);
constexpr uint16_t POWER_BANK_PURPLE = TFTPalette::rgb565(96, 32, 255);
constexpr uint16_t POWER_BANK_CARD = TFTPalette::rgb565(217, 222, 227);
constexpr uint16_t POWER_BANK_CARD_INK = TFTPalette::rgb565(48, 58, 68);
constexpr uint16_t POWER_BANK_BATTERY_FILL = TFTPalette::rgb565(23, 39, 57);
constexpr uint16_t POWER_BANK_GREEN = TFTPalette::rgb565(3, 214, 117);
constexpr uint16_t POWER_BANK_CHARGING = TFTPalette::rgb565(54, 241, 159);
constexpr uint16_t POWER_BANK_WIRELESS = TFTPalette::rgb565(0, 234, 140);
constexpr uint16_t POWER_BANK_WIRELESS_TEXT = TFTPalette::rgb565(0, 62, 48);
constexpr uint16_t POWER_BANK_GOLD = TFTPalette::rgb565(254, 225, 113);
constexpr uint16_t POWER_BANK_OFF = TFTPalette::rgb565(58, 77, 92);
constexpr uint16_t POWER_BANK_FAULT = TFTPalette::rgb565(255, 77, 77);
constexpr uint16_t POWER_BANK_DIVIDER = TFTPalette::rgb565(151, 160, 170);
constexpr uint16_t POWER_BANK_LIGHT_DIVIDER = TFTPalette::rgb565(208, 227, 241);
constexpr uint16_t POWER_BANK_TRACK = TFTPalette::rgb565(150, 149, 198);
constexpr uint16_t POWER_BANK_SHINE = TFTPalette::rgb565(68, 250, 172);
constexpr uint16_t POWER_BANK_BADGE = TFTPalette::rgb565(249, 244, 254);
constexpr uint16_t POWER_BANK_TEMP_BACKGROUND = TFTPalette::rgb565(245, 238, 182);
constexpr uint16_t POWER_BANK_TEMP_VALUE = TFTPalette::rgb565(163, 142, 40);
constexpr uint16_t POWER_BANK_BOLT = TFTPalette::rgb565(150, 118, 214);

// Lightning bolt, 16x16 XBM.
constexpr uint8_t POWER_BANK_BOLT_WIDTH = 16;
constexpr uint8_t POWER_BANK_BOLT_HEIGHT = 16;
const uint8_t POWER_BANK_BOLT_XBM[] PROGMEM = {
    0x00, 0x00, 0x00, 0x06, 0x00, 0x07, 0x80, 0x03, 0xC0, 0x03, 0xE0, 0x01, 0xF0, 0x3F, 0xF8, 0x1F,
    0xC0, 0x0F, 0xC0, 0x07, 0xC0, 0x03, 0xE0, 0x01, 0xE0, 0x00, 0x70, 0x00, 0x30, 0x00, 0x00, 0x00,
};

// Custom round digits, 8x12.
// One byte per row, left pixel in bit 0; all display scales reuse this table.
constexpr uint8_t POWER_BANK_DIGITS[10][12] = {
    {0x38, 0x44, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x44, 0x38},
    {0x10, 0x18, 0x14, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
    {0x38, 0x44, 0x82, 0x80, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0xfc},
    {0x38, 0x44, 0x82, 0x80, 0x40, 0x38, 0x40, 0x80, 0x80, 0x82, 0x44, 0x38},
    {0x40, 0x60, 0x50, 0x48, 0x44, 0x42, 0x42, 0xfe, 0x40, 0x40, 0x40, 0x40},
    {0xfc, 0x02, 0x02, 0x02, 0x3e, 0x40, 0x80, 0x80, 0x80, 0x82, 0x44, 0x38},
    {0x38, 0x44, 0x02, 0x02, 0x3e, 0x42, 0x82, 0x82, 0x82, 0x82, 0x44, 0x38},
    {0xfe, 0x80, 0x40, 0x40, 0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04},
    {0x38, 0x44, 0x82, 0x82, 0x44, 0x38, 0x44, 0x82, 0x82, 0x82, 0x44, 0x38},
    {0x38, 0x44, 0x82, 0x82, 0x82, 0x82, 0x84, 0xf8, 0x80, 0x80, 0x44, 0x38},
};

struct PowerBankTextMetrics {
    uint16_t width = 0;
    uint8_t top = 0;
    uint8_t height = 0;
};

PowerBankTextMetrics measurePowerBankText(const uint8_t *font, const char *text)
{
    PowerBankTextMetrics result;
    const uint8_t height = pgm_read_byte(font + HEIGHT_POS);
    const uint8_t first = pgm_read_byte(font + FIRST_CHAR_POS);
    const uint8_t count = pgm_read_byte(font + CHAR_NUM_POS);
    const uint8_t rasterHeight = (height + 7) / 8;
    const uint16_t dataStart = JUMPTABLE_START + count * JUMPTABLE_BYTES;
    uint8_t top = height;
    uint8_t bottom = 0;
    for (const char *p = text; *p; ++p) {
        const uint8_t code = static_cast<uint8_t>(*p);
        if (code < first || code - first >= count)
            return {};
        const uint8_t *entry = font + JUMPTABLE_START + (code - first) * JUMPTABLE_BYTES;
        const uint16_t offset = (pgm_read_byte(entry) << 8) | pgm_read_byte(entry + JUMPTABLE_LSB);
        result.width += font == FONT_MEDIUM_LOCAL && code == '1' ? 7 : pgm_read_byte(entry + JUMPTABLE_WIDTH);
        if (offset == 0xffff)
            continue;
        const uint8_t size = pgm_read_byte(entry + JUMPTABLE_SIZE);
        for (uint16_t i = 0; i < size; ++i) {
            const uint8_t bits = pgm_read_byte(font + dataStart + offset + i);
            for (uint8_t bit = 0; bit < 8; ++bit) {
                const uint8_t glyphY = (i % rasterHeight) * 8 + bit;
                if (glyphY < height && (bits & (1U << bit))) {
                    if (glyphY < top)
                        top = glyphY;
                    if (glyphY > bottom)
                        bottom = glyphY;
                }
            }
        }
    }
    if (top < height) {
        result.top = top;
        result.height = bottom - top + 1;
    }
    return result;
}

void drawPowerBankText(OLEDDisplay *display, int16_t x, int16_t y, const char *text, const uint8_t *font, uint8_t scale,
                       uint16_t color, uint16_t background = POWER_BANK_BACKGROUND)
{
    const PowerBankTextMetrics metrics = measurePowerBankText(font, text);
    if (!metrics.width || !metrics.height)
        return;
    const uint8_t first = pgm_read_byte(font + FIRST_CHAR_POS);
    const uint8_t count = pgm_read_byte(font + CHAR_NUM_POS);
    const uint8_t rasterHeight = (pgm_read_byte(font + HEIGHT_POS) + 7) / 8;
    const uint16_t dataStart = JUMPTABLE_START + count * JUMPTABLE_BYTES;
    registerTFTColorRegionDirect(x, y, metrics.width * scale, metrics.height * scale, color, background);
    // Clear the mono mask so text can sit on a filled color panel.
    display->setColor(BLACK);
    display->fillRect(x, y, metrics.width * scale, metrics.height * scale);
    display->setColor(WHITE);
    for (const char *p = text; *p; ++p) {
        const uint8_t code = static_cast<uint8_t>(*p);
        if (font == FONT_MEDIUM_LOCAL && code >= '0' && code <= '9') {
            const uint8_t *rows = POWER_BANK_DIGITS[code - '0'];
            for (uint8_t row = 0; row < 12; ++row)
                for (uint8_t column = 0; column < 8; ++column)
                    if (rows[row] & (1U << column))
                        display->fillRect(x + column * scale, y + row * scale, scale, scale);
            x += (code == '1' ? 7 : 9) * scale;
            continue;
        }
        const uint8_t *entry = font + JUMPTABLE_START + (code - first) * JUMPTABLE_BYTES;
        const uint16_t offset = (pgm_read_byte(entry) << 8) | pgm_read_byte(entry + JUMPTABLE_LSB);
        const uint8_t size = pgm_read_byte(entry + JUMPTABLE_SIZE);
        if (offset != 0xffff) {
            for (uint16_t i = 0; i < size; ++i) {
                const uint8_t bits = pgm_read_byte(font + dataStart + offset + i);
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    const uint8_t glyphY = (i % rasterHeight) * 8 + bit;
                    if (glyphY >= metrics.top && glyphY < metrics.top + metrics.height && (bits & (1U << bit)))
                        display->fillRect(x + (i / rasterHeight) * scale, y + (glyphY - metrics.top) * scale, scale, scale);
                }
            }
        }
        x += pgm_read_byte(entry + JUMPTABLE_WIDTH) * scale;
    }
}

void fillPowerBankCircle(OLEDDisplay *display, int16_t centerX, int16_t centerY, int16_t radius)
{
    int16_t halfWidth = radius;
    for (int16_t row = 0; row <= radius; ++row) {
        while (halfWidth * halfWidth + row * row > radius * radius)
            --halfWidth;
        display->drawHorizontalLine(centerX - halfWidth, centerY + row, 2 * halfWidth + 1);
        if (row)
            display->drawHorizontalLine(centerX - halfWidth, centerY - row, 2 * halfWidth + 1);
    }
}

void drawPowerBankRect(OLEDDisplay *display, int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color)
{
    registerTFTColorRegionDirect(x, y, width, height, color, color);
    display->setColor(WHITE);
    display->fillRect(x, y, width, height);
}

void drawPowerBankRoundBox(OLEDDisplay *display, int16_t x, int16_t y, int16_t width, int16_t height, int16_t radius,
                           uint16_t color, uint16_t background)
{
    registerTFTColorRegionDirect(x, y, width, height, color, background);
    display->setColor(BLACK);
    display->fillRect(x, y, width, height);
    display->setColor(WHITE);
    display->fillRect(x + radius, y, width - 2 * radius, height);
    display->fillRect(x, y + radius, width, height - 2 * radius);
    for (int16_t cx : {x + radius, x + width - radius - 1})
        for (int16_t cy : {y + radius, y + height - radius - 1})
            fillPowerBankCircle(display, cx, cy, radius);
}

void drawPowerBankDisk(OLEDDisplay *display, int16_t x, int16_t y, int16_t radius, uint16_t color, uint16_t background)
{
    registerTFTColorRegionDirect(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1, color, background);
    display->setColor(BLACK);
    display->fillRect(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1);
    display->setColor(WHITE);
    fillPowerBankCircle(display, x, y, radius);
}

void drawPowerBankBadge(OLEDDisplay *display, int16_t x, int16_t y, int16_t width, int16_t height, const char *text,
                        uint16_t color, uint16_t ink, int8_t textOffsetX = 0)
{
    const PowerBankTextMetrics metrics = measurePowerBankText(FONT_SMALL_LOCAL, text);
    if (width < metrics.width + 6)
        width = metrics.width + 6;
    drawPowerBankRoundBox(display, x, y, width, height, 3, color, POWER_BANK_CARD);
    drawPowerBankText(display, x + (width - metrics.width) / 2 + textOffsetX, y + (height - metrics.height) / 2, text,
                      FONT_SMALL_LOCAL, 1, ink, color);
}

void drawPowerBankBattery(OLEDDisplay *display, int16_t x, int16_t y, bool valid, uint8_t percent)
{
    drawPowerBankText(display, x + 8, y + 11, "Battery", FONT_SMALL_LOCAL, 1, POWER_BANK_INK, POWER_BANK_BLUE);
    char value[4] = "--";
    if (valid)
        snprintf(value, sizeof(value), "%u", percent);
    const PowerBankTextMetrics soc = measurePowerBankText(FONT_MEDIUM_LOCAL, value);
    drawPowerBankText(display, x + 8, y + (valid ? 25 : 30), value, FONT_MEDIUM_LOCAL, 1, POWER_BANK_INK, POWER_BANK_BLUE);
    drawPowerBankText(display, x + 10 + soc.width, y + 30, "%", FONT_SMALL_LOCAL, 1, POWER_BANK_INK, POWER_BANK_BLUE);
    drawPowerBankRoundBox(display, x + 49, y + 16, 40, 23, 4, POWER_BANK_INK, POWER_BANK_BLUE);
    drawPowerBankRect(display, x + 89, y + 23, 4, 9, POWER_BANK_INK);
    drawPowerBankRect(display, x + 90, y + 24, 2, 7, POWER_BANK_BATTERY_FILL);
    drawPowerBankRoundBox(display, x + 50, y + 17, 38, 21, 3, POWER_BANK_BATTERY_FILL, POWER_BANK_INK);
    // These inner-box corners lie outside the white battery outline.
    for (int16_t px : {x + 50, x + 87})
        for (int16_t py : {y + 17, y + 37})
            drawPowerBankRect(display, px, py, 1, 1, POWER_BANK_BLUE);
    const int16_t fill = valid ? (34 * percent + 50) / 100 : 0;
    if (fill) {
        drawPowerBankRect(display, x + 52, y + 19, fill, 17, POWER_BANK_GREEN);
        drawPowerBankRect(display, x + 52, y + 19, fill, 7, POWER_BANK_SHINE);
    }
    drawPowerBankRect(display, x + 101, y + 9, 1, 51, POWER_BANK_LIGHT_DIVIDER);
    drawPowerBankRect(display, x + 8, y + 44, 85, 1, POWER_BANK_LIGHT_DIVIDER);
    drawPowerBankRect(display, x + 8, y + 50, 85, 8, POWER_BANK_INK);
    const int16_t filled = valid ? (83 * percent + 50) / 100 : 0;
    if (filled)
        registerTFTColorRegionDirect(x + 9, y + 51, filled, 6, POWER_BANK_INK, POWER_BANK_GREEN);
    if (filled < 83)
        registerTFTColorRegionDirect(x + 9 + filled, y + 51, 83 - filled, 6, POWER_BANK_INK, POWER_BANK_TRACK);
    display->setColor(BLACK);
    display->fillRect(x + 9, y + 51, 83, 6);
    display->setColor(WHITE);
    for (uint8_t i = 1; i < 10; ++i)
        display->drawVerticalLine(x + 9 + i * 83 / 10, y + 51, 6);
}

// hot: the controller's NTC over-temperature flag, which the gauge's cell temperature may not show. Rings the badge red.
void drawPowerBankTemperature(OLEDDisplay *display, int16_t x, int16_t y, bool valid, int16_t valueX10, bool hot)
{
    const uint16_t ring = hot ? POWER_BANK_FAULT : POWER_BANK_BADGE;
    drawPowerBankDisk(display, x + 132, y + 24, 11, ring, POWER_BANK_BLUE);
    // Clip inner color regions to their parent disks, preserving the blue corners.
    registerTFTColorRegionDirect(x + 126, y + 15, 13, 19, ring, POWER_BANK_TEMP_VALUE);
    registerTFTColorRegionDirect(x + 123, y + 18, 19, 13, ring, POWER_BANK_TEMP_VALUE);
    display->setColor(BLACK);
    fillPowerBankCircle(display, x + 132, y + 24, 9);
    registerTFTColorRegionDirect(x + 128, y + 16, 9, 17, POWER_BANK_TEMP_BACKGROUND, POWER_BANK_TEMP_VALUE);
    registerTFTColorRegionDirect(x + 124, y + 20, 17, 9, POWER_BANK_TEMP_BACKGROUND, POWER_BANK_TEMP_VALUE);
    registerTFTColorRegionDirect(x + 126, y + 18, 13, 13, POWER_BANK_TEMP_BACKGROUND, POWER_BANK_TEMP_VALUE);
    display->setColor(WHITE);
    fillPowerBankCircle(display, x + 132, y + 24, 8);
    drawPowerBankText(display, x + 129, y + 20, "T", FONT_SMALL_LOCAL, 1, POWER_BANK_TEMP_VALUE, POWER_BANK_TEMP_BACKGROUND);
    char value[16] = "--";
    if (valid) {
        const int32_t temperature = valueX10;
        const uint32_t magnitude = temperature < 0 ? -temperature : temperature;
        snprintf(value, sizeof(value), "%s%lu.%lu", temperature < 0 ? "-" : "", (unsigned long)(magnitude / 10),
                 (unsigned long)(magnitude % 10));
    }
    const PowerBankTextMetrics metrics = measurePowerBankText(FONT_MEDIUM_LOCAL, value);
    const char *unitText = "\xb0"
                           "C";
    const PowerBankTextMetrics unit = measurePowerBankText(FONT_SMALL_LOCAL, unitText);
    const uint16_t width = metrics.width + 1 + unit.width;
    if (width <= 58) {
        const int16_t left = x + 132 - width / 2;
        drawPowerBankText(display, left, y + (valid ? 44 : 49), value, FONT_MEDIUM_LOCAL, 1, POWER_BANK_INK, POWER_BANK_BLUE);
        drawPowerBankText(display, left + metrics.width + 1, y + 56 - unit.height, unitText, FONT_SMALL_LOCAL, 1, POWER_BANK_INK,
                          POWER_BANK_BLUE);
    } else {
        char fullValue[20];
        snprintf(fullValue, sizeof(fullValue), "%s%s", value, unitText);
        const PowerBankTextMetrics small = measurePowerBankText(FONT_SMALL_LOCAL, fullValue);
        drawPowerBankText(display, x + 132 - small.width / 2, y + 47, fullValue, FONT_SMALL_LOCAL, 1, POWER_BANK_INK,
                          POWER_BANK_BLUE);
    }
}

void drawPowerBankPower(OLEDDisplay *display, int16_t x, int16_t y, const char *value, bool showRays)
{
    drawPowerBankRoundBox(display, x + 169, y + 2, 49, 66, 6, POWER_BANK_PURPLE, POWER_BANK_BACKGROUND);
    if (showRays) {
        registerTFTColorRegionDirect(x + 176, y + 7, 35, 35, POWER_BANK_BADGE, POWER_BANK_PURPLE);
        display->setColor(BLACK);
        display->fillRect(x + 176, y + 7, 35, 35);
    }
    drawPowerBankDisk(display, x + 193, y + 24, 11, POWER_BANK_BADGE, POWER_BANK_PURPLE);
    if (showRays) {
        display->drawVerticalLine(x + 193, y + 7, 4);
        display->drawVerticalLine(x + 193, y + 38, 4);
        display->drawHorizontalLine(x + 176, y + 24, 4);
        display->drawHorizontalLine(x + 207, y + 24, 4);
        for (int8_t side : {-1, 1}) {
            display->drawLine(x + 193 + side * 10, y + 14, x + 193 + side * 12, y + 12);
            display->drawLine(x + 193 + side * 10, y + 34, x + 193 + side * 12, y + 36);
        }
    }
    // Only the nontransparent part of the XBM needs the tint region.
    registerTFTColorRegionDirect(x + 188, y + 17, 11, 14, POWER_BANK_BADGE, POWER_BANK_BOLT);
    display->setColor(BLACK);
    display->drawXbm(x + 185, y + 16, POWER_BANK_BOLT_WIDTH, POWER_BANK_BOLT_HEIGHT, POWER_BANK_BOLT_XBM);
    const uint8_t *font = FONT_MEDIUM_LOCAL;
    PowerBankTextMetrics metrics = measurePowerBankText(font, value);
    if (metrics.width + 6 > 43) {
        font = FONT_SMALL_LOCAL;
        metrics = measurePowerBankText(font, value);
    }
    const int16_t left = x + 193 - (metrics.width + 6) / 2;
    drawPowerBankText(display, left, y + (value[0] == '-' && value[1] == '-' ? 49 : 44), value, font, 1, POWER_BANK_INK,
                      POWER_BANK_PURPLE);
    const int16_t unitX = left + metrics.width + 1;
    registerTFTColorRegionDirect(unitX, y + 51, 5, 5, POWER_BANK_INK, POWER_BANK_PURPLE);
    display->setColor(BLACK);
    display->fillRect(unitX, y + 51, 5, 5);
    display->setColor(WHITE);
    display->drawLine(unitX, y + 51, unitX + 1, y + 55);
    display->drawLine(unitX + 1, y + 55, unitX + 2, y + 53);
    display->drawLine(unitX + 2, y + 53, unitX + 3, y + 55);
    display->drawLine(unitX + 3, y + 55, unitX + 4, y + 51);
}

bool isPortConnected(PowerBankDetail::PortState state)
{
    return state != PowerBankDetail::PORT_IDLE;
}

void drawPowerBankUsbPort(OLEDDisplay *display, int16_t x, int16_t y, const char *name, bool valid,
                          PowerBankDetail::PortState state, const char *fastCharge, bool sharedProtocol)
{
    drawPowerBankRoundBox(display, x, y + 72, 70, 54, 6, POWER_BANK_CARD, POWER_BANK_BACKGROUND);
    drawPowerBankText(display, x + 6, y + 78, name, FONT_MEDIUM_LOCAL, 1, POWER_BANK_CARD_INK, POWER_BANK_CARD);
    drawPowerBankRoundBox(display, x + 31, y + 83, 25, 9, 3, POWER_BANK_CARD_INK, POWER_BANK_CARD);
    drawPowerBankRoundBox(display, x + 32, y + 84, 23, 7, 2, POWER_BANK_CARD, POWER_BANK_CARD_INK);
    drawPowerBankRect(display, x + 36, y + 87, 15, 1, POWER_BANK_CARD_INK);
    drawPowerBankRect(display, x + 5, y + 97, 60, 1, POWER_BANK_DIVIDER);
    const char *label = "--";
    uint16_t color = POWER_BANK_OFF;
    if (valid) {
        switch (state) {
        case PowerBankDetail::PORT_IDLE:
            label = "OFF";
            break;
        case PowerBankDetail::PORT_CHARGING_BANK:
            label = "CHG";
            color = POWER_BANK_CHARGING;
            break;
        case PowerBankDetail::PORT_POWERING_DEVICE:
            label = "DCHG";
            color = POWER_BANK_GOLD;
            break;
        }
    }
    const bool connected = valid && isPortConnected(state);
    drawPowerBankBadge(display, x + 6, y + 102, 28, 9, label, color, connected ? TFTPalette::Black : POWER_BANK_INK);
    char protocol[16] = "";
    if (connected && fastCharge)
        snprintf(protocol, sizeof(protocol), "%s%s", fastCharge, sharedProtocol ? "*" : "");
    const uint8_t *font = FONT_MEDIUM_LOCAL;
    PowerBankTextMetrics metrics = measurePowerBankText(font, protocol);
    if (metrics.width > 58) {
        font = FONT_SMALL_LOCAL;
        metrics = measurePowerBankText(font, protocol);
    }
    drawPowerBankText(display, x + 6, y + 112 + (12 - metrics.height) / 2, protocol, font, 1, POWER_BANK_CARD_INK,
                      POWER_BANK_CARD);
}

void drawPowerBankWirelessPort(OLEDDisplay *display, int16_t x, int16_t y, bool valid, const PowerBankDetail &detail)
{
    drawPowerBankRoundBox(display, x + 148, y + 72, 70, 54, 6, POWER_BANK_CARD, POWER_BANK_BACKGROUND);
    drawPowerBankText(display, x + 154, y + 78, "Qi2", FONT_MEDIUM_LOCAL, 1, POWER_BANK_CARD_INK, POWER_BANK_CARD);
    const bool fod = valid && detail.wirelessFault;
    const bool active = valid && detail.wirelessActive;
    if (fod) {
        drawPowerBankBadge(display, x + 184, y + 83, 29, 10, "FOD!", POWER_BANK_FAULT, POWER_BANK_INK);
    } else {
        const uint16_t color = active ? POWER_BANK_WIRELESS : POWER_BANK_OFF;
        drawPowerBankRoundBox(display, x + 182, y + 82, 32, 11, 4, color, POWER_BANK_CARD);
        if (active) {
            drawPowerBankText(display, x + 188, y + 84, "ON", FONT_SMALL_LOCAL, 1, POWER_BANK_WIRELESS_TEXT, color);
            drawPowerBankDisk(display, x + 209, y + 87, 3, POWER_BANK_INK, color);
        } else {
            drawPowerBankDisk(display, x + 187, y + 87, 3, POWER_BANK_INK, color);
            drawPowerBankText(display, x + 193, y + (valid ? 84 : 87), valid ? "OFF" : "--", FONT_SMALL_LOCAL, 1, POWER_BANK_INK,
                              color);
        }
    }
    drawPowerBankRect(display, x + 153, y + 97, 60, 1, POWER_BANK_DIVIDER);
    drawPowerBankText(display, x + 154, y + 103, "FOD:", FONT_SMALL_LOCAL, 1, POWER_BANK_CARD_INK, POWER_BANK_CARD);
    const char *fodText = "--";
    uint16_t fodColor = POWER_BANK_OFF;
    if (valid) {
        fodText = fod ? "1" : "0";
        fodColor = fod ? POWER_BANK_FAULT : POWER_BANK_GOLD;
    }
    drawPowerBankBadge(display, x + 182, y + 102, 13, 9, fodText, fodColor, valid ? TFTPalette::Black : POWER_BANK_INK, 1);
    if (active && detail.wirelessProtocol)
        drawPowerBankBadge(display, x + 154, y + 113, 0, 10, detail.wirelessProtocol, POWER_BANK_GOLD, TFTPalette::Black);
}
} // namespace

void drawGraphicFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    display->clear();
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    registerTFTColorRegionDirect(0, 0, display->getWidth(), display->getHeight(), POWER_BANK_INK, POWER_BANK_BACKGROUND);

    PowerBankStatus status;
    PowerBankDetail detail;
    const bool haveStatus = powerBank && powerBank->getStatus(status);
    const bool haveDetail = powerBank && powerBank->getDetail(detail);
    const bool live = haveDetail && detail.live;
    const uint8_t percent = status.batteryPercent < 0 ? 0 : (status.batteryPercent > 100 ? 100 : status.batteryPercent);

    drawPowerBankRoundBox(display, x + 2, y + 2, 164, 66, 6, POWER_BANK_BLUE, POWER_BANK_BACKGROUND);
    drawPowerBankBattery(display, x, y, haveStatus && status.batteryPercent >= 0, percent);
    drawPowerBankTemperature(display, x, y, haveDetail && detail.hasBatteryTemperature, detail.batteryTempTenthsC,
                             live && detail.overTemperature);

    char value[16] = "--";
    if (live) {
        const uint32_t powerX10 = static_cast<uint32_t>(detail.busMv) * detail.busMa / 100000;
        snprintf(value, sizeof(value), "%lu.%lu", (unsigned long)(powerX10 / 10), (unsigned long)(powerX10 % 10));
    }
    const PowerBankDetail::PortState c1 = detail.numPorts > 0 ? detail.portStates[0] : PowerBankDetail::PORT_IDLE;
    const PowerBankDetail::PortState c2 = detail.numPorts > 1 ? detail.portStates[1] : PowerBankDetail::PORT_IDLE;
    const bool showRays = live && (status.charging || isPortConnected(c1) || isPortConnected(c2) || detail.wirelessActive);
    drawPowerBankPower(display, x, y, value, showRays);

    const bool sharedProtocol = isPortConnected(c1) && isPortConnected(c2);
    drawPowerBankUsbPort(display, x + 2, y, detail.numPorts > 0 ? detail.portNames[0] : "C1", live, c1, detail.fastCharge,
                         sharedProtocol);
    drawPowerBankUsbPort(display, x + 75, y, detail.numPorts > 1 ? detail.portNames[1] : "C2", live, c2, detail.fastCharge,
                         sharedProtocol);
    drawPowerBankWirelessPort(display, x, y, live, detail);
    display->setFont(FONT_SMALL_LOCAL);
}
} // namespace PowerBankPage
} // namespace graphics
#endif
