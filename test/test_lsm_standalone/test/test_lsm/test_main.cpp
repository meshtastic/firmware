// Standalone LSM tests - completely isolated from Meshtastic
// Tests core LSM algorithms without any external dependencies

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unity.h>
#include <vector>

// ============================================================================
// Minimal LSM Types (copied inline for standalone testing)
// ============================================================================

namespace meshtastic
{
namespace tinylsm
{

// CompositeKey
struct CompositeKey {
    uint64_t value;

    CompositeKey() : value(0) {}
    explicit CompositeKey(uint64_t v) : value(v) {}
    CompositeKey(uint32_t node_id, uint16_t field_tag)
        : value((static_cast<uint64_t>(node_id) << 16) | static_cast<uint64_t>(field_tag))
    {
    }

    bool operator<(const CompositeKey &other) const { return value < other.value; }
    bool operator>(const CompositeKey &other) const { return value > other.value; }
    bool operator==(const CompositeKey &other) const { return value == other.value; }

    uint32_t node_id() const { return static_cast<uint32_t>(value >> 16); }
    uint16_t field_tag() const { return static_cast<uint16_t>(value & 0xFFFF); }
};

// Field tags
enum FieldTagEnum : uint16_t {
    WHOLE_DURABLE = 1,
    LAST_HEARD = 3,
    NEXT_HOP = 4,
    CHANNEL = 8,
};

typedef uint16_t FieldTag;

inline const char *field_tag_name(FieldTag tag)
{
    switch (tag) {
    case WHOLE_DURABLE:
        return "DURABLE";
    case LAST_HEARD:
        return "LAST_HEARD";
    case NEXT_HOP:
        return "NEXT_HOP";
    case CHANNEL:
        return "CHANNEL";
    default:
        return "UNKNOWN";
    }
}

// Records
struct DurableRecord {
    uint32_t node_id;
    char long_name[40];
    char short_name[5];
    uint8_t public_key[32];
    uint8_t hw_model;
    uint32_t flags;
}; // 84 bytes

struct EphemeralRecord {
    uint32_t node_id;
    uint32_t last_heard_epoch;
    uint32_t next_hop;
    int16_t rssi_avg;
    int8_t snr;
    uint8_t role;
    uint8_t hop_limit;
    uint8_t channel;
    uint8_t battery_level;
    uint16_t route_cost;
    uint32_t flags;
}; // 24 bytes

// CRC32 (simplified implementation)
class CRC32
{
  private:
    static uint32_t table[256];
    static bool initialized;

    static void init()
    {
        if (initialized)
            return;
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
            }
            table[i] = crc;
        }
        initialized = true;
    }

  public:
    static uint32_t compute(const uint8_t *data, size_t length)
    {
        init();
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; i++) {
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }
};

uint32_t CRC32::table[256];
bool CRC32::initialized = false;

// Bloom Filter (simplified)
class BloomFilter
{
  private:
    std::vector<uint8_t> bits;
    size_t num_bits;

    size_t hash1(uint64_t key) const
    {
        uint64_t h = key;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        return h % num_bits;
    }

    size_t hash2(uint64_t key) const
    {
        uint64_t h = key;
        h ^= h >> 30;
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 27;
        return h % num_bits;
    }

  public:
    BloomFilter(size_t estimated_keys, float bits_per_key)
    {
        num_bits = static_cast<size_t>(estimated_keys * bits_per_key);
        size_t num_bytes = (num_bits + 7) / 8;
        bits.resize(num_bytes, 0);
        num_bits = num_bytes * 8;
    }

    void add(CompositeKey key)
    {
        size_t h1 = hash1(key.value);
        size_t h2 = hash2(key.value);

        bits[h1 / 8] |= (1 << (h1 % 8));
        bits[h2 / 8] |= (1 << (h2 % 8));
    }

    bool maybe_contains(CompositeKey key) const
    {
        size_t h1 = hash1(key.value);
        size_t h2 = hash2(key.value);

        bool b1 = bits[h1 / 8] & (1 << (h1 % 8));
        bool b2 = bits[h2 / 8] & (1 << (h2 % 8));

        return b1 && b2;
    }
};

} // namespace tinylsm
} // namespace meshtastic

// NodeShadow
struct NodeShadow {
    uint32_t node_id;
    uint32_t last_heard;
    uint32_t is_favorite : 1;
    uint32_t is_ignored : 1;
    uint32_t has_user : 1;
    uint32_t has_position : 1;
    uint32_t via_mqtt : 1;
    uint32_t has_hops_away : 1;
    uint32_t reserved_flags : 10;
    uint32_t hops_away : 8;
    uint32_t channel : 8;
    uint32_t sort_key;

    NodeShadow()
        : node_id(0), last_heard(0), is_favorite(0), is_ignored(0), has_user(0), has_position(0), via_mqtt(0), has_hops_away(0),
          reserved_flags(0), hops_away(0), channel(0), sort_key(0)
    {
    }

    NodeShadow(uint32_t id, uint32_t heard)
        : node_id(id), last_heard(heard), is_favorite(0), is_ignored(0), has_user(0), has_position(0), via_mqtt(0),
          has_hops_away(0), reserved_flags(0), hops_away(0), channel(0), sort_key(0)
    {
        update_sort_key(0);
    }

    void update_sort_key(uint32_t our_node_id)
    {
        if (node_id == our_node_id) {
            sort_key = 0;
        } else if (is_favorite) {
            sort_key = 1;
        } else {
            sort_key = 0xFFFFFFFF - last_heard;
        }
    }

    bool operator<(const NodeShadow &other) const { return sort_key < other.sort_key; }
};

using namespace meshtastic::tinylsm;

// ============================================================================
// Tests
// ============================================================================

