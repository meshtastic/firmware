#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "SDMapTiles.h"

#include "MapTileUtils.h"
#include "Throttle.h"
#include "UptimeClock.h"
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

#ifndef INKHUD_SD_MAP_INDEX_CACHE_PERCENT
#define INKHUD_SD_MAP_INDEX_CACHE_PERCENT 10U
#endif

namespace NicheGraphics::InkHUD::SDMapTiles
{
namespace
{

constexpr char MAP_PATH[] = "/Map/MapTile.bin";
constexpr uint32_t HEADER_SIZE = 16;
constexpr uint32_t POSITION_SIZE = 5;
constexpr uint32_t PAYLOAD_SIZE = 7;
constexpr uint32_t HEADER_RETRY_MS = 2000;
constexpr uint32_t STREAMED_INDEX_ENTRIES_PER_BLOCK = 64;
constexpr size_t MAX_COMPRESSED_TILE_SIZE = MapTileUtils::TILE_BYTES + MapTileUtils::TILE_BYTES / 255 + 16;
static_assert(INKHUD_SD_MAP_INDEX_CACHE_PERCENT <= 50, "SD map index cache percentage cannot exceed 50");

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
uint32_t headerRetryAt = 0;
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

bool readCached(uint32_t offset, uint8_t *buffer, size_t size)
{
    if (indexData && offset <= indexSize && size <= indexSize - offset) {
        memcpy(buffer, indexData + offset, size);
        return true;
    }
    return false;
}

bool readSdBlock(uint32_t offset, uint8_t *buffer, size_t size, uint32_t *fileSize = nullptr)
{
    SpiLockGuard lock(250);
    if (!lock)
        return false;

    File file = SD.open(MAP_PATH, FILE_READ);
    if (!file)
        return false;
    const size_t currentFileSize = file.size();
    if (currentFileSize > UINT32_MAX) {
        file.close();
        return false;
    }
    if (fileSize)
        *fileSize = currentFileSize;
    const bool readOk =
        offset <= currentFileSize && size <= currentFileSize - offset && file.seek(offset) && file.read(buffer, size) == size;
    file.close();
    return readOk;
}

void deferHeaderRetry(const char *reason)
{
    headerRetryAt = Time::timerEndsAtMillis(HEADER_RETRY_MS);
    LOG_WARN("SD map: %s; retrying header", reason);
}

bool readPositionBlock(uint32_t firstEntry, uint32_t entryCount, uint8_t *buffer)
{
    const uint32_t offset = HEADER_SIZE + firstEntry * POSITION_SIZE;
    const size_t size = entryCount * POSITION_SIZE;
    if (readCached(offset, buffer, size))
        return true;
    const bool readOk = readSdBlock(offset, buffer, size);
    yield();
    return readOk;
}

bool scanZoomMask(Header &parsed)
{
    const uint32_t positionCount = parsed.layout == MapTileUtils::LAYOUT_GRID ? parsed.blockCount : parsed.tileCount;
    uint8_t positions[STREAMED_INDEX_ENTRIES_PER_BLOCK * POSITION_SIZE];
    for (uint32_t first = 0; first < positionCount; first += STREAMED_INDEX_ENTRIES_PER_BLOCK) {
        const uint32_t count = min(STREAMED_INDEX_ENTRIES_PER_BLOCK, positionCount - first);
        if (!readPositionBlock(first, count, positions))
            return false;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t zoom = positions[i * POSITION_SIZE];
            if (zoom <= 22)
                parsed.zoomMask |= 1UL << zoom;
        }
    }
    return true;
}

int findTile(int zoom, int x, int y)
{
    const uint32_t positionCount = header.layout == MapTileUtils::LAYOUT_GRID ? header.blockCount : header.tileCount;
    const uint32_t tilesPerBlock = (uint32_t)header.gridColumns * header.gridRows;
    uint8_t positions[STREAMED_INDEX_ENTRIES_PER_BLOCK * POSITION_SIZE];
    for (uint32_t first = 0; first < positionCount; first += STREAMED_INDEX_ENTRIES_PER_BLOCK) {
        const uint32_t count = min(STREAMED_INDEX_ENTRIES_PER_BLOCK, positionCount - first);
        if (!readPositionBlock(first, count, positions)) {
            LOG_WARN("SD map: index block read failed at entry %lu", (unsigned long)first);
            return -1;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *position = positions + i * POSITION_SIZE;
            if (header.layout == MapTileUtils::LAYOUT_SPARSE) {
                if (position[0] == zoom && readLittleEndian16(position + 1) == x && readLittleEndian16(position + 3) == y)
                    return first + i;
                continue;
            }

            const int originX = readLittleEndian16(position + 1);
            const int originY = readLittleEndian16(position + 3);
            if (position[0] == zoom && x >= originX && x < originX + header.gridColumns && y >= originY &&
                y < originY + header.gridRows)
                return (first + i) * tilesPerBlock + (x - originX) * header.gridRows + (y - originY);
        }
    }
    return -1;
}

} // namespace

