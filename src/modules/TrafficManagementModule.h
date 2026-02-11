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

    /**
     * Check if the current packet should have its hops exhausted.
     * Called from perhapsRebroadcast() to force hop_limit = 0 regardless of
     * router_preserve_hops or favorite node logic.
     */
    bool shouldExhaustHops() const { return exhaustRequested; }

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    void alterReceived(meshtastic_MeshPacket &mp) override;
    int32_t runOnce() override;

  private:
    // =========================================================================
    // Unified Cache Entry - Platform-Specific Variants
    // =========================================================================
    //
    // Two structure variants optimize for different memory constraints:
    //
    // FULL STRUCTURE (20 bytes) - Used on ESP32 with PSRAM, Native/Portduino:
    //   Stores complete truncated lat/lon coordinates for precise deduplication.
    //   These platforms have ample memory, so we prioritize accuracy.
    //
    // COMPACT STRUCTURE (12 bytes) - Used on NRF52, ESP32 without PSRAM:
    //   Uses an 8-bit hash of position instead of storing coordinates.
    //   This provides 40% memory reduction (20KB -> 12KB for 1024 entries).
    //   Hash collisions (~0.4% probability) may occasionally drop a valid
    //   position update, but this is acceptable for traffic management.
    //   Uses minute-granularity timestamps (4h15m max range vs 18h).
    //
    // Memory comparison at 1024 cache entries:
    //   - Full (PSRAM):    1024 × 20 = 20,480 bytes (in PSRAM)
    //   - Compact:         1024 × 12 = 12,288 bytes (in heap)
    //

#if (defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
    // -------------------------------------------------------------------------
    // Full Structure - ESP32 with PSRAM, Native/Portduino (20 bytes)
    // -------------------------------------------------------------------------
    // Layout:
    //   [0-3]   node             - NodeNum (4 bytes)
    //   [4-7]   lat_truncated    - Truncated latitude (4 bytes)
    //   [8-11]  lon_truncated    - Truncated longitude (4 bytes)
    //   [12-13] pos_time_secs    - Position timestamp offset (2 bytes)
    //   [14]    rate_count       - Packets in current window (1 byte)
    //   [15]    unknown_count    - Unknown packets count (1 byte)
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

    // Full structure uses 16-bit second offsets (~18 hour range)
    static constexpr bool useCompactTimestamps = false;

