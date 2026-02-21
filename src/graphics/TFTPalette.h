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
constexpr uint16_t Blue = rgb565(0, 0, 255);
constexpr uint16_t Yellow = rgb565(255, 255, 0);
constexpr uint16_t Orange = rgb565(255, 165, 0);
constexpr uint16_t Cyan = rgb565(0, 255, 255);
constexpr uint16_t Magenta = rgb565(255, 0, 255);

constexpr uint16_t Good = Green;
constexpr uint16_t Medium = Yellow;
constexpr uint16_t Bad = Red;

} // namespace TFTPalette
} // namespace graphics

