#include "WarmNodeStore.h"

#if WARM_NODE_COUNT > 0

#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "configuration.h"
#include "power/PowerHAL.h"
#include <ErriezCRC32.h>
#include <vector>

#if defined(NRF52840_XXAA)
#include "flash/flash_nrf5x.h"
#define WARM_RING_MAGIC 0x324E5257u    // "WRN2" — v2: last_heard low bits carry role + protected category
#define WARM_RING_MAGIC_V1 0x474E5257u // "WRNG" — v1: last_heard was a plain timestamp.
// v1 pages are still read on upgrade: we keep each record's identity + public key but
// DISCARD its last_heard (the old timestamp would be misread as role/protected bits).
// Records re-rank and re-learn their role on the next contact. Legacy pages convert to
// v2 naturally as the ring rotates.
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

#define WARM_STORE_MAGIC 0x324D5257u // "WRM2" — v2: last_heard low bits carry role + protected category
#define WARM_STORE_MAGIC_V1                                                                                                      \
    0x314D5257u // "WRM1" — v1: last_heard was a plain timestamp. On upgrade we keep
                // identity + key but discard last_heard, then rewrite as v2.

#ifdef FSCom
static const char *warmFileName = "/prefs/warm.dat";
#endif
#endif // NRF52840_XXAA

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
        LOG_WARN("WarmStore: PSRAM alloc failed, using heap");
        entries = static_cast<WarmNodeEntry *>(calloc(WARM_NODE_COUNT, sizeof(WarmNodeEntry)));
    }
#else
    entries = static_cast<WarmNodeEntry *>(calloc(WARM_NODE_COUNT, sizeof(WarmNodeEntry)));
