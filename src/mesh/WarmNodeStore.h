#pragma once

#include "MeshTypes.h"
#include "mesh-pb-constants.h"
#include <stdint.h>
#include <string.h>

// Verbose tracing for the warm-store migration + NodeDB self-care. Per-event /
// per-boot chatter routes through this so it can be silenced in one place (set
// to 0) once they're proven; genuine LOG_WARN anomalies stay unconditional.
#ifndef MESHTASTIC_NODEDB_MIGRATION_VERBOSE
#define MESHTASTIC_NODEDB_MIGRATION_VERBOSE 1
#endif
#if MESHTASTIC_NODEDB_MIGRATION_VERBOSE
#define LOG_MIGRATION(...) LOG_INFO(__VA_ARGS__)
#else
#define LOG_MIGRATION(...) ((void)0)
#endif

#if WARM_NODE_COUNT > 0

/**
 * Warm ("long-tail") node tier.
 *
 * Minimal identity record (NodeNum, last_heard, Curve25519 public key) for nodes
 * evicted from the hot NodeInfoLite store, so DMs to/from them keep encrypting —
 * the key is expensive to re-learn, the rest rebuilds from traffic in seconds.
 * Flat fixed array, linear scan (only on hot-store misses), LRU by last_heard
 * with keyed entries outranking keyless.
 *
 * Persistence: nRF52840 uses a 12 KB raw-flash record-ring below LittleFS
 * (append + replay + compact-on-rotate — see the backend in WarmNodeStore.cpp,
 * link-guarded by nrf52840_s140_v7.ld). Everywhere else: /prefs/warm.dat.
 */
struct WarmNodeEntry {
    NodeNum num;            // 0 = empty slot
    uint32_t last_heard;    // recency for LRU ordering
    uint8_t public_key[32]; // all-zero = no key (a real key is never all-zero)
};
static_assert(sizeof(WarmNodeEntry) == 40, "WarmNodeEntry must stay 40 B — persistence format depends on it");

// Gated on NRF52840_XXAA: the ring sits at 0xEA000
// valid only on the 1 MB-flash nRF52840.
#if defined(NRF52840_XXAA)
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
    WarmNodeStore(const WarmNodeStore &) = delete;
    WarmNodeStore &operator=(const WarmNodeStore &) = delete;

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

#if MESHTASTIC_NODEDB_MIGRATION_VERBOSE
    /// Debug: dump every live warm entry (num / last_heard / has-key) to the
    /// console. Compiled out unless MESHTASTIC_NODEDB_MIGRATION_VERBOSE.
    void dumpToLog(const char *reason = "dump") const;
#endif

    /// Load persisted entries (called once at boot, after the node DB loads).
    void load();
    /// Durability point, piggybacked on the node-database save cadence. On the
    /// ring backend this flushes the shared flash page cache; on the file
    /// backend it writes the warm.dat snapshot.
    bool saveIfDirty();

  private:
    WarmNodeEntry *entries = nullptr; // WARM_NODE_COUNT slots; PSRAM on ESP32 when available
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

#if defined(NRF52840_XXAA)
    // nRF52840 raw-flash record-ring state.
    struct WarmPageHeader {
        uint32_t magic; // WARM_RING_MAGIC
        uint32_t seq;   // page generation; 0xFFFFFFFF = erased/unused
    };
    static_assert(sizeof(WarmPageHeader) == 8, "page header is part of the flash format");
    static constexpr uint16_t kRecordsPerPage = (WARM_FLASH_PAGE_SIZE - sizeof(WarmPageHeader)) / sizeof(WarmNodeEntry); // 102
    static_assert(WARM_NODE_COUNT <= 2 * ((WARM_FLASH_PAGE_SIZE - 8) / 40), "live set must fit the ring with one page reclaimed");

    static constexpr uint8_t kNoPage = 0xFF; // "no page" sentinel for activePage / pageOf[]

    uint8_t activePage = kNoPage;    // no page opened yet (fresh/erased ring)
    uint16_t writeSlot = 0;          // next free record slot in the active page
    uint32_t nextSeq = 1;            // seq for the next page opened
    uint8_t pageOf[WARM_NODE_COUNT]; // flash page holding each RAM slot's newest record; kNoPage = none

    void ringAppend(const WarmNodeEntry &rec, int storeSlot /* -1 for tombstones */);
    void ringRotate();               // reclaim oldest page, compacting stranded live entries
    void ringOpenPage(uint8_t page); // erase + write header (seq = nextSeq++)
    bool ringReadHeader(uint8_t page, WarmPageHeader &h) const;
#endif

    bool save();
};

#endif // WARM_NODE_COUNT > 0
