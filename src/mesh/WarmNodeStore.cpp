#include "WarmNodeStore.h"

#if WARM_NODE_COUNT > 0

#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "configuration.h"
#include "power/PowerHAL.h"
#include <ErriezCRC32.h>
#include <vector>

#if defined(ARCH_NRF52) && defined(NRF52840_XXAA)
#include "flash/flash_nrf5x.h"
#define WARM_RING_MAGIC 0x474E5257u // "WRNG"
// A tombstone is an entry record whose last_heard is all-ones — getTime()
// (unix seconds) cannot reach 0xFFFFFFFF until 2106, and erased flash is
// detected via num == 0xFFFFFFFF before last_heard is ever inspected.
#define WARM_RING_TOMBSTONE 0xFFFFFFFFu
#else
// warm.dat layout: this header followed by count packed WarmNodeEntry records.
struct WarmStoreHeader {
    uint32_t magic;     // WARM_STORE_MAGIC
    uint32_t reserved;  // 0; kept so the header stays 16 B
    uint16_t count;     // entries persisted
    uint16_t entrySize; // sizeof(WarmNodeEntry), format guard
    uint32_t crc;       // crc32 over count * entrySize bytes
};
static_assert(sizeof(WarmStoreHeader) == 16, "header layout is part of the persistence format");

#define WARM_STORE_MAGIC 0x314D5257u // "WRM1"

#ifdef FSCom
static const char *warmFileName = "/prefs/warm.dat";
#endif
#endif // defined(ARCH_NRF52) && defined(NRF52840_XXAA)

static inline bool keyIsSet(const uint8_t key[32])
{
    for (int i = 0; i < 32; i++)
        if (key[i])
            return true;
    return false;
}

WarmNodeStore::WarmNodeStore()
{
#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    entries = static_cast<WarmNodeEntry *>(ps_calloc(WARM_NODE_COUNT, sizeof(WarmNodeEntry)));
    if (!entries) {
        LOG_WARN("WarmStore: PSRAM allocation failed, falling back to heap");
        entries = static_cast<WarmNodeEntry *>(calloc(WARM_NODE_COUNT, sizeof(WarmNodeEntry)));
    }
#else
    entries = static_cast<WarmNodeEntry *>(calloc(WARM_NODE_COUNT, sizeof(WarmNodeEntry)));
#endif
#if defined(ARCH_NRF52) && defined(NRF52840_XXAA)
    memset(pageOf, 0xFF, sizeof(pageOf));
#endif
}

WarmNodeStore::~WarmNodeStore()
{
    free(entries); // always malloc-family (calloc / ps_calloc)
    entries = nullptr;
}

WarmNodeEntry *WarmNodeStore::find(NodeNum num) const
{
    if (!entries || !num)
        return nullptr;
    for (size_t i = 0; i < WARM_NODE_COUNT; i++)
        if (entries[i].num == num)
            return &entries[i];
    return nullptr;
}

// Slot placement with the keyed-first admission policy. Shared by absorb()
// and the ring replay, so the policy is applied identically in both paths.
WarmNodeEntry *WarmNodeStore::place(NodeNum num, uint32_t lastHeard, const uint8_t *key32)
{
    if (!entries || !num)
        return nullptr;

    const bool candidateKeyed = key32 && keyIsSet(key32);

    WarmNodeEntry *slot = find(num);
    const bool sameNode = slot != nullptr;
    if (!slot) {
        // Pick a victim: any empty slot, else the oldest keyless entry, else
        // (only for keyed candidates) the oldest keyed entry.
        WarmNodeEntry *oldestKeyless = nullptr, *oldestKeyed = nullptr;
        for (size_t i = 0; i < WARM_NODE_COUNT; i++) {
            WarmNodeEntry &e = entries[i];
            if (!e.num) {
                slot = &e;
                break;
            }
            if (keyIsSet(e.public_key)) {
                if (!oldestKeyed || e.last_heard < oldestKeyed->last_heard)
                    oldestKeyed = &e;
            } else {
                if (!oldestKeyless || e.last_heard < oldestKeyless->last_heard)
                    oldestKeyless = &e;
            }
        }
        if (!slot)
            slot = oldestKeyless ? oldestKeyless : (candidateKeyed ? oldestKeyed : nullptr);
        if (!slot)
            return nullptr; // store full of keyed entries and the candidate has no key
    }

    slot->num = num;
    slot->last_heard = lastHeard;
    if (candidateKeyed)
        memcpy(slot->public_key, key32, 32);
    else if (!sameNode)
        // Repurposing a victim slot for a different node: clear its stale key.
        // A keyless refresh of a node already here keeps the key we learned.
        memset(slot->public_key, 0, 32);
    return slot;
}

