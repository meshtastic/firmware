#pragma once

#ifdef MESHTASTIC_INCLUDE_INKHUD

#include <stddef.h>
#include <stdint.h>

namespace NicheGraphics::InkHUD::MapTileUtils
{

constexpr uint16_t TILE_SIZE = 256;
constexpr size_t TILE_BYTES = TILE_SIZE * TILE_SIZE / 8;
constexpr uint8_t LAYOUT_SPARSE = 0;
constexpr uint8_t LAYOUT_GRID = 1;
constexpr uint8_t KIND_LZ4 = 0;
constexpr uint8_t KIND_WHITE = 1;
constexpr uint8_t KIND_BLACK = 2;

constexpr size_t mtb1PixelOffset(uint16_t x, uint16_t y)
{
    return (x / 8) * TILE_SIZE + y;
}

constexpr uint8_t mtb1PixelMask(uint16_t x)
{
    return 0x80U >> (x % 8);
}

// MTB1 matches native e-ink polarity: one is white and zero is black.
constexpr bool mtb1PixelIsWhite(uint8_t packedPixels, uint16_t x)
{
    return packedPixels & mtb1PixelMask(x);
}

constexpr uint16_t ancestorPixel(uint32_t tileCoordinate, uint16_t pixel, uint8_t zoomDifference)
{
    return ((static_cast<uint32_t>(tileCoordinate) * TILE_SIZE + pixel) >> zoomDifference) % TILE_SIZE;
}

int decompress(const uint8_t *source, size_t sourceLength, uint8_t *destination, size_t destinationCapacity);

} // namespace NicheGraphics::InkHUD::MapTileUtils

#endif
