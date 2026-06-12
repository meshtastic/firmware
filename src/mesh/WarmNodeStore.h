#pragma once

#include "MeshTypes.h"
#include "mesh-pb-constants.h"
#include <stdint.h>
#include <string.h>

#if WARM_NODE_COUNT > 0

/**
 * Warm ("long-tail") node tier
 *
 * Holds a minimal identity record for nodes evicted from the hot NodeInfoLite
 * store: NodeNum, last_heard and the Curve25519 public key. The tier exists
 * primarily so DMs to/from an evicted node keep encrypting/decrypting — the
 * public key is expensive to re-learn (requires a NodeInfo exchange) while
 * everything else in NodeInfoLite is rebuilt from traffic within seconds.
 *
 * Flat fixed-capacity array, linear scan (lookups happen only on hot-store
 * misses), LRU eviction by last_heard with key-bearing entries outranking
 * keyless ones.
 *
 * Persistence:
 *  - nRF52840: a dedicated 12 KB raw-flash record-ring (3 × 4 KB pages at
 *    0xEA000, directly below the stock LittleFS partition). Mutations append
 *    40 B upsert/tombstone records via the same SoftDevice-safe flash HAL
 *    InternalFS uses; boot replays the pages in sequence order. When the
 *    active page fills, the oldest page is reclaimed: live entries stranded
 *    in it are re-appended first (compaction), then it is erased and becomes
 *    the new head. The app image must end below the region —
 *    nrf52840_s140_v7.ld enforces this at link time and
 *    extra_scripts/nrf52_warm_region.py guards boards on the framework
 *    default linker script.
 *  - everywhere else: /prefs/warm.dat snapshot on the node-DB save cadence.
 */
struct WarmNodeEntry {
    NodeNum num;            // 0 = empty slot
    uint32_t last_heard;    // recency for LRU ordering
    uint8_t public_key[32]; // all-zero = no key (a real key is never all-zero)
};
static_assert(sizeof(WarmNodeEntry) == 40, "WarmNodeEntry must stay 40 B — persistence format depends on it");

// NRF52840_XXAA is explicit: nRF52832 also defines ARCH_NRF52 but only has
// 512 KB flash (top at 0x80000), so 0xEA000 would be past its flash end.
#if defined(ARCH_NRF52) && defined(NRF52840_XXAA)
#define WARM_FLASH_PAGE_SIZE 4096u
#define WARM_FLASH_PAGES 3u
#define WARM_FLASH_REGION_BASE (0xED000u - WARM_FLASH_PAGES * WARM_FLASH_PAGE_SIZE) // 0xEA000
#define WARM_FLASH_PAGE_ADDR(i) (WARM_FLASH_REGION_BASE + (i)*WARM_FLASH_PAGE_SIZE)
#endif

class WarmNodeStore
{
  public:
    WarmNodeStore();
    ~WarmNodeStore();

    /// Remember an evicted hot node. Keyless candidates never displace keyed
    /// entries; otherwise the oldest (keyless-first) entry is replaced.
    /// @return true if the node was stored or updated
    bool absorb(NodeNum num, uint32_t lastHeard, const uint8_t *key32 /* may be NULL */);

    /// Find and remove an entry (used when the node is re-admitted to the hot store).
    bool take(NodeNum num, WarmNodeEntry &out);

    /// Copy the 32-byte public key for a node, if we have one.
    bool copyKey(NodeNum num, uint8_t out[32]) const;

    bool contains(NodeNum num) const;
    void remove(NodeNum num);
    void clear();
    size_t count() const;
    size_t capacity() const { return entries ? WARM_NODE_COUNT : 0; }

    /// Load persisted entries (called once at boot, after the node DB loads).
    void load();
    /// Durability point, piggybacked on the node-database save cadence. On the
    /// ring backend this flushes the shared flash page cache; on the file
    /// backend it writes the warm.dat snapshot.
    bool saveIfDirty();

  private:
    WarmNodeEntry *entries = nullptr; // WARM_NODE_COUNT slots; PSRAM on ESP32 when available
    bool entriesFromPsram = false;
    bool dirty = false;

    WarmNodeEntry *find(NodeNum num) const;
    // Internal slot-placement shared by absorb() and ring replay: applies the
    // keyed-first admission policy without touching persistence.
    WarmNodeEntry *place(NodeNum num, uint32_t lastHeard, const uint8_t *key32);

    // Persistence hooks called from the mutation paths. File backend: mark
    // dirty. Ring backend: append an upsert/tombstone record (+ mark dirty).
    void persistEntry(const WarmNodeEntry &e); // e must point into entries[]
    void persistRemove(NodeNum num, int storeSlot);
    void persistClear();

#if defined(ARCH_NRF52) && defined(NRF52840_XXAA)
    // ---- raw-flash record-ring state ----
    struct WarmPageHeader {
        uint32_t magic; // WARM_RING_MAGIC
        uint32_t seq;   // page generation; 0xFFFFFFFF = erased/unused
    };
    static_assert(sizeof(WarmPageHeader) == 8, "page header is part of the flash format");
    static constexpr uint16_t kRecordsPerPage = (WARM_FLASH_PAGE_SIZE - sizeof(WarmPageHeader)) / sizeof(WarmNodeEntry); // 102
    static_assert(WARM_NODE_COUNT <= 2 * ((WARM_FLASH_PAGE_SIZE - 8) / 40), "live set must fit the ring with one page reclaimed");

    uint8_t activePage = 0xFF;       // 0xFF = no page opened yet (fresh/erased ring)
    uint16_t writeSlot = 0;          // next free record slot in the active page
    uint32_t nextSeq = 1;            // seq for the next page opened
    uint8_t pageOf[WARM_NODE_COUNT]; // flash page holding each RAM slot's newest record; 0xFF = none

    void ringAppend(const WarmNodeEntry &rec, int storeSlot /* -1 for tombstones */);
    void ringRotate();               // reclaim oldest page, compacting stranded live entries
    void ringOpenPage(uint8_t page); // erase + write header (seq = nextSeq++)
    bool ringReadHeader(uint8_t page, WarmPageHeader &h) const;
#endif

    bool save();
};

#endif // WARM_NODE_COUNT > 0