bool WarmNodeStore::absorb(NodeNum num, uint32_t lastHeard, const uint8_t *key32)
{
    const WarmNodeEntry *slot = place(num, lastHeard, key32);
    if (!slot)
        return false;
    persistEntry(*slot);
    return true;
}

bool WarmNodeStore::take(NodeNum num, WarmNodeEntry &out)
{
    WarmNodeEntry *e = find(num);
    if (!e)
        return false;
    out = *e;
    const int idx = static_cast<int>(e - entries);
    memset(e, 0, sizeof(*e));
    persistRemove(num, idx);
    return true;
}

bool WarmNodeStore::copyKey(NodeNum num, uint8_t out[32]) const
{
    const WarmNodeEntry *e = find(num);
    if (!e || !keyIsSet(e->public_key))
        return false;
    memcpy(out, e->public_key, 32);
    return true;
}

bool WarmNodeStore::contains(NodeNum num) const
{
    return find(num) != nullptr;
}

void WarmNodeStore::remove(NodeNum num)
{
    WarmNodeEntry *e = find(num);
    if (e) {
        const int idx = static_cast<int>(e - entries);
        memset(e, 0, sizeof(*e));
        persistRemove(num, idx);
    }
}

void WarmNodeStore::clear()
{
    if (!entries)
        return;
    memset(entries, 0, WARM_NODE_COUNT * sizeof(WarmNodeEntry));
#if defined(ARCH_NRF52) && defined(NRF52840_XXAA)
    memset(pageOf, 0xFF, sizeof(pageOf));
#endif
    persistClear();
}

size_t WarmNodeStore::count() const
{
    size_t n = 0;
    if (entries)
        for (size_t i = 0; i < WARM_NODE_COUNT; i++)
            if (entries[i].num)
                n++;
    return n;
}

bool WarmNodeStore::saveIfDirty()
{
    if (!dirty)
        return true;
    bool ok = save();
    if (ok)
        dirty = false;
    return ok;
}

#if defined(ARCH_NRF52) && defined(NRF52840_XXAA)

// ---- Raw-flash record-ring backend (nRF52840 only; nRF52832 uses file backend) ---
// 3 × 4 KB pages directly below LittleFS. Mutations append 40 B records (an
// entry snapshot, or a tombstone with last_heard == 0xFFFFFFFF) through the
// shared flash_nrf5x page cache; saveIfDirty() is the durability point. When
// the active page fills, the oldest page is reclaimed: live entries whose
// newest record is stranded there get re-appended first, then the page is
// erased and becomes the new head. All flash access happens under spiLock
// because the page cache is shared with InternalFS/LittleFS.

bool WarmNodeStore::ringReadHeader(uint8_t page, WarmPageHeader &h) const
{
    flash_nrf5x_read(&h, WARM_FLASH_PAGE_ADDR(page), sizeof(h));
    return h.magic == WARM_RING_MAGIC && h.seq != 0xFFFFFFFFu;
}

// Erase `page` and stamp a fresh header. Caller holds spiLock.
void WarmNodeStore::ringOpenPage(uint8_t page)
{
    // Drop any cached state for the page before the real erase, so a later
    // cache flush can't resurrect stale bytes.
    flash_nrf5x_flush();
    flash_nrf5x_erase(WARM_FLASH_PAGE_ADDR(page));
    WarmPageHeader h;
    h.magic = WARM_RING_MAGIC;
    h.seq = nextSeq++;
    flash_nrf5x_write(WARM_FLASH_PAGE_ADDR(page), &h, sizeof(h));
    activePage = page;
    writeSlot = 0;
}

