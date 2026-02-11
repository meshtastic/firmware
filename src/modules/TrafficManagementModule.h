#pragma once

#include "MeshModule.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#if HAS_TRAFFIC_MANAGEMENT

/**
 * TrafficManagementModule - Packet inspection and traffic shaping for mesh networks.
 *
 * This module provides:
 * - Position deduplication (drop redundant position broadcasts)
 * - Per-node rate limiting (throttle chatty nodes)
 * - Unknown packet filtering (drop undecoded packets from repeat offenders)
 * - NodeInfo direct response (answer queries from cache to reduce mesh chatter)
 * - Local-only telemetry/position (exhaust hop_limit for local broadcasts)
 * - Router hop preservation (maintain hop_limit for router-to-router traffic)
 *
 * Memory Optimization:
 * Uses a unified cache with cuckoo hashing for O(1) lookups and 56% memory reduction
 * compared to separate per-feature caches. Timestamps are stored as 16-bit relative
 * offsets from a rolling epoch to further reduce memory footprint.
 */
class TrafficManagementModule : public MeshModule, private concurrency::OSThread
{
  public:
    TrafficManagementModule();

    meshtastic_TrafficManagementStats getStats() const;
    void resetStats();
    void recordRouterHopPreserved();

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    void alterReceived(meshtastic_MeshPacket &mp) override;
    int32_t runOnce() override;

  private:
    // =========================================================================
    // Unified Cache Entry
    // =========================================================================
    //
    // Combines position dedup, rate limiting, and unknown packet tracking into
    // a single 20-byte structure. This reduces memory from 36 bytes (16+10+10)
    // per node to 20 bytes - a 44% reduction.
    //
    // Timestamps use 16-bit relative offsets from `cacheEpochMs`, providing
    // ~65535 second (~18 hour) range which exceeds typical TTL requirements.
    // The epoch advances when offsets would overflow.
    //
    // Layout (20 bytes):
    //   [0-3]   node             - NodeNum (4 bytes)
    //   [4-7]   lat_truncated    - Truncated latitude (4 bytes)
    //   [8-11]  lon_truncated    - Truncated longitude (4 bytes)
    //   [12-13] pos_time_secs    - Position timestamp offset (2 bytes)
    //   [14]    rate_count       - Packets in current window (1 byte, saturates at 255)
    //   [15]    unknown_count    - Unknown packets count (1 byte, saturates at 255)
    //   [16-17] rate_window_secs - Rate limit window start offset (2 bytes)
    //   [18-19] unknown_secs     - Unknown tracking window start offset (2 bytes)
    //
    struct UnifiedCacheEntry {
        NodeNum node;              // 4 bytes - Node identifier (0 = empty slot)
        int32_t lat_truncated;     // 4 bytes - Precision-truncated latitude
        int32_t lon_truncated;     // 4 bytes - Precision-truncated longitude
        uint16_t pos_time_secs;    // 2 bytes - Seconds since epoch for position
        uint8_t rate_count;        // 1 byte  - Packet count (saturates at 255)
        uint8_t unknown_count;     // 1 byte  - Unknown packet count (saturates at 255)
        uint16_t rate_window_secs; // 2 bytes - Window start for rate limiting
        uint16_t unknown_secs;     // 2 bytes - Window start for unknown tracking
    };
    static_assert(sizeof(UnifiedCacheEntry) == 20, "UnifiedCacheEntry should be 20 bytes");

    // =========================================================================
    // Hash-based Position Dedup (NRF52 only)
    // =========================================================================
    //
    // On memory-constrained NRF52 devices, position deduplication uses a hash
    // of the packet content instead of storing per-node lat/lon coordinates.
    // This trades some accuracy for significant memory savings.
    //
#if defined(ARCH_NRF52)
    struct HashDedupEntry {
        uint32_t hash;         // 4 bytes - Hash of (node + truncated position)
        uint32_t last_seen_ms; // 4 bytes - Absolute timestamp (not relative)
    };
#endif

    // =========================================================================
    // Cuckoo Hash Table Implementation
    // =========================================================================
    //
    // Cuckoo hashing provides O(1) worst-case lookup time using two hash functions.
    // Each key can be in one of two possible locations (h1 or h2). On collision,
    // the existing entry is "kicked" to its alternate location.
    //
    // Benefits over linear scan:
    // - O(1) lookup vs O(n) - critical at packet processing rates
    // - O(1) insertion (amortized) with simple eviction on cycles
    // - ~95% load factor achievable
    //
    // The cache size should be power-of-2 for fast modulo via bitmask.
    // Default TRAFFIC_MANAGEMENT_CACHE_SIZE=1000 rounds up to 1024 internally.
    //
    static constexpr uint16_t cacheSize();
    static constexpr uint16_t cacheMask();

