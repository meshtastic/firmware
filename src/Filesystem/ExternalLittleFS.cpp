#include "Filesystem/ExternalLittleFS.h"

#include "Filesystem/FSCommon.h"
#include "SPILock.h"

namespace
{
Adafruit_SPIFlash *externalFlash = nullptr;

int countUsedBlocks(void *ctx, lfs_block_t block)
{
    (void)block;
    auto usedBlocks = static_cast<uint32_t *>(ctx);
    (*usedBlocks)++;
    return 0;
}

int externalFlashRead(const struct lfs_config *config, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)config;
    if (!externalFlash) {
        return -1;
    }
    const uint32_t address = block * ExternalLittleFS::blockSize + off;
    return (externalFlash->readBuffer(address, static_cast<uint8_t *>(buffer), size) == size) ? 0 : -1;
}

int externalFlashProg(const struct lfs_config *config, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)config;
    if (!externalFlash) {
        return -1;
    }
    const uint32_t address = block * ExternalLittleFS::blockSize + off;
    return (externalFlash->writeBuffer(address, static_cast<uint8_t const *>(buffer), size) == size) ? 0 : -1;
}

int externalFlashErase(const struct lfs_config *config, lfs_block_t block)
{
    (void)config;
    if (!externalFlash) {
        return -1;
    }

    const uint32_t firstSector = block;
    return externalFlash->eraseSector(firstSector) ? 0 : -1;
}

int externalFlashSync(const struct lfs_config *config)
{
    (void)config;
    if (!externalFlash) {
        return -1;
    }
    return externalFlash->syncBlocks() ? 0 : -1;
}

lfs_config externalFsConfig = {
    .context = nullptr,
    .read = externalFlashRead,
    .prog = externalFlashProg,
    .erase = externalFlashErase,
    .sync = externalFlashSync,
    .read_size = 256,
    .prog_size = 256,
    .block_size = ExternalLittleFS::blockSize,
    .block_count = 0,
    .lookahead = 128,
    .read_buffer = nullptr,
    .prog_buffer = nullptr,
    .lookahead_buffer = nullptr,
    .file_buffer = nullptr,
};
} // namespace

ExternalLittleFS externalFS;

ExternalLittleFS::ExternalLittleFS() : Adafruit_LittleFS(&externalFsConfig) {}

bool ExternalLittleFS::prepare(Adafruit_SPIFlash *flashDevice)
{
    if (!flashDevice) {
        return false;
    }

    externalFlash = flashDevice;
    uint32_t flashSizeBytes = flashDevice->size();
    if (flashSizeBytes == 0) {
        const uint32_t jedecId = flashDevice->getJEDECID();
        const uint8_t capacityCode = static_cast<uint8_t>(jedecId & 0xFFu);

        // JEDEC capacity code encodes size as 2^N bytes for common SPI NOR parts.
        // Example: 0x15 => 2^21 => 2 MiB (e.g. W25Q16)
        if (capacityCode >= 0x10 && capacityCode <= 31) {
            const uint64_t derivedSize = (1ULL << capacityCode);
            if (derivedSize <= 0xFFFFFFFFULL) {
                flashSizeBytes = static_cast<uint32_t>(derivedSize);
                LOG_WARN("SPI flash size() returned 0, deriving size from JEDEC 0x%08lX: %lu bytes", jedecId,
                         static_cast<unsigned long>(flashSizeBytes));
            }
        }
    }

    if (flashSizeBytes < blockSize) {
        LOG_ERROR("External flash size invalid (%lu bytes), cannot initialize LittleFS",
                  static_cast<unsigned long>(flashSizeBytes));
        blockCount = 0;
        externalFsConfig.block_count = 0;
        return false;
    }

    blockCount = flashSizeBytes / blockSize;
    externalFsConfig.block_count = blockCount;

    if (blockCount == 0) {
        LOG_ERROR("External LittleFS block count is zero, cannot initialize");
        return false;
    }

    LOG_INFO("External LittleFS geometry: size=%lu bytes, block=%lu, blocks=%lu", static_cast<unsigned long>(flashSizeBytes),
             static_cast<unsigned long>(blockSize), static_cast<unsigned long>(blockCount));

    return true;
}

bool ExternalLittleFS::begin(Adafruit_SPIFlash *flashDevice)
{
    if (!prepare(flashDevice)) {
        return false;
    }

    concurrency::LockGuard g(spiLock);
    if (Adafruit_LittleFS::begin(&externalFsConfig)) {
        return true;
    }

    if (!this->format()) {
        return false;
    }

    return Adafruit_LittleFS::begin(&externalFsConfig);
}

uint32_t ExternalLittleFS::freeClusterCount()
{
    if (blockCount == 0) {
        return 0;
    }

    uint32_t usedBlocks = 0;
    _lockFS();
    int traverseResult = lfs_traverse(_getFS(), countUsedBlocks, &usedBlocks);
    _unlockFS();

    if (traverseResult < 0) {
        return 0;
    }
    if (usedBlocks >= blockCount) {
        return 0;
    }

    return blockCount - usedBlocks;
}
