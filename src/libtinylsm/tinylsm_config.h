#pragma once

#include <cstddef>
#include <cstdint>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Compile-time Configuration
// ============================================================================

// Enable PSRAM usage on ESP32 (runtime detection still needed)
#if defined(ARCH_ESP32)
#define TINYLSM_USE_PSRAM 1
#else
#define TINYLSM_USE_PSRAM 0
#endif

// Enable Bloom filters (default on for ESP32, off for nRF52)
#if defined(ARCH_ESP32)
#define TINYLSM_ENABLE_BLOOM 1
#else
#define TINYLSM_ENABLE_BLOOM 0
#endif

// Enable durable WAL
#define TINYLSM_DURABLE_WAL 1

// Number of shards (1 for nRF52, 4 for ESP32)
#if defined(ARCH_ESP32)
#define TINYLSM_SHARDS 4
#else
#define TINYLSM_SHARDS 1
#endif

// ============================================================================
// Runtime Configuration
// ============================================================================

struct StoreConfig {
    // Platform detection
    bool has_psram;

    // Memtable sizes (in KB)
    size_t memtable_durable_kb;
    size_t memtable_ephemeral_kb;

    // Block size for SortedTables
    size_t block_size_bytes;

    // Bloom filter settings
    bool enable_bloom;
    float bloom_bits_per_key;

    // Flush intervals
    uint32_t flush_interval_sec_ephem;

    // TTL for ephemeral data (seconds)
    uint32_t ttl_ephemeral_sec;

    // Sharding
    uint8_t shards;

    // Compaction settings
    uint8_t max_l0_tables;
    uint8_t size_tier_K; // Number of similar-sized tables to trigger merge

    // Cache sizes (ESP32 only)
    size_t block_cache_kb;
    size_t filter_cache_kb;

    // File system paths
    const char *base_path;
    const char *durable_path;
    const char *ephemeral_path;

    // WAL settings
    size_t wal_ring_kb;

    // Emergency behavior
    bool enable_low_battery_flush;

    // Constructor with defaults
    StoreConfig()
        : has_psram(false), memtable_durable_kb(32), memtable_ephemeral_kb(16), block_size_bytes(1024), enable_bloom(false),
          bloom_bits_per_key(8.0f), flush_interval_sec_ephem(600), ttl_ephemeral_sec(48 * 3600), shards(1), max_l0_tables(4),
          size_tier_K(4), block_cache_kb(0), filter_cache_kb(0), base_path("/lfs"), durable_path("/lfs/nodedb_d"),
          ephemeral_path("/lfs/nodedb_e"), wal_ring_kb(8), enable_low_battery_flush(true)
    {
    }

    // Preset for nRF52 (no PSRAM)
    static StoreConfig nrf52()
    {
        StoreConfig cfg;
        cfg.has_psram = false;
        cfg.memtable_durable_kb = 32;
        cfg.memtable_ephemeral_kb = 16;
        cfg.block_size_bytes = 1024;
        cfg.enable_bloom = false;
        cfg.bloom_bits_per_key = 8.0f;
        cfg.flush_interval_sec_ephem = 600;
        cfg.ttl_ephemeral_sec = 48 * 3600;
        cfg.shards = 1;
        cfg.max_l0_tables = 4;
        cfg.size_tier_K = 4;
        cfg.block_cache_kb = 0;
        cfg.filter_cache_kb = 0;
        cfg.wal_ring_kb = 8;
        return cfg;
    }

    // Preset for ESP32 with PSRAM
    static StoreConfig esp32_psram()
    {
        StoreConfig cfg;
        cfg.has_psram = true;
        cfg.memtable_durable_kb = 256;
        cfg.memtable_ephemeral_kb = 512;
        cfg.block_size_bytes = 1024;
        cfg.enable_bloom = true;
        cfg.bloom_bits_per_key = 8.0f;
        cfg.flush_interval_sec_ephem = 600;
        cfg.ttl_ephemeral_sec = 48 * 3600;
        cfg.shards = 4;
        cfg.max_l0_tables = 4;
        cfg.size_tier_K = 4;
        cfg.block_cache_kb = 64;
        cfg.filter_cache_kb = 32;
        cfg.wal_ring_kb = 16;
        return cfg;
    }

    // Preset for ESP32 without PSRAM
    static StoreConfig esp32_no_psram()
    {
        StoreConfig cfg;
        cfg.has_psram = false;
        cfg.memtable_durable_kb = 64;
        cfg.memtable_ephemeral_kb = 32;
        cfg.block_size_bytes = 1024;
        cfg.enable_bloom = true;
        cfg.bloom_bits_per_key = 8.0f;
        cfg.flush_interval_sec_ephem = 600;
        cfg.ttl_ephemeral_sec = 48 * 3600;
        cfg.shards = 1;
        cfg.max_l0_tables = 4;
        cfg.size_tier_K = 4;
        cfg.block_cache_kb = 32;
        cfg.filter_cache_kb = 16;
        cfg.wal_ring_kb = 8;
        return cfg;
    }
};

// ============================================================================
// Constants
// ============================================================================

namespace constants
{

// Magic numbers for file format validation
constexpr uint32_t SSTABLE_MAGIC = 0x5454534C;  // "LSTT" (Little-endian SortedTable)
constexpr uint32_t MANIFEST_MAGIC = 0x464E4D4C; // "LMNF" (Little-endian MaNiFest)
constexpr uint32_t WAL_MAGIC = 0x4C41574C;      // "LWAL" (Little-endian WAL)

// Version numbers
constexpr uint16_t SSTABLE_VERSION = 1;
constexpr uint16_t MANIFEST_VERSION = 1;
constexpr uint16_t WAL_VERSION = 1;

// Limits
constexpr size_t MAX_KEY_SIZE = 8; // CompositeKey is 64-bit
constexpr size_t MAX_VALUE_SIZE = 4096;
constexpr size_t MAX_FILENAME = 64;
constexpr size_t MAX_PATH = 256;

// Bloom filter constants
constexpr size_t BLOOM_MAX_SIZE_KB = 64; // Max size per filter
constexpr uint8_t BLOOM_NUM_HASHES = 2;  // CPU-light, 2 hash functions

} // namespace constants

} // namespace tinylsm
} // namespace meshtastic