void preload()
{
    if (header.checked || indexData)
        return;
    if (headerRetryAt && !Throttle::deadlinePassed(headerRetryAt))
        return;
    headerRetryAt = 0;

    uint8_t rawHeader[HEADER_SIZE];
    uint32_t fileSize = 0;
    if (!readSdBlock(0, rawHeader, sizeof(rawHeader), &fileSize)) {
        deferHeaderRetry("header read failed");
        return;
    }

    Header parsed;
    if (!parseHeader(rawHeader, fileSize, parsed)) {
        LOG_WARN("SD map: invalid tile index");
        parsed.checked = true;
        header = parsed;
        return;
    }

    uint8_t *candidateIndex = nullptr;
    uint32_t freePsram = 0;
    uint32_t totalPsram = 0;
    uint32_t cacheLimit = 0;
#if defined(ARCH_ESP32)
    freePsram = ESP.getFreePsram();
    totalPsram = ESP.getPsramSize();
    cacheLimit = ((uint64_t)freePsram * INKHUD_SD_MAP_INDEX_CACHE_PERCENT) / 100U;
    if (parsed.dataOffset <= cacheLimit)
        candidateIndex = static_cast<uint8_t *>(ps_malloc(parsed.dataOffset));
#endif

    if (candidateIndex) {
        constexpr size_t CACHE_READ_BLOCK_SIZE = 4096;
        bool cacheReadOk = true;
        for (uint32_t offset = 0; offset < parsed.dataOffset; offset += CACHE_READ_BLOCK_SIZE) {
            const size_t size = min((uint32_t)CACHE_READ_BLOCK_SIZE, parsed.dataOffset - offset);
            if (!readSdBlock(offset, candidateIndex + offset, size)) {
                cacheReadOk = false;
                break;
            }
            yield();
        }
        if (!cacheReadOk) {
            free(candidateIndex);
            deferHeaderRetry("index preload failed");
            return;
        }
        indexData = candidateIndex;
        indexSize = parsed.dataOffset;
        mapFileSize = fileSize;
        LOG_INFO("SD map: cached %lu-byte index for %lu-byte map (limit=%lu, PSRAM=%lu/%lu free)", (unsigned long)indexSize,
                 (unsigned long)mapFileSize, (unsigned long)cacheLimit, (unsigned long)(freePsram - indexSize),
                 (unsigned long)totalPsram);
    } else {
        LOG_INFO("SD map: streaming %lu-byte index (limit=%lu, PSRAM=%lu/%lu free)", (unsigned long)parsed.dataOffset,
                 (unsigned long)cacheLimit, (unsigned long)freePsram, (unsigned long)totalPsram);
    }

    if (!scanZoomMask(parsed)) {
        free(indexData);
        indexData = nullptr;
        indexSize = 0;
        mapFileSize = 0;
        deferHeaderRetry("position index read failed");
        return;
    }
    parsed.valid = parsed.tileCount > 0 && parsed.zoomMask != 0;
    parsed.checked = true;
    header = parsed;
}

bool hasZoom(int zoom)
{
    preload();
    return header.valid && zoom >= 0 && zoom <= 22 && (header.zoomMask & (1UL << zoom));
}

bool readTile(int zoom, int x, int y, uint8_t *destination, size_t destinationSize)
{
    if (!destination || destinationSize < MapTileUtils::TILE_BYTES || !hasZoom(zoom))
        return false;

    const int tileIndex = findTile(zoom, x, y);
    if (tileIndex < 0)
        return false;

    uint8_t payload[PAYLOAD_SIZE];
    const uint32_t payloadOffset = header.payloadOffset + tileIndex * PAYLOAD_SIZE;
    if (!readCached(payloadOffset, payload, sizeof(payload)) && !readSdBlock(payloadOffset, payload, sizeof(payload)))
        return false;

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

    if (!readSdBlock(header.dataOffset + dataOffset, compressedTile, compressedSize)) {
        LOG_WARN("SD map: tile data read failed z%d/%d/%d", zoom, x, y);
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