#endif
#if defined(NRF52840_XXAA)
    memset(pageOf, kNoPage, sizeof(pageOf));
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
            // Compare on the time bits only — the low metadata bits (role/protected) must
            // not perturb LRU victim selection.
            if (keyIsSet(e.public_key)) {
                if (!oldestKeyed || warmTimeOf(e) < warmTimeOf(*oldestKeyed))
                    oldestKeyed = &e;
            } else {
                if (!oldestKeyless || warmTimeOf(e) < warmTimeOf(*oldestKeyless))
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

bool WarmNodeStore::absorb(NodeNum num, uint32_t lastHeard, const uint8_t *key32, uint8_t role, uint8_t protectedCat)
{
    // Pack role + protected category into the low bits of last_heard. place() and ring
    // replay store the raw word verbatim, so the metadata round-trips through flash.
    const uint32_t packed = warmPackLastHeard(lastHeard, role, protectedCat);
    const WarmNodeEntry *slot = place(num, packed, key32);
    if (!slot)
        return false;
    persistEntry(*slot);
    LOG_MIGRATION("WarmStore absorb 0x%08x key=%d last_heard=%u role=%u prot=%u (now %u/%u)", (unsigned)num,
                  keyIsSet(slot->public_key) ? 1 : 0, (unsigned)warmTimeOf(*slot), (unsigned)role, (unsigned)protectedCat,
                  (unsigned)count(), (unsigned)capacity());
    return true;
}

bool WarmNodeStore::lookupMeta(NodeNum num, uint8_t &role, uint8_t &protectedCat) const
{
    const WarmNodeEntry *e = find(num);
    if (!e)
        return false;
    role = warmRoleOf(*e);
    protectedCat = warmProtOf(*e);
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
    LOG_MIGRATION("WarmStore take(rehydrate) 0x%08x key=%d (now %u/%u)", (unsigned)num, keyIsSet(out.public_key) ? 1 : 0,
                  (unsigned)count(), (unsigned)capacity());
    return true;
}

#if MESHTASTIC_NODEDB_MIGRATION_VERBOSE
void WarmNodeStore::dumpToLog(const char *reason) const
{
    if (!entries) {
        LOG_MIGRATION("WarmStore dump (%s): backend not allocated", reason);
        return;
    }
    LOG_MIGRATION("WarmStore dump (%s): %u live / %u cap ==>", reason, (unsigned)count(), (unsigned)capacity());
    unsigned shown = 0;
    for (size_t i = 0; i < WARM_NODE_COUNT; i++) {
        const WarmNodeEntry &e = entries[i];
        if (e.num == 0)
            continue;
        LOG_MIGRATION("  warm[%3u] 0x%08x last_heard=%u key=%d", (unsigned)i, (unsigned)e.num, (unsigned)e.last_heard,
                      keyIsSet(e.public_key) ? 1 : 0);
        shown++;
    }
    LOG_MIGRATION("WarmStore dump (%s): <== end (%u entries)", reason, shown);
}
#endif // MESHTASTIC_NODEDB_MIGRATION_VERBOSE

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
#if defined(NRF52840_XXAA)
    memset(pageOf, kNoPage, sizeof(pageOf));
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

#if defined(NRF52840_XXAA)

// Raw-flash record-ring backend (nRF52840).
// 3 × 4 KB pages below LittleFS. Mutations append 40 B records (entry snapshot,
// or tombstone with last_heard == 0xFFFFFFFF) via the shared flash_nrf5x page
// cache; saveIfDirty() is the durability point. A full page reclaims the oldest
// (stranded live entries re-appended, then erased). Flash access holds spiLock —
// the page cache is shared with InternalFS/LittleFS.

bool WarmNodeStore::ringReadHeader(uint8_t page, WarmPageHeader &h, bool *legacy) const
{
    flash_nrf5x_read(&h, WARM_FLASH_PAGE_ADDR(page), sizeof(h));
    if (h.seq == 0xFFFFFFFFu)
        return false; // erased page
    if (h.magic == WARM_RING_MAGIC) {
        if (legacy)
            *legacy = false;
        return true;
    }
    if (h.magic == WARM_RING_MAGIC_V1) {
        if (legacy)
            *legacy = true; // v1 page: replay it, but discard last_heard (see WARM_RING_MAGIC_V1)
        return true;
    }
    return false;
}

// Caller holds spiLock.
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

// Caller holds spiLock. May recurse once via ringAppend if the stranded set
// fills the fresh page exactly — bounded by WARM_NODE_COUNT <= 2*kRecordsPerPage.
void WarmNodeStore::ringRotate()
{
    uint8_t target = 0;
    if (activePage != kNoPage) {
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
            pageOf[i] = kNoPage;
    }

    ringOpenPage(target);

    for (int k = 0; k < nStranded; k++)
        ringAppend(entries[stranded[k]], stranded[k]);
}

// Caller holds spiLock.
void WarmNodeStore::ringAppend(const WarmNodeEntry &rec, int storeSlot)
{
    if (activePage == kNoPage || writeSlot >= kRecordsPerPage)
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
    bool legacyOf[WARM_FLASH_PAGES] = {}; // per-page: v1 (WRNG) → discard last_heard on replay
    uint8_t nValid = 0;
    uint8_t nCorrupt = 0;
    for (uint8_t p = 0; p < WARM_FLASH_PAGES; p++) {
        WarmPageHeader h;
        bool legacy = false;
        if (!ringReadHeader(p, h, &legacy)) {
            // An erased page reads back all-ones; any other magic is a
            // partially-written or bit-rotted header we're dropping, so flag it
            // rather than silently treating the loss as a clean empty ring.
            if (h.magic != 0xFFFFFFFFu)
                nCorrupt++;
            continue;
        }
        legacyOf[p] = legacy;
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
            LOG_WARN("WarmStore: ring unreadable (%u bad page(s)), empty", nCorrupt);
        else
            LOG_INFO("WarmStore: ring empty, starting fresh");
        return;
    }

    uint32_t replayed = 0;
    uint32_t migrated = 0;
    for (uint8_t k = 0; k < nValid; k++) {
        const uint8_t p = order[k];
        const bool legacy = legacyOf[p];
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
                // v1 (legacy) record: keep identity + key, but discard the old timestamp —
                // its low bits would otherwise be misread as role/protected metadata.
                uint32_t lh = rec.last_heard;
                if (legacy) {
                    lh = 0;
                    migrated++;
                }
                const WarmNodeEntry *e = place(rec.num, lh, rec.public_key);
                if (e)
                    pageOf[e - entries] = p;
            }
        }
        if (k == nValid - 1) { // newest page becomes the active head
            activePage = p;
            writeSlot = slot;
            nextSeq = seqs[k] + 1;
            // If the head is a v1 page, force the next append to rotate into a fresh v2 page,
            // so new (v2) records never land in a page whose header says v1 (which would make
            // a later load discard their last_heard — including the role/protected we just set).
            if (legacy)
                writeSlot = kRecordsPerPage;
        }
    }
    if (nCorrupt)
        LOG_WARN("WarmStore: dropped %u corrupt ring page(s), some nodes lost", nCorrupt);
    if (migrated)
        LOG_INFO("WarmStore: migrated %u v1 record(s) (kept key, discarded last_heard)", (unsigned)migrated);
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

