#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "SDMapTiles.h"

#include "MapTileUtils.h"
#include "configuration.h"

#if defined(INKHUD_USE_SDCARD_MAP_TILES)

#if !defined(HAS_SDCARD) || defined(SDCARD_USE_SOFT_SPI)
#error "INKHUD_USE_SDCARD_MAP_TILES requires hardware SPI SD card support"
#endif

#include "SPILock.h"
#include <Arduino.h>
#include <SD.h>
#include <stdlib.h>
#include <string.h>

namespace NicheGraphics::InkHUD::SDMapTiles
{
namespace
{

constexpr char MAP_PATH[] = "/Map/MapTile.bin";
constexpr uint32_t HEADER_SIZE = 16;
constexpr uint32_t POSITION_SIZE = 5;
constexpr uint32_t PAYLOAD_SIZE = 7;
constexpr size_t MAX_COMPRESSED_TILE_SIZE = MapTileUtils::TILE_BYTES + MapTileUtils::TILE_BYTES / 255 + 16;

class SpiLockGuard
{
  public:
    explicit SpiLockGuard(uint32_t timeoutMs, bool acquire = true)
        : locked(acquire && spiLock->lock(timeoutMs)), usable(!acquire || locked)
    {
    }
    SpiLockGuard(const SpiLockGuard &) = delete;
    SpiLockGuard &operator=(const SpiLockGuard &) = delete;
    ~SpiLockGuard()
    {
        if (locked)
            spiLock->unlock();
    }
    explicit operator bool() const { return usable; }

