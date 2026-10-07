#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MapTileSD.h"
#include "./MapTile.h" // compiled (always-empty, for this board) fallback data

#include "FSCommon.h"
#include "configuration.h"

#if defined(ARCH_ESP32) && defined(HAS_SDCARD) && !defined(SDCARD_USE_SOFT_SPI) && !defined(HAS_SD_MMC)

#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include <SD.h>
#include <stdlib.h>
#include <string.h>

namespace
{
constexpr char MAP_TILE_BIN_PATH[] = "/Map/MapTile.bin";
constexpr char MAGIC[4] = {'M', 'T', 'B', '1'};
constexpr uint8_t MAP_TILE_LAYOUT_SPARSE = 0;
constexpr uint8_t MAP_TILE_LAYOUT_GRID = 1;
constexpr uint8_t MAP_TILE_KIND_LZ4 = 0;
constexpr uint8_t MAP_TILE_KIND_WHITE = 1;
constexpr uint8_t MAP_TILE_KIND_BLACK = 2;

bool s_loaded = false;
uint8_t s_layout = 0, s_gridCols = 0, s_gridRows = 0, s_blockCount = 0;
uint32_t s_count = 0;
uint8_t *s_zooms = nullptr;
uint16_t *s_tx = nullptr;
uint16_t *s_ty = nullptr;
uint8_t *s_blockZooms = nullptr;
uint16_t *s_blockTx = nullptr;
uint16_t *s_blockTy = nullptr;
uint8_t *s_kinds = nullptr;
uint16_t *s_sizes = nullptr;
uint32_t *s_offsets = nullptr;
uint8_t *s_data = nullptr;

bool readExact(File &f, void *buf, size_t len)
{
    return f.read(static_cast<uint8_t *>(buf), len) == len;
}

uint8_t readU8(File &f, bool &ok)
{
    uint8_t v = 0;
    ok = ok && readExact(f, &v, 1);
    return v;
}
uint16_t readU16(File &f, bool &ok)
{
    uint8_t b[2] = {0, 0};
    ok = ok && readExact(f, b, 2);
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}
uint32_t readU32(File &f, bool &ok)
{
    uint8_t b[4] = {0, 0, 0, 0};
    ok = ok && readExact(f, b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

void freeBuffers(uint8_t *zooms, uint16_t *tx, uint16_t *ty, uint8_t *kinds, uint16_t *sizes, uint32_t *offsets,
                 uint8_t *data)
{
    free(zooms);
    free(tx);
    free(ty);
    free(kinds);
    free(sizes);
    free(offsets);
    free(data);
}
} // namespace

namespace NicheGraphics::InkHUD::MapTileSD
{

bool tryLoad()
{
    if (s_loaded)
        return true;

    concurrency::LockGuard g(spiLock);

    File f = SD.open(MAP_TILE_BIN_PATH, FILE_O_READ);
    if (!f) {
        LOG_DEBUG("MapTileSD: no %s", MAP_TILE_BIN_PATH);
        return false;
    }

    char magic[4];
    if (!readExact(f, magic, sizeof(magic)) || memcmp(magic, MAGIC, 4) != 0) {
        LOG_WARN("MapTileSD: bad magic in %s", MAP_TILE_BIN_PATH);
        f.close();
        return false;
    }

    bool ok = true;
    uint8_t layout = readU8(f, ok);
    uint8_t gridCols = readU8(f, ok);
    uint8_t gridRows = readU8(f, ok);
    uint8_t blockCount = readU8(f, ok);
    uint32_t tileCount = readU32(f, ok);
    uint32_t dataSize = readU32(f, ok);
    if (!ok) {
        LOG_WARN("MapTileSD: truncated header in %s", MAP_TILE_BIN_PATH);
        f.close();
        return false;
    }

    constexpr uint32_t kMaxTileCount = 100000;
    if (tileCount == 0 || tileCount > kMaxTileCount) {
        LOG_WARN("MapTileSD: bad tile count in %s", MAP_TILE_BIN_PATH);
        f.close();
        return false;
    }

    const bool grid = layout == MAP_TILE_LAYOUT_GRID;
    uint32_t positionCount = grid ? blockCount : tileCount;
    if ((layout != MAP_TILE_LAYOUT_SPARSE && layout != MAP_TILE_LAYOUT_GRID) ||
        (grid && (gridCols == 0 || gridRows == 0 || blockCount == 0))) {
        LOG_WARN("MapTileSD: bad layout in %s", MAP_TILE_BIN_PATH);
        f.close();
        return false;
    }
    if (grid && tileCount > (uint32_t)gridCols * gridRows * blockCount) {
        LOG_WARN("MapTileSD: grid metadata too small in %s", MAP_TILE_BIN_PATH);
        f.close();
        return false;
    }

    // Reject bogus/oversized headers before allocating anything.
    constexpr uint32_t kSafetyMargin = 300 * 1024;
    const uint32_t metadataSize = positionCount * (sizeof(uint8_t) + (2 * sizeof(uint16_t))) +
                                  tileCount * (sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t));
    const uint32_t freePsram = ESP.getFreePsram();
    if (dataSize > freePsram || freePsram - dataSize < metadataSize + kSafetyMargin) {
        LOG_WARN("MapTileSD: %s needs %u bytes, only %u PSRAM free - skipping", MAP_TILE_BIN_PATH,
                 dataSize + metadataSize, freePsram);
        f.close();
        return false;
    }

    const uint64_t expectedSize = 16ULL + ((uint64_t)positionCount * 5ULL) + ((uint64_t)tileCount * 7ULL) + dataSize;
    if ((uint64_t)f.size() < expectedSize) {
        LOG_WARN("MapTileSD: truncated file %s", MAP_TILE_BIN_PATH);
        f.close();
        return false;
    }

    uint8_t *zooms = static_cast<uint8_t *>(malloc(positionCount * sizeof(uint8_t)));
    uint16_t *tx = static_cast<uint16_t *>(malloc(positionCount * sizeof(uint16_t)));
    uint16_t *ty = static_cast<uint16_t *>(malloc(positionCount * sizeof(uint16_t)));
    uint8_t *kinds = static_cast<uint8_t *>(malloc(tileCount * sizeof(uint8_t)));
    uint16_t *sizes = static_cast<uint16_t *>(malloc(tileCount * sizeof(uint16_t)));
    uint32_t *offsets = static_cast<uint32_t *>(malloc(tileCount * sizeof(uint32_t)));
    uint8_t *data = static_cast<uint8_t *>(malloc(dataSize > 0 ? dataSize : 1));

    if (!zooms || !tx || !ty || !kinds || !sizes || !offsets || !data) {
        LOG_WARN("MapTileSD: out of memory loading %s (need %u bytes for tile data)", MAP_TILE_BIN_PATH, dataSize);
        f.close();
        freeBuffers(zooms, tx, ty, kinds, sizes, offsets, data);
        return false;
    }

    for (uint32_t i = 0; i < positionCount; i++) {
        zooms[i] = readU8(f, ok);
        tx[i] = readU16(f, ok);
        ty[i] = readU16(f, ok);
    }
    for (uint32_t i = 0; i < tileCount; i++) {
        kinds[i] = readU8(f, ok);
        sizes[i] = readU16(f, ok);
        offsets[i] = readU32(f, ok);
        if (kinds[i] == MAP_TILE_KIND_LZ4 && (offsets[i] > dataSize || sizes[i] > dataSize - offsets[i]))
            ok = false;
        if (kinds[i] != MAP_TILE_KIND_LZ4 && kinds[i] != MAP_TILE_KIND_WHITE && kinds[i] != MAP_TILE_KIND_BLACK)
            ok = false;
    }
    if (!ok) {
        LOG_WARN("MapTileSD: invalid metadata in %s", MAP_TILE_BIN_PATH);
        f.close();
        freeBuffers(zooms, tx, ty, kinds, sizes, offsets, data);
        return false;
    }

    if (dataSize > 0 && f.read(data, dataSize) != dataSize) {
        LOG_WARN("MapTileSD: truncated payload blob in %s", MAP_TILE_BIN_PATH);
        f.close();
        freeBuffers(zooms, tx, ty, kinds, sizes, offsets, data);
        return false;
    }

    f.close();

    s_layout = layout;
    s_gridCols = gridCols;
    s_gridRows = gridRows;
    s_blockCount = blockCount;
    s_count = tileCount;
    if (grid) {
        s_blockZooms = zooms;
        s_blockTx = tx;
        s_blockTy = ty;
    } else {
        s_zooms = zooms;
        s_tx = tx;
        s_ty = ty;
    }
    s_kinds = kinds;
    s_sizes = sizes;
    s_offsets = offsets;
    s_data = data;

    LOG_INFO("MapTileSD: loaded %u tiles from %s", tileCount, MAP_TILE_BIN_PATH);
    s_loaded = true;
    return true;
}

bool isLoaded()
{
    return s_loaded;
}

uint8_t layout()
{
    return s_loaded ? s_layout : map_tile_layout;
}
uint8_t gridCols()
{
    return s_loaded ? s_gridCols : map_tile_grid_cols;
}
uint8_t gridRows()
{
    return s_loaded ? s_gridRows : map_tile_grid_rows;
}
uint8_t blockCount()
{
    return s_loaded ? s_blockCount : map_tile_block_count;
}
uint32_t count()
{
    return s_loaded ? s_count : map_tile_count;
}

const uint8_t *zooms()
{
    return s_loaded ? s_zooms : map_tile_zooms;
}
const uint16_t *tx()
{
    return s_loaded ? s_tx : map_tile_tx;
}
const uint16_t *ty()
{
    return s_loaded ? s_ty : map_tile_ty;
}

const uint8_t *blockZooms()
{
    return s_loaded ? s_blockZooms : map_tile_block_zooms;
}
const uint16_t *blockTx()
{
    return s_loaded ? s_blockTx : map_tile_block_tx;
}
const uint16_t *blockTy()
{
    return s_loaded ? s_blockTy : map_tile_block_ty;
}

const uint8_t *kinds()
{
    return s_loaded ? s_kinds : map_tile_kinds;
}
const uint16_t *sizes()
{
    return s_loaded ? s_sizes : map_tile_sizes;
}
const uint32_t *offsets()
{
    return s_loaded ? s_offsets : map_tile_offsets;
}
const uint8_t *data()
{
    return s_loaded ? s_data : map_tile_data;
}

} // namespace NicheGraphics::InkHUD::MapTileSD

#else // !(ARCH_ESP32 && HAS_SDCARD && !SDCARD_USE_SOFT_SPI && !HAS_SD_MMC)

// No SD card on this board - stub straight through to the compiled (always-empty) defaults.
namespace NicheGraphics::InkHUD::MapTileSD
{
bool tryLoad()
{
    return false;
}
bool isLoaded()
{
    return false;
}
uint8_t layout()
{
    return map_tile_layout;
}
uint8_t gridCols()
{
    return map_tile_grid_cols;
}
uint8_t gridRows()
{
    return map_tile_grid_rows;
}
uint8_t blockCount()
{
    return map_tile_block_count;
}
uint32_t count()
{
    return map_tile_count;
}
const uint8_t *zooms()
{
    return map_tile_zooms;
}
const uint16_t *tx()
{
    return map_tile_tx;
}
const uint16_t *ty()
{
    return map_tile_ty;
}
const uint8_t *blockZooms()
{
    return map_tile_block_zooms;
}
const uint16_t *blockTx()
{
    return map_tile_block_tx;
}
const uint16_t *blockTy()
{
    return map_tile_block_ty;
}
const uint8_t *kinds()
{
    return map_tile_kinds;
}
const uint16_t *sizes()
{
    return map_tile_sizes;
}
const uint32_t *offsets()
{
    return map_tile_offsets;
}
const uint8_t *data()
{
    return map_tile_data;
}
} // namespace NicheGraphics::InkHUD::MapTileSD

#endif

#endif // MESHTASTIC_INCLUDE_INKHUD