#else // !NRF52840_XXAA --------------------

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
    // Clear first — all failure paths below then correctly represent "empty",
    // even if load() is called on an already-used instance.
    memset(entries, 0, WARM_NODE_COUNT * sizeof(WarmNodeEntry));
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(warmFileName, FILE_O_READ);
    if (!f)
        return;
    WarmStoreHeader h;
    if ((size_t)f.read((uint8_t *)&h, sizeof(h)) != sizeof(h)) {
        f.close();
        LOG_WARN("WarmStore: %s header read failed, starting empty", warmFileName);
        return;
    }
    // v1 (WRM1) is still accepted: same record size, but its last_heard was a plain
    // timestamp. We keep identity + key and discard last_heard on load (see below).
    const bool legacy = (h.magic == WARM_STORE_MAGIC_V1);
    if ((h.magic != WARM_STORE_MAGIC && !legacy) || h.entrySize != sizeof(WarmNodeEntry) || h.count > WARM_NODE_COUNT) {
        f.close();
        LOG_WARN("WarmStore: %s header invalid (magic=0x%08x entrySize=%u count=%u), starting empty", warmFileName, h.magic,
                 h.entrySize, h.count);
        return;
    }
    if (h.count) {
        const size_t len = (size_t)h.count * sizeof(WarmNodeEntry);
        const bool readOk = (size_t)f.read((uint8_t *)entries, len) == len;
        f.close();
        if (!readOk) {
            LOG_WARN("WarmStore: %s entries read failed, starting empty", warmFileName);
            return;
        }
        // CRC covers the bytes as written (v1 still has the old last_heard), so check before migrating.
        if (crc32Buffer(entries, len) != h.crc) {
            LOG_WARN("WarmStore: %s CRC mismatch, starting empty", warmFileName);
            memset(entries, 0, WARM_NODE_COUNT * sizeof(WarmNodeEntry));
            return;
        }
        if (legacy) {
            // Migrate v1 → v2: discard the old last_heard (its low bits would be misread as
            // role/protected); keep num + public_key. Mark dirty so save() rewrites as v2.
            for (size_t i = 0; i < WARM_NODE_COUNT; i++)
                if (entries[i].num)
                    entries[i].last_heard = 0;
            dirty = true;
        }
    } else {
        f.close();
    }
    LOG_INFO("WarmStore: loaded %u warm nodes from %s%s", h.count, warmFileName,
             legacy ? " (v1 migrated: discarded last_heard)" : "");
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

    concurrency::LockGuard g(spiLock);
    FSCom.mkdir("/prefs");

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
#endif // NRF52840_XXAA

#endif // WARM_NODE_COUNT > 0