  private:
    bool locked;
    bool usable;
};

struct Header {
    bool checked = false;
    bool valid = false;
    uint8_t layout = 0;
    uint8_t gridColumns = 0;
    uint8_t gridRows = 0;
    uint8_t blockCount = 0;
    uint32_t tileCount = 0;
    uint32_t dataSize = 0;
    uint32_t payloadOffset = 0;
    uint32_t dataOffset = 0;
    uint32_t zoomMask = 0;
};

Header header;
uint8_t *indexData = nullptr;
uint32_t indexSize = 0;
uint32_t mapFileSize = 0;
uint8_t compressedTile[MAX_COMPRESSED_TILE_SIZE];

uint16_t readLittleEndian16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t readLittleEndian32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

bool parseHeader(const uint8_t *rawHeader, uint32_t fileSize, Header &parsed)
{
    if (fileSize < HEADER_SIZE || memcmp(rawHeader, "MTB1", 4) != 0)
        return false;

    parsed.layout = rawHeader[4];
    parsed.gridColumns = rawHeader[5];
    parsed.gridRows = rawHeader[6];
    parsed.blockCount = rawHeader[7];
    parsed.tileCount = readLittleEndian32(rawHeader + 8);
    parsed.dataSize = readLittleEndian32(rawHeader + 12);
    const uint32_t positionCount = parsed.layout == MapTileUtils::LAYOUT_GRID ? parsed.blockCount : parsed.tileCount;

    const uint64_t payloadOffset = HEADER_SIZE + (uint64_t)positionCount * POSITION_SIZE;
    const uint64_t dataOffset = payloadOffset + (uint64_t)parsed.tileCount * PAYLOAD_SIZE;
    const uint64_t expectedSize = dataOffset + parsed.dataSize;
    const bool layoutValid =
        parsed.layout == MapTileUtils::LAYOUT_SPARSE ||
        (parsed.layout == MapTileUtils::LAYOUT_GRID && parsed.gridColumns && parsed.gridRows && parsed.blockCount &&
         parsed.tileCount == (uint32_t)parsed.gridColumns * parsed.gridRows * parsed.blockCount);
    if (!layoutValid || dataOffset > UINT32_MAX || expectedSize > fileSize)
        return false;

    parsed.payloadOffset = payloadOffset;
    parsed.dataOffset = dataOffset;
    return true;
}

bool readAt(File *file, uint32_t offset, uint8_t *buffer, size_t size)
{
    if (indexData && offset <= indexSize && size <= indexSize - offset) {
        memcpy(buffer, indexData + offset, size);
        return true;
    }
    return file && file->seek(offset) && file->read(buffer, size) == size;
}

void loadHeader()
{
    if (header.checked)
        return;

    header.checked = true;
    SpiLockGuard lock(100, !indexData);
    if (!lock) {
        header.checked = false;
        LOG_WARN("SD map: SPI bus busy, deferring header read");
        return;
    }

    File file;
    File *source = nullptr;
    uint32_t fileSize = mapFileSize;
    if (!indexData) {
        file = SD.open(MAP_PATH, FILE_READ);
        if (!file)
            return;
        source = &file;
        fileSize = file.size();
    }

    uint8_t rawHeader[HEADER_SIZE];
    if (fileSize < sizeof(rawHeader) || !readAt(source, 0, rawHeader, sizeof(rawHeader)) ||
        !parseHeader(rawHeader, fileSize, header)) {
        if (file)
            file.close();
        return;
    }
    const uint32_t positionCount = header.layout == MapTileUtils::LAYOUT_GRID ? header.blockCount : header.tileCount;
    uint8_t position[POSITION_SIZE];
    for (uint32_t i = 0; i < positionCount; ++i) {
        if (!readAt(source, HEADER_SIZE + i * POSITION_SIZE, position, sizeof(position))) {
            if (file)
                file.close();
            return;
        }
        if (position[0] <= 22)
            header.zoomMask |= 1UL << position[0];
    }
    if (file)
        file.close();
    header.valid = header.tileCount > 0 && header.zoomMask != 0;
}

int findTile(File *file, int zoom, int x, int y)
{
    uint8_t position[POSITION_SIZE];
    if (header.layout == MapTileUtils::LAYOUT_SPARSE) {
        for (uint32_t i = 0; i < header.tileCount; ++i) {
            if (!readAt(file, HEADER_SIZE + i * POSITION_SIZE, position, sizeof(position)))
                return -1;
            if (position[0] == zoom && readLittleEndian16(position + 1) == x && readLittleEndian16(position + 3) == y)
                return i;
        }
        return -1;
    }

    const uint32_t tilesPerBlock = (uint32_t)header.gridColumns * header.gridRows;
    for (uint32_t block = 0; block < header.blockCount; ++block) {
        if (!readAt(file, HEADER_SIZE + block * POSITION_SIZE, position, sizeof(position)))
            return -1;
        const int originX = readLittleEndian16(position + 1);
        const int originY = readLittleEndian16(position + 3);
        if (position[0] != zoom || x < originX || x >= originX + header.gridColumns || y < originY ||
            y >= originY + header.gridRows)
            continue;
        return block * tilesPerBlock + (x - originX) * header.gridRows + (y - originY);
    }
    return -1;
}

} // namespace

void preload()
{
    if (header.checked || indexData)
        return;

    SpiLockGuard lock(1000);
    if (!lock) {
        LOG_WARN("SD map: SPI bus busy during preload");
        return;
    }

    File file = SD.open(MAP_PATH, FILE_READ);
    if (!file) {
        LOG_DEBUG("SD map: %s not found during preload", MAP_PATH);
        header.checked = true;
        return;
    }

    const size_t fileSize = file.size();
    uint8_t rawHeader[HEADER_SIZE];
    if (!fileSize || fileSize > UINT32_MAX || file.read(rawHeader, sizeof(rawHeader)) != sizeof(rawHeader)) {
        LOG_WARN("SD map: invalid header or size %lu", (unsigned long)fileSize);
        file.close();
        header.checked = true;
        return;
    }

    Header parsed;
    if (!parseHeader(rawHeader, fileSize, parsed)) {
        LOG_WARN("SD map: invalid tile index");
        file.close();
        header.checked = true;
        return;
    }

#if defined(ARCH_ESP32)
    indexData = static_cast<uint8_t *>(ps_malloc(parsed.dataOffset));
#endif
    if (!indexData) {
        LOG_WARN("SD map: insufficient PSRAM to cache %lu-byte index; using streamed index", (unsigned long)parsed.dataOffset);
        file.close();
        return;
    }

    if (!file.seek(0) || file.read(indexData, parsed.dataOffset) != parsed.dataOffset) {
        LOG_WARN("SD map: index preload failed");
        free(indexData);
        indexData = nullptr;
        file.close();
        return;
    }
    indexSize = parsed.dataOffset;
    mapFileSize = fileSize;
    file.close();
    LOG_INFO("SD map: cached %lu-byte index for %lu-byte map", (unsigned long)parsed.dataOffset, (unsigned long)fileSize);

    loadHeader();
    if (!header.valid) {
        LOG_WARN("SD map: preloaded file is invalid");
        free(indexData);
        indexData = nullptr;
        indexSize = 0;
        mapFileSize = 0;
    }
}

bool hasZoom(int zoom)
{
    loadHeader();
    return header.valid && zoom >= 0 && zoom <= 22 && (header.zoomMask & (1UL << zoom));
}

bool readTile(int zoom, int x, int y, uint8_t *destination, size_t destinationSize)
{
    if (!destination || destinationSize < MapTileUtils::TILE_BYTES || !hasZoom(zoom))
        return false;

    uint8_t payload[PAYLOAD_SIZE];
    if (indexData) {
        const int tileIndex = findTile(nullptr, zoom, x, y);
        if (tileIndex < 0 || !readAt(nullptr, header.payloadOffset + tileIndex * PAYLOAD_SIZE, payload, sizeof(payload)))
            return false;
    } else {
        SpiLockGuard lock(250);
        if (!lock) {
            LOG_WARN("SD map: SPI bus busy, skipping tile z%d/%d/%d", zoom, x, y);
            return false;
        }

        File file = SD.open(MAP_PATH, FILE_READ);
        if (!file)
            return false;
        const int tileIndex = findTile(&file, zoom, x, y);
        const bool readOk =
            tileIndex >= 0 && readAt(&file, header.payloadOffset + tileIndex * PAYLOAD_SIZE, payload, sizeof(payload));
        file.close();
        if (!readOk)
            return false;
    }

    const uint8_t kind = payload[0];
    if (kind == MapTileUtils::KIND_WHITE || kind == MapTileUtils::KIND_BLACK) {
        memset(destination, kind == MapTileUtils::KIND_BLACK ? 0xFF : 0x00, MapTileUtils::TILE_BYTES);
        return true;
    }

    const uint16_t compressedSize = readLittleEndian16(payload + 1);
    const uint32_t dataOffset = readLittleEndian32(payload + 3);
    if (kind != MapTileUtils::KIND_LZ4 || compressedSize == 0 || compressedSize > sizeof(compressedTile) ||
        dataOffset > header.dataSize || compressedSize > header.dataSize - dataOffset)
        return false;

    {
        SpiLockGuard lock(250);
        if (!lock) {
            LOG_WARN("SD map: SPI bus busy, skipping tile data z%d/%d/%d", zoom, x, y);
            return false;
        }

        File file = SD.open(MAP_PATH, FILE_READ);
        if (!file)
            return false;
        const bool readOk = readAt(&file, header.dataOffset + dataOffset, compressedTile, compressedSize);
        file.close();
        if (!readOk)
            return false;
    }

    return MapTileUtils::decompress(compressedTile, compressedSize, destination, MapTileUtils::TILE_BYTES) ==
           MapTileUtils::TILE_BYTES;
}

} // namespace NicheGraphics::InkHUD::SDMapTiles

#else

namespace NicheGraphics::InkHUD::SDMapTiles
{

void preload() {}
bool hasZoom(int zoom)
{
    (void)zoom;
    return false;
}
bool readTile(int zoom, int x, int y, uint8_t *destination, size_t destinationSize)
{
    (void)zoom;
    (void)x;
    (void)y;
    (void)destination;
    (void)destinationSize;
    return false;
}

} // namespace NicheGraphics::InkHUD::SDMapTiles

#endif
#endif