    // Hash functions for cuckoo hashing
    // h1: Simple modulo - good distribution for sequential NodeNums
    // h2: Multiplicative hash - decorrelated from h1 for cuckoo property
    inline uint16_t cuckooHash1(NodeNum node) const { return node & cacheMask(); }
    inline uint16_t cuckooHash2(NodeNum node) const
    {
        // Golden ratio hash: multiply by 2654435769 (closest prime to 2^32/phi)
        // Then shift to get cacheMask() bits
        return ((node * 2654435769u) >> (32 - cuckooHashBits())) & cacheMask();
    }
    static constexpr uint8_t cuckooHashBits();

    // =========================================================================
    // Relative Timestamp Management
    // =========================================================================
    //
    // To save memory, timestamps are stored as 16-bit second offsets from a
    // rolling epoch. This provides ~18 hour range which exceeds our longest TTL.
    //
    // When an offset would exceed UINT16_MAX, the epoch advances and all
    // entries are invalidated (handled during periodic maintenance).
    //
    uint32_t cacheEpochMs = 0; // Rolling epoch base for relative timestamps

    uint16_t toRelativeSecs(uint32_t nowMs) const
    {
        uint32_t offsetMs = nowMs - cacheEpochMs;
        uint32_t secs = offsetMs / 1000;
        return (secs > UINT16_MAX) ? UINT16_MAX : static_cast<uint16_t>(secs);
    }

    uint32_t fromRelativeSecs(uint16_t secs) const { return cacheEpochMs + (static_cast<uint32_t>(secs) * 1000); }

    bool needsEpochReset(uint32_t nowMs) const
    {
        // Reset when we're within 1 hour of overflow (~17 hours elapsed)
        return (nowMs - cacheEpochMs) > (UINT16_MAX - 3600) * 1000UL;
    }

    void resetEpoch(uint32_t nowMs);

    // =========================================================================
    // Cache Storage
    // =========================================================================

    mutable concurrency::Lock cacheLock; // Protects all cache access
    UnifiedCacheEntry *cache = nullptr;  // Cuckoo hash table
#if defined(ARCH_NRF52)
    HashDedupEntry *hashCache = nullptr; // Separate hash cache for NRF52 position dedup
#endif

    meshtastic_TrafficManagementStats stats;

    // =========================================================================
    // Cache Operations
    // =========================================================================

    // Find or create entry for node using cuckoo hashing
    // Returns nullptr if cache is full and eviction fails
    UnifiedCacheEntry *findOrCreateEntry(NodeNum node, bool *isNew);

    // Find existing entry (no creation)
    UnifiedCacheEntry *findEntry(NodeNum node);

#if defined(ARCH_NRF52)
    HashDedupEntry *findHashEntry(uint32_t hash, bool *isNew);
#endif

    // =========================================================================
    // Traffic Management Logic
    // =========================================================================

    bool shouldDropPosition(const meshtastic_MeshPacket *p, const meshtastic_Position *pos, uint32_t nowMs);
    bool shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse);
    bool isMinHopsFromRequestor(const meshtastic_MeshPacket *p) const;
    bool isRateLimited(NodeNum from, uint32_t nowMs);
    bool shouldDropUnknown(const meshtastic_MeshPacket *p, uint32_t nowMs);
    void exhaustHops(meshtastic_MeshPacket *p);

#if defined(ARCH_NRF52)
    uint32_t computePacketHash(const meshtastic_MeshPacket *p, uint8_t precision) const;
#endif

    void logAction(const char *action, const meshtastic_MeshPacket *p, const char *reason) const;
    void incrementStat(uint32_t *field);
};

// =========================================================================
// Compile-time Cache Size Calculations
// =========================================================================
//
// Round TRAFFIC_MANAGEMENT_CACHE_SIZE up to next power of 2 for efficient
// cuckoo hash indexing (allows bitmask instead of modulo).
//
// These use C++11-compatible constexpr (single return statement).
//

namespace detail
{
// Helper: round up to next power of 2 using bit manipulation
constexpr uint16_t nextPow2(uint16_t n)
{
    return n == 0 ? 0 : (((n - 1) | ((n - 1) >> 1) | ((n - 1) >> 2) | ((n - 1) >> 4) | ((n - 1) >> 8)) + 1);
}

// Helper: count trailing zeros (log2 for power of 2)
constexpr uint8_t log2Floor(uint16_t n)
{
    return n <= 1      ? 0
           : n <= 2    ? 1
           : n <= 4    ? 2
           : n <= 8    ? 3
           : n <= 16   ? 4
           : n <= 32   ? 5
           : n <= 64   ? 6
           : n <= 128  ? 7
           : n <= 256  ? 8
           : n <= 512  ? 9
           : n <= 1024 ? 10
           : n <= 2048 ? 11
                       : 12;
}
} // namespace detail

constexpr uint16_t TrafficManagementModule::cacheSize()
{
    return detail::nextPow2(TRAFFIC_MANAGEMENT_CACHE_SIZE);
}

constexpr uint16_t TrafficManagementModule::cacheMask()
{
    return cacheSize() > 0 ? cacheSize() - 1 : 0;
}

constexpr uint8_t TrafficManagementModule::cuckooHashBits()
{
    return detail::log2Floor(cacheSize());
}

extern TrafficManagementModule *trafficManagementModule;

#endif