// Reclaim the oldest (or an unused) page and compact stranded live entries
// into it. Caller holds spiLock. May recurse once via ringAppend when the
// stranded set fills the fresh page exactly — bounded by the page count and
// the WARM_NODE_COUNT <= 2 * kRecordsPerPage static_assert.
void WarmNodeStore::ringRotate()
{
    uint8_t target = 0;
    if (activePage != 0xFF) {
        // Lowest-seq valid page, preferring erased pages; never the active one
        uint32_t bestSeq = 0;
        bool found = false;
        for (uint8_t p = 0; p < WARM_FLASH_PAGES; p++) {
            if (p == activePage)
                continue;
            WarmPageHeader h;
            if (!ringReadHeader(p, h)) {
                target = p; // erased/invalid page: free real estate, take it
                found = true;
                break;
            }
            if (!found || static_cast<int32_t>(h.seq - bestSeq) < 0) {
                target = p;
                bestSeq = h.seq;
                found = true;
            }
        }
    }

    // Capture live entries stranded in the page we're about to erase
    int stranded[WARM_NODE_COUNT] = {};
    int nStranded = 0;
    for (size_t i = 0; i < WARM_NODE_COUNT; i++) {
        if (entries[i].num && pageOf[i] == target)
            stranded[nStranded++] = static_cast<int>(i);
        if (pageOf[i] == target)
            pageOf[i] = 0xFF;
    }

    ringOpenPage(target);

    for (int k = 0; k < nStranded; k++)
        ringAppend(entries[stranded[k]], stranded[k]);
}

// Append one record. Caller holds spiLock.
void WarmNodeStore::ringAppend(const WarmNodeEntry &rec, int storeSlot)
{
    if (activePage == 0xFF || writeSlot >= kRecordsPerPage)
        ringRotate();
    const uint32_t addr =
        WARM_FLASH_PAGE_ADDR(activePage) + sizeof(WarmPageHeader) + static_cast<uint32_t>(writeSlot) * sizeof(WarmNodeEntry);
    flash_nrf5x_write(addr, &rec, sizeof(rec));
    writeSlot++;
    if (storeSlot >= 0)
        pageOf[storeSlot] = activePage;
    dirty = true;
}

void WarmNodeStore::persistEntry(const WarmNodeEntry &e)
{
    concurrency::LockGuard g(spiLock);
    ringAppend(e, static_cast<int>(&e - entries));
}

void WarmNodeStore::persistRemove(NodeNum num, int storeSlot)
{
    if (storeSlot >= 0 && storeSlot < static_cast<int>(WARM_NODE_COUNT))
        pageOf[storeSlot] = 0xFF;
    WarmNodeEntry tomb;
    memset(&tomb, 0, sizeof(tomb));
    tomb.num = num;
    tomb.last_heard = WARM_RING_TOMBSTONE;
    concurrency::LockGuard g(spiLock);
    ringAppend(tomb, -1);
}

void WarmNodeStore::persistClear()
{
    concurrency::LockGuard g(spiLock);
    flash_nrf5x_flush();
    for (uint8_t p = 0; p < WARM_FLASH_PAGES; p++)
        flash_nrf5x_erase(WARM_FLASH_PAGE_ADDR(p));
    activePage = 0xFF;
    writeSlot = 0;
    nextSeq = 1;
    dirty = false; // the erased ring already reflects the empty store
}

void WarmNodeStore::load()
{
    if (!entries)
        return;
    concurrency::LockGuard g(spiLock);

    // Order valid pages by ascending seq so replay applies oldest first
    uint8_t order[WARM_FLASH_PAGES] = {};
    uint32_t seqs[WARM_FLASH_PAGES] = {};
    uint8_t nValid = 0;
    uint8_t nCorrupt = 0;
    for (uint8_t p = 0; p < WARM_FLASH_PAGES; p++) {
        WarmPageHeader h;
        if (!ringReadHeader(p, h)) {
            // An erased page reads back all-ones; any other magic is a
            // partially-written or bit-rotted header we're dropping, so flag it
            // rather than silently treating the loss as a clean empty ring.
            if (h.magic != 0xFFFFFFFFu)
                nCorrupt++;
            continue;
        }
        uint8_t pos = nValid;
        while (pos > 0 && static_cast<int32_t>(h.seq - seqs[pos - 1]) < 0) {
            order[pos] = order[pos - 1];
            seqs[pos] = seqs[pos - 1];
            pos--;
        }
        order[pos] = p;
        seqs[pos] = h.seq;
        nValid++;
    }

    if (nValid == 0) {
        activePage = 0xFF;
        writeSlot = 0;
        nextSeq = 1;
        if (nCorrupt)
            LOG_WARN("WarmStore: ring unreadable (%u corrupt page(s)), starting empty", nCorrupt);
        else
            LOG_INFO("WarmStore: ring empty, starting fresh");
        return;
    }

    uint32_t replayed = 0;
    for (uint8_t k = 0; k < nValid; k++) {
        const uint8_t p = order[k];
        uint16_t slot = 0;
        for (; slot < kRecordsPerPage; slot++) {
            WarmNodeEntry rec;
            flash_nrf5x_read(&rec, WARM_FLASH_PAGE_ADDR(p) + sizeof(WarmPageHeader) + (uint32_t)slot * sizeof(rec), sizeof(rec));
            if (rec.num == 0xFFFFFFFFu)
                break; // erased space: end of this page's records (append-only)
            if (rec.num == 0)
                continue; // unexpected; skip defensively
            replayed++;
            if (rec.last_heard == WARM_RING_TOMBSTONE) {
                WarmNodeEntry *e = find(rec.num);
                if (e) {
                    pageOf[e - entries] = 0xFF;
                    memset(e, 0, sizeof(*e));
                }
            } else {
                const WarmNodeEntry *e = place(rec.num, rec.last_heard, rec.public_key);
                if (e)
                    pageOf[e - entries] = p;
            }
        }
        if (k == nValid - 1) { // newest page becomes the active head
            activePage = p;
            writeSlot = slot;
            nextSeq = seqs[k] + 1;
        }
    }
    if (nCorrupt)
        LOG_WARN("WarmStore: dropped %u corrupt ring page(s) during replay; some warm nodes may be lost", nCorrupt);
    LOG_INFO("WarmStore: replayed %u ring records -> %u live nodes (page %u, slot %u)", (unsigned)replayed, (unsigned)count(),
             activePage, writeSlot);
}

