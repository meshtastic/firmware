#pragma once

#ifdef MESHTASTIC_INCLUDE_INKHUD

#include <stddef.h>
#include <stdint.h>

namespace NicheGraphics::InkHUD::MapTileUtils
{

constexpr size_t TILE_BYTES = 8192;
constexpr uint8_t LAYOUT_SPARSE = 0;
constexpr uint8_t LAYOUT_GRID = 1;
constexpr uint8_t KIND_LZ4 = 0;
constexpr uint8_t KIND_WHITE = 1;
constexpr uint8_t KIND_BLACK = 2;

int decompress(const uint8_t *source, size_t sourceLength, uint8_t *destination, size_t destinationCapacity);

} // namespace NicheGraphics::InkHUD::MapTileUtils

#endif
