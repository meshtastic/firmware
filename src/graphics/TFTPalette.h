#pragma once

#include <stdint.h>

namespace graphics
{
namespace TFTPalette
{

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return static_cast<uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | ((blue & 0xF8) >> 3));
}

constexpr uint16_t Black = 0x0000;
constexpr uint16_t White = 0xFFFF;
constexpr uint16_t DarkGray = 0x4208;
constexpr uint16_t Gray = 0x8410;
constexpr uint16_t LightGray = 0xC618;

constexpr uint16_t Red = rgb565(255, 0, 0);
constexpr uint16_t Green = rgb565(0, 255, 0);
constexpr uint16_t Blue = rgb565(0, 130, 252);
constexpr uint16_t Yellow = rgb565(255, 255, 0);
constexpr uint16_t Orange = rgb565(255, 165, 0);
constexpr uint16_t Cyan = rgb565(0, 255, 255);
constexpr uint16_t Magenta = rgb565(255, 0, 255);

constexpr uint16_t Good = Green;
constexpr uint16_t Medium = Yellow;
constexpr uint16_t Bad = Red;

// Christmas / seasonal accent colors
constexpr uint16_t ChristmasRed = rgb565(178, 34, 34);
constexpr uint16_t ChristmasGreen = rgb565(0, 128, 0);
constexpr uint16_t Gold = rgb565(255, 215, 0);
constexpr uint16_t Pine = rgb565(15, 35, 10);

// Pink theme colors (light variant)
constexpr uint16_t HotPink = rgb565(255, 105, 180);
constexpr uint16_t PalePink = rgb565(255, 228, 235);
constexpr uint16_t DeepPink = rgb565(200, 50, 120);

// Blue theme colors (dark variant)
constexpr uint16_t SkyBlue = rgb565(100, 180, 255);
constexpr uint16_t Navy = rgb565(15, 15, 50);
constexpr uint16_t DeepBlue = rgb565(30, 60, 120);

// Creamsicle theme colors (light variant)
constexpr uint16_t CreamOrange = rgb565(255, 140, 50);
constexpr uint16_t DeepOrange = rgb565(220, 100, 20);
constexpr uint16_t Cream = rgb565(255, 248, 235);

// Classic monochrome theme accent colors (single-color-on-black themes)
constexpr uint16_t MeshtasticGreen = rgb565(0x67, 0xEA, 0x94);
constexpr uint16_t ClassicRed = rgb565(255, 64, 64);
// Monochrome White reuses TFTPalette::White above.

// Fast contrast picker for monochrome glyph overlays on arbitrary RGB565 backgrounds.
// Uses channel-sum brightness approximation to keep code size small.
constexpr uint16_t pickReadableMonoFg(uint16_t backgroundColor)
{
    const uint16_t r = (backgroundColor >> 11) & 0x1F;
    const uint16_t g = (backgroundColor >> 5) & 0x3F;
    const uint16_t b = backgroundColor & 0x1F;
    return ((r + g + b) >= 70) ? DarkGray : White;
}

} // namespace TFTPalette
} // namespace graphics