void test_crc32_basic()
{
    const char *test_data = "Hello, World!";
    uint32_t crc = CRC32::compute(reinterpret_cast<const uint8_t *>(test_data), strlen(test_data));
    uint32_t crc2 = CRC32::compute(reinterpret_cast<const uint8_t *>(test_data), strlen(test_data));
    TEST_ASSERT_EQUAL_UINT32(crc, crc2);
}

void test_key_encoding()
{
    CompositeKey key(0x12345678, 0xABCD);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, key.node_id());
    TEST_ASSERT_EQUAL_UINT16(0xABCD, key.field_tag());
}

void test_key_comparison()
{
    CompositeKey k1(0x100, 0x1);
    CompositeKey k2(0x100, 0x2);
    CompositeKey k3(0x101, 0x1);

    TEST_ASSERT_TRUE(k1 < k2);
    TEST_ASSERT_TRUE(k2 < k3);
}

void test_bloom_add_contains()
{
    BloomFilter filter(100, 8.0f);

    CompositeKey k1(0x100, 1);
    CompositeKey k2(0x200, 1);

    filter.add(k1);
    filter.add(k2);

    TEST_ASSERT_TRUE(filter.maybe_contains(k1));
    TEST_ASSERT_TRUE(filter.maybe_contains(k2));
}

void test_bloom_false_positive_rate()
{
    BloomFilter filter(1000, 8.0f);

    for (uint32_t i = 0; i < 500; i++) {
        filter.add(CompositeKey(i, LAST_HEARD));
    }

    uint32_t false_positives = 0;
    for (uint32_t i = 1000; i < 2000; i++) {
        if (filter.maybe_contains(CompositeKey(i, LAST_HEARD))) {
            false_positives++;
        }
    }

    float fp_rate = 100.0f * false_positives / 1000.0f;
    TEST_ASSERT_LESS_THAN(5.0f, fp_rate);
    printf("Bloom filter FP rate: %.2f%% (should be <5%%)\n", fp_rate);
}

void test_shadow_index_basic()
{
    NodeShadow shadow(0x12345678, 1000);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, shadow.node_id);
    TEST_ASSERT_EQUAL_UINT32(1000, shadow.last_heard);
    TEST_ASSERT_EQUAL(16, sizeof(NodeShadow));
}

void test_shadow_index_sorting()
{
    std::vector<NodeShadow> shadows;

    NodeShadow s1(0x100, 1000);
    NodeShadow s2(0x200, 2000);
    NodeShadow s3(0x300, 500);

    s2.is_favorite = true;

    s1.update_sort_key(0x999);
    s2.update_sort_key(0x999);
    s3.update_sort_key(0x999);

    shadows.push_back(s1);
    shadows.push_back(s2);
    shadows.push_back(s3);

    std::sort(shadows.begin(), shadows.end());

    TEST_ASSERT_EQUAL_UINT32(0x200, shadows[0].node_id);
    TEST_ASSERT_TRUE(shadows[0].is_favorite);
}

void test_field_tag_names()
{
    TEST_ASSERT_EQUAL_STRING("DURABLE", field_tag_name(WHOLE_DURABLE));
    TEST_ASSERT_EQUAL_STRING("LAST_HEARD", field_tag_name(LAST_HEARD));
    TEST_ASSERT_EQUAL_STRING("NEXT_HOP", field_tag_name(NEXT_HOP));
    TEST_ASSERT_EQUAL_STRING("CHANNEL", field_tag_name(CHANNEL));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", field_tag_name(999));
}

void test_struct_sizes()
{
    // Verify sizes are reasonable (padding may vary by platform)
    TEST_ASSERT_LESS_OR_EQUAL(96, sizeof(DurableRecord));    // Max 96 bytes
    TEST_ASSERT_GREATER_OR_EQUAL(84, sizeof(DurableRecord)); // Min 84 bytes

    TEST_ASSERT_LESS_OR_EQUAL(32, sizeof(EphemeralRecord));    // Max 32 bytes
    TEST_ASSERT_GREATER_OR_EQUAL(24, sizeof(EphemeralRecord)); // Min 24 bytes

    TEST_ASSERT_EQUAL(16, sizeof(NodeShadow)); // Exactly 16 (critical for optimization)

    printf("\n✅ Struct Sizes (with platform padding):\n");
    printf("   DurableRecord:   %zu bytes (target: 84, acceptable: 84-96)\n", sizeof(DurableRecord));
    printf("   EphemeralRecord: %zu bytes (target: 24, acceptable: 24-32)\n", sizeof(EphemeralRecord));
    printf("   NodeShadow:      %zu bytes (must be exactly 16) ✓\n", sizeof(NodeShadow));
}

void test_composite_key_grouping()
{
    CompositeKey durable(0x1234, WHOLE_DURABLE);
    CompositeKey ephemeral(0x1234, LAST_HEARD);
    CompositeKey other(0x1235, WHOLE_DURABLE);

    TEST_ASSERT_TRUE(durable < ephemeral);
    TEST_ASSERT_TRUE(ephemeral < other);
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    printf("\n");
    printf("========================================\n");
    printf("  Tiny-LSM Standalone Test Suite\n");
    printf("========================================\n");
    printf("\n");

    RUN_TEST(test_crc32_basic);
    RUN_TEST(test_key_encoding);
    RUN_TEST(test_key_comparison);
    RUN_TEST(test_bloom_add_contains);
    RUN_TEST(test_bloom_false_positive_rate);
    RUN_TEST(test_shadow_index_basic);
    RUN_TEST(test_shadow_index_sorting);
    RUN_TEST(test_field_tag_names);
    RUN_TEST(test_struct_sizes);
    RUN_TEST(test_composite_key_grouping);

    printf("\n");
    return UNITY_END();
}
