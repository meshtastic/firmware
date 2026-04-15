#include "FlashSlotStore.h"

#include <ErriezCRC32.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "mesh-pb-constants.h"

namespace
{

constexpr const char *MANIFEST_FILENAME = "manifest.bin";
constexpr size_t MAX_PATH_LEN = 96;

bool buildManifestPath(const char *directory, char *path, size_t pathSize)
{
    const int written = snprintf(path, pathSize, "%s/%s", directory, MANIFEST_FILENAME);
    return written > 0 && static_cast<size_t>(written) < pathSize;
}

bool buildPagePath(const char *directory, uint16_t pageIndex, char *path, size_t pathSize)
{
    const int written = snprintf(path, pathSize, "%s/page-%04u.bin", directory, static_cast<unsigned>(pageIndex));
    return written > 0 && static_cast<size_t>(written) < pathSize;
}

bool pageExists(const char *directory, uint16_t pageIndex)
{
#ifdef FSCom
    char path[MAX_PATH_LEN] = {};
    if (!buildPagePath(directory, pageIndex, path, sizeof(path))) {
        LOG_ERROR("FlashSlotStore page path overflow");
        return false;
    }

    concurrency::LockGuard g(spiLock);
    return FSCom.exists(path);
#else
    (void)directory;
    (void)pageIndex;
    return false;
#endif
}

bool isValidManifest(const FlashSlotStore::Manifest &manifest)
{
    return manifest.magic == FlashSlotStore::MANIFEST_MAGIC && manifest.version == FlashSlotStore::FORMAT_VERSION &&
           manifest.record_size == sizeof(FlashSlotStore::Record) && manifest.slots_per_page != 0;
}

bool isPresentRecord(const FlashSlotStore::RecordHeader &header)
{
    return header.magic == FlashSlotStore::RECORD_MAGIC && header.version == FlashSlotStore::FORMAT_VERSION &&
           (header.flags & FlashSlotStore::RECORD_FLAG_PRESENT) != 0;
}

size_t pageSizeBytes(const FlashSlotStore::Manifest &manifest)
{
    return static_cast<size_t>(manifest.record_size) * manifest.slots_per_page;
}

uint16_t pageCount(const FlashSlotStore::Manifest &manifest)
{
    return static_cast<uint16_t>((manifest.slot_count + manifest.slots_per_page - 1) / manifest.slots_per_page);
}

uint16_t pageIndexForSlot(const FlashSlotStore::Manifest &manifest, uint16_t slotIndex)
{
    return slotIndex / manifest.slots_per_page;
}

size_t slotOffsetInPage(const FlashSlotStore::Manifest &manifest, uint16_t slotIndex)
{
    return static_cast<size_t>(slotIndex % manifest.slots_per_page) * manifest.record_size;
}

bool ensureDirectoryExists(const char *directory)
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    FSCom.mkdir("/prefs");
    FSCom.mkdir(directory);
    return true;
#else
    (void)directory;
    return false;
#endif
}

bool loadPage(const char *directory, const FlashSlotStore::Manifest &manifest, uint16_t pageIndex, std::vector<uint8_t> &page)
{
    page.assign(pageSizeBytes(manifest), 0);

#ifdef FSCom
    char path[MAX_PATH_LEN] = {};
    if (!buildPagePath(directory, pageIndex, path, sizeof(path))) {
        LOG_ERROR("FlashSlotStore page path overflow");
        return false;
    }

    concurrency::LockGuard g(spiLock);
    if (!FSCom.exists(path)) {
        return true;
    }

    File file = FSCom.open(path, FILE_O_READ);
    if (!file) {
        LOG_ERROR("FlashSlotStore failed to open page %s", path);
        return false;
    }

    if (static_cast<size_t>(file.size()) != page.size()) {
        LOG_WARN("FlashSlotStore page %s size mismatch (%u != %u)", path, static_cast<unsigned>(file.size()),
                 static_cast<unsigned>(page.size()));
        file.close();
        return false;
    }

    const int bytesRead = file.read(page.data(), page.size());
    file.close();
    if (bytesRead != static_cast<int>(page.size())) {
        LOG_ERROR("FlashSlotStore short read on %s", path);
        return false;
    }

    return true;
#else
    (void)directory;
    (void)pageIndex;
    return false;
#endif
}

bool deletePage(const char *directory, uint16_t pageIndex)
{
#ifdef FSCom
    char path[MAX_PATH_LEN] = {};
    if (!buildPagePath(directory, pageIndex, path, sizeof(path))) {
        LOG_ERROR("FlashSlotStore page path overflow");
        return false;
    }

    concurrency::LockGuard g(spiLock);
    return !FSCom.exists(path) || FSCom.remove(path);
#else
    (void)directory;
    (void)pageIndex;
    return false;
#endif
}