#else
    // -------------------------------------------------------------------------
    // Compact Structure - NRF52, ESP32 without PSRAM (12 bytes)
    // -------------------------------------------------------------------------
    // Uses 8-bit position hash instead of full coordinates to save 8 bytes.
    // Hash collision probability is ~0.4% (1/256), which is acceptable since
    // a collision only causes an occasional valid position to be dropped.
    //
    // Layout:
    //   [0-3]   node             - NodeNum (4 bytes)
    //   [4]     pos_hash         - 8-bit hash of truncated position (1 byte)
    //   [5]     rate_count       - Packets in current window (1 byte)
    //   [6]     unknown_count    - Unknown packets count (1 byte)
    //   [7]     pos_time_mins    - Position timestamp in minutes (1 byte, ~4h max)
    //   [8-9]   rate_window_secs - Rate limit window start offset (2 bytes)
    //   [10-11] unknown_secs     - Unknown tracking window start offset (2 bytes)
    //
    struct UnifiedCacheEntry {
        NodeNum node;              // 4 bytes - Node identifier (0 = empty slot)
        uint8_t pos_hash;          // 1 byte  - 8-bit hash of truncated lat/lon
        uint8_t rate_count;        // 1 byte  - Packet count (saturates at 255)
        uint8_t unknown_count;     // 1 byte  - Unknown packet count (saturates at 255)
        uint8_t pos_time_mins;     // 1 byte  - Minutes since epoch (4h15m max range)
        uint16_t rate_window_secs; // 2 bytes - Window start for rate limiting
        uint16_t unknown_secs;     // 2 bytes - Window start for unknown tracking
    };
    static_assert(sizeof(UnifiedCacheEntry) == 12, "Compact UnifiedCacheEntry should be 12 bytes");

    // Compact structure uses 8-bit minute offsets (~4 hour range)
    static constexpr bool useCompactTimestamps = true;

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
    // TRAFFIC_MANAGEMENT_CACHE_SIZE rounds up to next power-of-2 internally
    // (e.g., 1000 → 1024, 2000 → 2048). Override per-variant in variant.h.
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
    // Timestamps are stored as relative offsets from a rolling epoch to save
    // memory. Two modes are supported based on platform:
    //
    // FULL (PSRAM): 16-bit second offsets (~18 hour range)
    //   - Used for all timestamp fields
    //   - Epoch resets when approaching overflow (~17 hours)
    //
    // COMPACT (non-PSRAM): Mixed granularity
    //   - Position: 8-bit minute offsets (~4 hour range, sufficient for dedup)
    //   - Rate/Unknown: 16-bit second offsets (same as full)
    //   - Epoch resets when position minutes would overflow (~4 hours)
    //
    uint32_t cacheEpochMs = 0; // Rolling epoch base for relative timestamps

    // --- 16-bit second offset helpers (used by both modes for rate/unknown) ---

    uint16_t toRelativeSecs(uint32_t nowMs) const
    {
        uint32_t offsetMs = nowMs - cacheEpochMs;
        uint32_t secs = offsetMs / 1000;
        return (secs > UINT16_MAX) ? UINT16_MAX : static_cast<uint16_t>(secs);
    }

    uint32_t fromRelativeSecs(uint16_t secs) const { return cacheEpochMs + (static_cast<uint32_t>(secs) * 1000); }

    // --- 8-bit minute offset helpers (compact mode position timestamps) ---

    uint8_t toRelativeMins(uint32_t nowMs) const
    {
        uint32_t offsetMs = nowMs - cacheEpochMs;
        uint32_t mins = offsetMs / 60000; // ms to minutes
        return (mins > UINT8_MAX) ? UINT8_MAX : static_cast<uint8_t>(mins);
    }

    uint32_t fromRelativeMins(uint8_t mins) const { return cacheEpochMs + (static_cast<uint32_t>(mins) * 60000); }

    // --- Epoch reset detection ---

    bool needsEpochReset(uint32_t nowMs) const
    {
        if (useCompactTimestamps) {
            // Compact mode: reset when approaching 8-bit minute overflow (~4 hours)
            // Reset at ~3.5 hours (210 minutes) to provide margin
            return (nowMs - cacheEpochMs) > (210UL * 60 * 1000);
        } else {
            // Full mode: reset when approaching 16-bit second overflow (~17 hours)
            return (nowMs - cacheEpochMs) > (UINT16_MAX - 3600) * 1000UL;
        }
    }

    void resetEpoch(uint32_t nowMs);

    // =========================================================================
    // Position Hash (Compact Mode Only)
    // =========================================================================
    //
    // Computes an 8-bit hash from truncated lat/lon coordinates.
    // Used instead of storing full coordinates to save 7 bytes per entry.
    //
    // Hash collision probability: ~0.4% (1/256)
    // Impact of collision: may drop a valid position update (acceptable)
    //
    static uint8_t computePositionHash(int32_t lat_truncated, int32_t lon_truncated);

    // =========================================================================
    // Cache Storage
    // =========================================================================

    mutable concurrency::Lock cacheLock; // Protects all cache access
    UnifiedCacheEntry *cache = nullptr;  // Cuckoo hash table (unified for all platforms)

    meshtastic_TrafficManagementStats stats;

    // Flag set during alterReceived() when packet should be exhausted.
    // Checked by perhapsRebroadcast() to force hop_limit = 0.
    // Reset at start of handleReceived().
    bool exhaustRequested = false;

    // =========================================================================
    // Cache Operations
    // =========================================================================

    // Find or create entry for node using cuckoo hashing
    // Returns nullptr if cache is full and eviction fails
    UnifiedCacheEntry *findOrCreateEntry(NodeNum node, bool *isNew);

    // Find existing entry (no creation)
    UnifiedCacheEntry *findEntry(NodeNum node);

    // =========================================================================
    // Traffic Management Logic
    // =========================================================================

    bool shouldDropPosition(const meshtastic_MeshPacket *p, const meshtastic_Position *pos, uint32_t nowMs);
    bool shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse);
    bool isMinHopsFromRequestor(const meshtastic_MeshPacket *p) const;
    bool isRateLimited(NodeNum from, uint32_t nowMs);
    bool shouldDropUnknown(const meshtastic_MeshPacket *p, uint32_t nowMs);
    void exhaustHops(meshtastic_MeshPacket *p);

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

// Helper: floor(log2(n)) for n >= 0, C++11-compatible constexpr.
constexpr uint8_t log2Floor(uint16_t n)
{
    return n <= 1 ? 0 : static_cast<uint8_t>(1 + log2Floor(static_cast<uint16_t>(n >> 1)));
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
