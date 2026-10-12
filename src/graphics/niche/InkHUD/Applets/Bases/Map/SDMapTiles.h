#pragma once

#ifdef MESHTASTIC_INCLUDE_INKHUD

#include <stddef.h>
#include <stdint.h>

namespace NicheGraphics::InkHUD::SDMapTiles
{

void preload();
bool hasZoom(int zoom);
bool readTileOrParent(int zoom, int x, int y, uint8_t *destination, size_t destinationSize, uint8_t &sourceZoom);

} // namespace NicheGraphics::InkHUD::SDMapTiles

#endif