bool storePage(const char *directory, const FlashSlotStore::Manifest &manifest, uint16_t pageIndex,
               const std::vector<uint8_t> &page)
{
#ifdef FSCom
    if (page.size() != pageSizeBytes(manifest)) {
        LOG_ERROR("FlashSlotStore page write size mismatch");
        return false;
    }

    char path[MAX_PATH_LEN] = {};
    if (!buildPagePath(directory, pageIndex, path, sizeof(path))) {
        LOG_ERROR("FlashSlotStore page path overflow");
        return false;
    }

    SafeFile file(path, false);
    if (file.write(page.data(), page.size()) != page.size()) {
        LOG_ERROR("FlashSlotStore failed to write page %s", path);
        return false;
    }

    if (!file.close()) {
        LOG_ERROR("FlashSlotStore failed to close page %s", path);
        return false;
    }

    return true;
#else
    (void)directory;
    (void)manifest;
    (void)pageIndex;
    (void)page;
    return false;
#endif
}

bool patchRecordInPage(const FlashSlotStore::Manifest &manifest, uint16_t slotIndex, const FlashSlotStore::Record &record,
                       std::vector<uint8_t> &page)
{
    if (slotIndex >= manifest.slot_count) {
        LOG_ERROR("FlashSlotStore slot index %u out of range", static_cast<unsigned>(slotIndex));
        return false;
    }

    if (page.size() != pageSizeBytes(manifest)) {
        LOG_ERROR("FlashSlotStore page buffer size mismatch");
        return false;
    }

    const size_t recordOffset = slotOffsetInPage(manifest, slotIndex);
    if ((recordOffset + sizeof(record)) > page.size()) {
        LOG_ERROR("FlashSlotStore slot offset overflow");
        return false;
    }

    memcpy(page.data() + recordOffset, &record, sizeof(record));
    return true;
}

bool encodeNodeRecord(uint16_t slotIndex, const meshtastic_NodeInfoLite &node, FlashSlotStore::Record &record)
{
    record = {};
    const size_t encodedLen = pb_encode_to_bytes(record.payload, sizeof(record.payload), meshtastic_NodeInfoLite_fields, &node);
    if (encodedLen == 0 || encodedLen > sizeof(record.payload)) {
        LOG_ERROR("FlashSlotStore failed to encode slot %u", static_cast<unsigned>(slotIndex));
        return false;
    }

    record.header.flags = FlashSlotStore::RECORD_FLAG_PRESENT;
    record.header.encoded_len = encodedLen;
    record.header.crc32 = crc32Buffer(record.payload, encodedLen);
    return true;
}

} // namespace

FlashSlotStore::FlashSlotStore(const char *directory) : directory(directory) {}

bool FlashSlotStore::initialize(uint16_t slotCount, uint16_t slotsPerPage) const
{
    Manifest manifest;
    manifest.slot_count = slotCount;
    manifest.slots_per_page = slotsPerPage == 0 ? DEFAULT_SLOTS_PER_PAGE : slotsPerPage;
    return writeManifest(manifest);
}

