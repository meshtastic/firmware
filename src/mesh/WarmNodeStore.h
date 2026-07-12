#pragma once

#include "MeshTypes.h"
#include "mesh-pb-constants.h"
#include <stdint.h>
#include <string.h>

// Verbose tracing for the warm-store migration + NodeDB self-care. Per-event /
// per-boot chatter routes through this so it can be silenced in one place (set
// to 0) once they're proven; genuine LOG_WARN anomalies stay unconditional.
#ifndef MESHTASTIC_NODEDB_MIGRATION_VERBOSE
#define MESHTASTIC_NODEDB_MIGRATION_VERBOSE 0
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
 * evicted from the hot NodeInfoLite store, so DMs to/from them keep encrypting -
 * the key is expensive to re-learn, the rest rebuilds from traffic in seconds.
 * Flat fixed array, linear scan (only on hot-store misses), LRU by last_heard
 * with keyed entries outranking keyless.
 *
 * Persistence: nRF52840 uses a 12 KB raw-flash record-ring below LittleFS
 * (append + replay + compact-on-rotate - see the backend in WarmNodeStore.cpp,
 * link-guarded by nrf52840_s140_v7.ld). Everywhere else: /prefs/warm.dat.
 */
struct WarmNodeEntry {
    NodeNum num;            // 0 = empty slot
    uint32_t last_heard;    // recency for LRU ordering - see the metadata steal below
    uint8_t public_key[32]; // all-zero = no key (a real key is never all-zero)
};
static_assert(sizeof(WarmNodeEntry) == 40, "WarmNodeEntry must stay 40 B - persistence format depends on it");

// Metadata packed into the low bits of last_heard.
//
// The warm tier only uses last_heard to LRU-rank evicted (long-tail) nodes, so ~minute
// recency resolution is plenty. We reclaim the low WARM_META_BITS of that field to carry
// the evicted node's device role + a protected category, at zero cost to record size
// (entry stays 40 B; no RAM/flash growth). The high bits remain a real unix-seconds
// timestamp quantised to (1 << WARM_META_BITS) seconds.
//
// Safe because: a real timestamp can never be all-ones (the tombstone sentinel) before
// 2106, and tombstones/erased flash are detected via num before last_heard is read. Only
// the LOW bits are stolen - the high (era) bits are untouched, so the time range is intact.
static constexpr uint32_t WARM_META_BITS = 6;                          // role(4) + protected(2)
static constexpr uint32_t WARM_META_MASK = (1u << WARM_META_BITS) - 1; // 0x3F → 64 s quantum
static constexpr uint32_t WARM_TIME_MASK = ~WARM_META_MASK;            // 0xFFFFFFC0
static constexpr uint32_t WARM_ROLE_MASK = 0x0Fu;                      // bits [3:0] device role (0..12)
static constexpr uint32_t WARM_PROT_SHIFT = 4;                         // bits [5:4] protected category
static constexpr uint32_t WARM_PROT_MASK = 0x03u;

// Protected category cached alongside role so consumers needn't re-derive the mapping.
enum class WarmProtected : uint8_t { None = 0, Role = 1, Flag = 2 };

inline uint32_t warmPackLastHeard(uint32_t lastHeard, uint8_t role, uint8_t prot)
{
    return (lastHeard & WARM_TIME_MASK) | (static_cast<uint32_t>(role) & WARM_ROLE_MASK) |
           ((static_cast<uint32_t>(prot) & WARM_PROT_MASK) << WARM_PROT_SHIFT);
}
inline uint32_t warmTimeOf(const WarmNodeEntry &e)
{
    return e.last_heard & WARM_TIME_MASK;
}
inline uint8_t warmRoleOf(const WarmNodeEntry &e)
{
    return static_cast<uint8_t>(e.last_heard & WARM_ROLE_MASK);
}
inline uint8_t warmProtOf(const WarmNodeEntry &e)
{
    return static_cast<uint8_t>((e.last_heard >> WARM_PROT_SHIFT) & WARM_PROT_MASK);
}

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
    /// @param role         the node's device role (meshtastic_Config_DeviceConfig_Role, 0..12)
    /// @param protectedCat WarmProtected category cached for the hop-trim path
    /// @return true if the node was stored or updated
    bool absorb(NodeNum num, uint32_t lastHeard, const uint8_t *key32 /* may be NULL */, uint8_t role = 0,
                uint8_t protectedCat = 0);

    /// Look up the cached device role + protected category for a warm node.
    /// @return false if the node is not in the warm tier.
    bool lookupMeta(NodeNum num, uint8_t &role, uint8_t &protectedCat) const;

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
    bool ringReadHeader(uint8_t page, WarmPageHeader &h, bool *legacy = nullptr) const;
#endif

    bool save();
};

#endif // WARM_NODE_COUNT > 0
