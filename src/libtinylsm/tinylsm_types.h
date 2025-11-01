#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Field Tag Enum (for CompositeKey)
// ============================================================================

enum FieldTagEnum : uint16_t {
    WHOLE_DURABLE = 1,   // Entire durable record
    WHOLE_EPHEMERAL = 2, // Entire ephemeral record
    LAST_HEARD = 3,      // Just last_heard_epoch
    NEXT_HOP = 4,        // Just next_hop
    SNR = 5,             // Just snr
    ROLE = 6,            // Just role
    HOP_LIMIT = 7,       // Just hop_limit
    CHANNEL = 8,         // Just channel
    RSSI_AVG = 9,        // Just rssi_avg
    ROUTE_COST = 10,     // Just route_cost
    BATTERY_LEVEL = 11,  // Just battery_level
};

typedef uint16_t FieldTag;

// Helper: Convert field tag to human-readable string
inline const char *field_tag_name(FieldTag tag)
{
    switch (tag) {
    case WHOLE_DURABLE:
        return "DURABLE";
    case WHOLE_EPHEMERAL:
        return "EPHEMERAL";
    case LAST_HEARD:
        return "LAST_HEARD";
    case NEXT_HOP:
        return "NEXT_HOP";
    case SNR:
        return "SNR";
    case ROLE:
        return "ROLE";
    case HOP_LIMIT:
        return "HOP_LIMIT";
    case CHANNEL:
        return "CHANNEL";
    case RSSI_AVG:
        return "RSSI_AVG";
    case ROUTE_COST:
        return "ROUTE_COST";
    case BATTERY_LEVEL:
        return "BATTERY_LEVEL";
    default:
        return "UNKNOWN";
    }
}

// ============================================================================
// Composite Key (64-bit: node_id << 16 | field_tag)
// ============================================================================

struct CompositeKey {
    uint64_t value;

    CompositeKey() : value(0) {}
    explicit CompositeKey(uint64_t v) : value(v) {}

    // Construct from node_id and field_tag
    CompositeKey(uint32_t node_id, uint16_t field_tag)
        : value((static_cast<uint64_t>(node_id) << 16) | static_cast<uint64_t>(field_tag))
    {
    }

    bool operator<(const CompositeKey &other) const { return value < other.value; }
    bool operator>(const CompositeKey &other) const { return value > other.value; }
    bool operator==(const CompositeKey &other) const { return value == other.value; }
    bool operator!=(const CompositeKey &other) const { return value != other.value; }
    bool operator>=(const CompositeKey &other) const { return value >= other.value; }
    bool operator<=(const CompositeKey &other) const { return value <= other.value; }

    uint32_t node_id() const { return static_cast<uint32_t>(value >> 16); }
    uint16_t field_tag() const { return static_cast<uint16_t>(value & 0xFFFF); }
};

// ============================================================================
// Value Blob (move-only, avoids copies)
// ============================================================================

struct ValueBlob {
    std::vector<uint8_t> data;

    ValueBlob() {}
    ValueBlob(size_t size) : data(size) {}
    ValueBlob(const uint8_t *src, size_t size, bool copy = true)
    {
        if (copy) {
            data.assign(src, src + size);
        } else {
            // For zero-copy scenarios (advanced)
            data.resize(size);
            memcpy(data.data(), src, size);
        }
    }

    // Move-only semantics
    ValueBlob(ValueBlob &&other) noexcept : data(std::move(other.data)) {}
    ValueBlob &operator=(ValueBlob &&other) noexcept
    {
        if (this != &other) {
            data = std::move(other.data);
        }
        return *this;
    }

    // Disable copy
    ValueBlob(const ValueBlob &) = delete;
    ValueBlob &operator=(const ValueBlob &) = delete;

    const uint8_t *ptr() const { return data.data(); }
    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }

    void resize(size_t s) { data.resize(s); }
    void clear() { data.clear(); }
};

// ============================================================================
// Durable Record (identity & configuration)
// ============================================================================

struct DurableRecord {
    uint32_t node_id;       // Node identifier
    char long_name[40];     // Display name (null-terminated)
    char short_name[5];     // Short name (null-terminated)
    uint8_t public_key[32]; // Encryption key
    uint8_t hw_model;       // Hardware type enum
    uint32_t flags;         // Config flags

    DurableRecord() : node_id(0), hw_model(0), flags(0)
    {
        memset(long_name, 0, sizeof(long_name));
        memset(short_name, 0, sizeof(short_name));
        memset(public_key, 0, sizeof(public_key));
    }
};

// ============================================================================
// Ephemeral Record (routing & metrics - HOT PATH)
// ============================================================================

struct EphemeralRecord {
    uint32_t node_id;          // Node identifier
    uint32_t last_heard_epoch; // Last heard time (Unix epoch seconds)
    uint32_t next_hop;         // Next hop node ID for routing ⚡
    int16_t rssi_avg;          // Average RSSI
    int8_t snr;                // SNR in dB (-128..+127) ⚡
    uint8_t role;              // Role (client/router/etc) ⚡
    uint8_t hop_limit;         // Hops away (0..255), was hops_away ⚡
    uint8_t channel;           // Channel number (0..255) ⚡
    uint8_t battery_level;     // Battery % (0-100)
    uint16_t route_cost;       // Routing metric
    uint32_t flags;            // Runtime flags

    EphemeralRecord()
        : node_id(0), last_heard_epoch(0), next_hop(0), rssi_avg(0), snr(0), role(0), hop_limit(0), channel(0), battery_level(0),
          route_cost(0), flags(0)
    {
    }
};

// ============================================================================
// Key Range
// ============================================================================

struct KeyRange {
    CompositeKey start;
    CompositeKey end;

    KeyRange() {}
    KeyRange(CompositeKey s, CompositeKey e) : start(s), end(e) {}

    bool contains(CompositeKey key) const { return key >= start && key <= end; }
    bool overlaps(const KeyRange &other) const { return !(end < other.start || other.end < start); }
};

// ============================================================================
// Store Statistics
// ============================================================================

struct StoreStats {
    // Memtable
    uint32_t durable_memtable_entries;
    uint32_t ephemeral_memtable_entries;

    // SortedTables
    uint32_t durable_sstables;
    uint32_t ephemeral_sstables;

    // Sizes
    size_t durable_total_bytes;
    size_t ephemeral_total_bytes;

    // Operations
    uint32_t compactions_total;
    uint32_t sstables_written;
    uint32_t sstables_deleted;

    // Cache (if implemented)
    uint32_t cache_hits;
    uint32_t cache_misses;

    StoreStats()
        : durable_memtable_entries(0), ephemeral_memtable_entries(0), durable_sstables(0), ephemeral_sstables(0),
          durable_total_bytes(0), ephemeral_total_bytes(0), compactions_total(0), sstables_written(0), sstables_deleted(0),
          cache_hits(0), cache_misses(0)
    {
    }
};

// ============================================================================
// Get Result (wrapper for optional return values)
// ============================================================================

template <typename T> struct GetResult {
    bool found;
    T value;

    GetResult() : found(false), value() {}
    GetResult(bool f, const T &v) : found(f), value(v) {}
};

} // namespace tinylsm
} // namespace meshtastic