bool FlashSlotStore::readManifest(Manifest &manifest) const
{
    manifest = Manifest();

#ifdef FSCom
    char path[MAX_PATH_LEN] = {};
    if (!buildManifestPath(directory, path, sizeof(path))) {
        LOG_ERROR("FlashSlotStore manifest path overflow");
        return false;
    }

    concurrency::LockGuard g(spiLock);
    File file = FSCom.open(path, FILE_O_READ);
    if (!file) {
        return false;
    }

    if (static_cast<size_t>(file.size()) != sizeof(manifest)) {
        LOG_WARN("FlashSlotStore manifest size mismatch (%u != %u)", static_cast<unsigned>(file.size()),
                 static_cast<unsigned>(sizeof(manifest)));
        file.close();
        return false;
    }

    const int bytesRead = file.read(reinterpret_cast<uint8_t *>(&manifest), sizeof(manifest));
    file.close();
    if (bytesRead != static_cast<int>(sizeof(manifest))) {
        LOG_ERROR("FlashSlotStore short manifest read");
        return false;
    }

    if (!isValidManifest(manifest)) {
        LOG_WARN("FlashSlotStore manifest validation failed");
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool FlashSlotStore::writeManifest(const Manifest &manifest) const
{
#ifdef FSCom
    if (!isValidManifest(manifest)) {
        LOG_ERROR("FlashSlotStore refusing to write invalid manifest");
        return false;
    }

    if (!ensureDirectoryExists(directory)) {
        LOG_ERROR("FlashSlotStore failed to ensure directory");
        return false;
    }

    char path[MAX_PATH_LEN] = {};
    if (!buildManifestPath(directory, path, sizeof(path))) {
        LOG_ERROR("FlashSlotStore manifest path overflow");
        return false;
    }

    SafeFile file(path, false);
    if (file.write(reinterpret_cast<const uint8_t *>(&manifest), sizeof(manifest)) != sizeof(manifest)) {
        LOG_ERROR("FlashSlotStore failed to write manifest");
        return false;
    }

    if (!file.close()) {
        LOG_ERROR("FlashSlotStore failed to close manifest");
        return false;
    }

    return true;
#else
    (void)manifest;
    return false;
#endif
}

bool FlashSlotStore::readSlot(uint16_t slotIndex, meshtastic_NodeInfoLite &node) const
{
    Manifest manifest;
    return readManifest(manifest) && readSlot(manifest, slotIndex, node);
}

bool FlashSlotStore::readSlot(const Manifest &manifest, uint16_t slotIndex, meshtastic_NodeInfoLite &node) const
{
    const meshtastic_NodeInfoLite emptyNode = meshtastic_NodeInfoLite_init_default;
    node = emptyNode;

    if (!isValidManifest(manifest) || slotIndex >= manifest.slot_count) {
        return false;
    }

    std::vector<uint8_t> page;
    if (!loadPage(manifest, pageIndexForSlot(manifest, slotIndex), page)) {
        return false;
    }

    const size_t recordOffset = slotOffsetInPage(manifest, slotIndex);
    if ((recordOffset + sizeof(Record)) > page.size()) {
        LOG_ERROR("FlashSlotStore slot offset overflow");
        return false;
    }

    Record record = {};
    memcpy(&record, page.data() + recordOffset, sizeof(record));
    if (!isPresentRecord(record.header)) {
        return false;
    }

    if (record.header.encoded_len == 0 || record.header.encoded_len > sizeof(record.payload)) {
        LOG_WARN("FlashSlotStore slot %u encoded_len invalid", static_cast<unsigned>(slotIndex));
        return false;
    }

    if (crc32Buffer(record.payload, record.header.encoded_len) != record.header.crc32) {
        LOG_WARN("FlashSlotStore slot %u crc mismatch", static_cast<unsigned>(slotIndex));
        return false;
    }

    return pb_decode_from_bytes(record.payload, record.header.encoded_len, meshtastic_NodeInfoLite_fields, &node);
}

bool FlashSlotStore::writeSlot(uint16_t slotIndex, const meshtastic_NodeInfoLite &node) const
{
    Manifest manifest;
    return readManifest(manifest) && writeSlot(manifest, slotIndex, node);
}

bool FlashSlotStore::writeSlot(const Manifest &manifest, uint16_t slotIndex, const meshtastic_NodeInfoLite &node) const
{
    if (!isValidManifest(manifest) || slotIndex >= manifest.slot_count) {
        return false;
    }

    std::vector<uint8_t> page;
    const uint16_t pageIndex = pageIndexForSlot(manifest, slotIndex);
    if (!loadPage(manifest, pageIndex, page)) {
        return false;
    }

    if (!writeSlotToPage(manifest, slotIndex, node, page)) {
        return false;
    }

    return storePage(manifest, pageIndex, page);
}

bool FlashSlotStore::clearSlot(uint16_t slotIndex) const
{
    Manifest manifest;
    return readManifest(manifest) && clearSlot(manifest, slotIndex);
}

bool FlashSlotStore::clearSlot(const Manifest &manifest, uint16_t slotIndex) const
{
    if (!isValidManifest(manifest) || slotIndex >= manifest.slot_count) {
        return false;
    }

    const uint16_t pageIndex = pageIndexForSlot(manifest, slotIndex);
    if (!pageExists(directory, pageIndex)) {
        return true;
    }

    std::vector<uint8_t> page;
    if (!loadPage(manifest, pageIndex, page)) {
        return false;
    }

    if (!clearSlotInPage(manifest, slotIndex, page)) {
        return false;
    }

    return storePage(manifest, pageIndex, page);
}

bool FlashSlotStore::loadPage(const Manifest &manifest, uint16_t pageIndex, std::vector<uint8_t> &page) const
{
    if (!isValidManifest(manifest) || pageIndex >= pageCount(manifest)) {
        return false;
    }

    return ::loadPage(directory, manifest, pageIndex, page);
}

bool FlashSlotStore::storePage(const Manifest &manifest, uint16_t pageIndex, const std::vector<uint8_t> &page) const
{
    if (!isValidManifest(manifest) || pageIndex >= pageCount(manifest)) {
        return false;
    }

    if (!ensureDirectoryExists(directory)) {
        LOG_ERROR("FlashSlotStore failed to ensure directory");
        return false;
    }

    return ::storePage(directory, manifest, pageIndex, page);
}

bool FlashSlotStore::deletePage(const Manifest &manifest, uint16_t pageIndex) const
{
    if (!isValidManifest(manifest) || pageIndex >= pageCount(manifest)) {
        return false;
    }

    return ::deletePage(directory, pageIndex);
}

bool FlashSlotStore::writeSlotToPage(const Manifest &manifest, uint16_t slotIndex, const meshtastic_NodeInfoLite &node,
                                     std::vector<uint8_t> &page) const
{
    if (!isValidManifest(manifest) || slotIndex >= manifest.slot_count) {
        return false;
    }

    Record record = {};
    return encodeNodeRecord(slotIndex, node, record) && patchRecordInPage(manifest, slotIndex, record, page);
}

bool FlashSlotStore::clearSlotInPage(const Manifest &manifest, uint16_t slotIndex, std::vector<uint8_t> &page) const
{
    if (!isValidManifest(manifest) || slotIndex >= manifest.slot_count) {
        return false;
    }

    const Record clearedRecord = {};
    return patchRecordInPage(manifest, slotIndex, clearedRecord, page);
}