bool WarmNodeStore::save()
{
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to save WarmStore on unsafe device power level.");
        return false;
    }
    concurrency::LockGuard g(spiLock);
    flash_nrf5x_flush();
    return true;
}

#else // !(defined(ARCH_NRF52) && defined(NRF52840_XXAA)) --------------------

void WarmNodeStore::persistEntry(const WarmNodeEntry &e)
{
    (void)e;
    dirty = true;
}

void WarmNodeStore::persistRemove(NodeNum num, int storeSlot)
{
    (void)num;
    (void)storeSlot;
    dirty = true;
}

void WarmNodeStore::persistClear()
{
    dirty = true;
}

#ifdef FSCom

// ---- File persistence: /prefs/warm.dat snapshots ----------------------------

// Compact occupied slots to the front of `dst`; returns the count.
static uint16_t packEntries(const WarmNodeEntry *src, WarmNodeEntry *dst)
{
    uint16_t n = 0;
    for (size_t i = 0; i < WARM_NODE_COUNT; i++)
        if (src[i].num)
            dst[n++] = src[i];
    return n;
}

void WarmNodeStore::load()
{
    if (!entries)
        return;
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(warmFileName, FILE_O_READ);
    if (!f)
        return;
    WarmStoreHeader h;
    bool ok = (size_t)f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == WARM_STORE_MAGIC &&
              h.entrySize == sizeof(WarmNodeEntry) && h.count <= WARM_NODE_COUNT;
    if (ok && h.count) {
        const size_t len = (size_t)h.count * sizeof(WarmNodeEntry);
        ok = (size_t)f.read((uint8_t *)entries, len) == len && crc32Buffer(entries, len) == h.crc;
        if (!ok)
            memset(entries, 0, WARM_NODE_COUNT * sizeof(WarmNodeEntry));
    }
    f.close();
    if (ok)
        LOG_INFO("WarmStore: loaded %u warm nodes from %s", h.count, warmFileName);
    else
        LOG_WARN("WarmStore: %s invalid, starting empty", warmFileName);
}

bool WarmNodeStore::save()
{
    if (!entries)
        return false;
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to save WarmStore on unsafe device power level.");
        return false;
    }

    std::vector<WarmNodeEntry> packed(WARM_NODE_COUNT);
    WarmStoreHeader h;
    h.magic = WARM_STORE_MAGIC;
    h.reserved = 0;
    h.count = packEntries(entries, packed.data());
    h.entrySize = sizeof(WarmNodeEntry);
    h.crc = crc32Buffer(packed.data(), h.count * sizeof(WarmNodeEntry));

    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();

    auto f = SafeFile(warmFileName, false);
    f.write((const uint8_t *)&h, sizeof(h));
    f.write((const uint8_t *)packed.data(), h.count * sizeof(WarmNodeEntry));
    bool ok = f.close();
    if (!ok)
        LOG_ERROR("WarmStore: can't write %s", warmFileName);
    else
        LOG_DEBUG("WarmStore: saved %u warm nodes to %s", h.count, warmFileName);
    return ok;
}

#else

void WarmNodeStore::load() {}
bool WarmNodeStore::save()
{
    return true;
}

#endif // FSCom
#endif // defined(ARCH_NRF52) && defined(NRF52840_XXAA)

#endif // WARM_NODE_COUNT > 0
