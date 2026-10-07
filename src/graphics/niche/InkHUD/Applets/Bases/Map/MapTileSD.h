#pragma once

#ifdef MESHTASTIC_INCLUDE_INKHUD

#include <stdint.h>

// Loads Map/MapTile.bin from the SD card as an alternative to tiles compiled into flash.
// See MapTileSD.cpp for the file format and MapApplet.cpp for how these are wired in.
namespace NicheGraphics::InkHUD::MapTileSD
{

// Call once, when MapTile.h has no tiles compiled in. Returns false on any failure.
bool tryLoad();
bool isLoaded();

uint8_t layout();
uint8_t gridCols();
uint8_t gridRows();
uint8_t blockCount();
uint32_t count();

const uint8_t *zooms();
const uint16_t *tx();
const uint16_t *ty();

const uint8_t *blockZooms();
const uint16_t *blockTx();
const uint16_t *blockTy();

const uint8_t *kinds();
const uint16_t *sizes();
const uint32_t *offsets();
const uint8_t *data();

} // namespace NicheGraphics::InkHUD::MapTileSD

#endif // MESHTASTIC_INCLUDE_INKHUD
