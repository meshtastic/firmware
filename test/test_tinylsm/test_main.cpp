// Unit tests for Tiny-LSM components
// These tests can run on host (native) or on-device

#include "../../src/libtinylsm/tinylsm_filter.h"
#include "../../src/libtinylsm/tinylsm_manifest.h"
#include "../../src/libtinylsm/tinylsm_memtable.h"
#include "../../src/libtinylsm/tinylsm_types.h"
#include "../../src/libtinylsm/tinylsm_utils.h"
#include "../../src/mesh/NodeShadow.h"
#include <algorithm>
#include <cstring>
#include <unity.h>

using namespace meshtastic::tinylsm;

// ============================================================================
// CRC32 Tests
// ============================================================================

void test_crc32_basic()
{
    const char *test_data = "Hello, World!";
    uint32_t crc = CRC32::compute(reinterpret_cast<const uint8_t *>(test_data), strlen(test_data));

    // Known CRC32 for "Hello, World!"
    // We just check it's consistent
    uint32_t crc2 = CRC32::compute(reinterpret_cast<const uint8_t *>(test_data), strlen(test_data));
    TEST_ASSERT_EQUAL_UINT32(crc, crc2);
}

void test_crc32_empty()
{
    uint32_t crc = CRC32::compute(nullptr, 0);
    // CRC32 of empty buffer: starts with 0xFFFFFFFF, no bytes processed,
    // final XOR with 0xFFFFFFFF results in 0
    TEST_ASSERT_EQUAL_UINT32(0, crc);
}

// ============================================================================
// Key Encoding Tests
// ============================================================================

void test_key_encoding()
{
    CompositeKey key(0x12345678, 0xABCD);

    uint8_t buffer[8];
    encode_key(key, buffer);

    CompositeKey decoded = decode_key(buffer);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, decoded.node_id());
    TEST_ASSERT_EQUAL_UINT16(0xABCD, decoded.field_tag());
}

void test_key_comparison()
{
    CompositeKey k1(0x100, 0x1);
    CompositeKey k2(0x100, 0x2);
    CompositeKey k3(0x101, 0x1);

    TEST_ASSERT_TRUE(k1 < k2); // Same node, different field
    TEST_ASSERT_TRUE(k2 < k3); // Different node
    TEST_ASSERT_TRUE(k1 < k3);
}

// ============================================================================
// Memtable Tests
// ============================================================================

void test_memtable_put_get()
{
    Memtable mt(32); // 32KB

    CompositeKey key(0x123, 1);
    const char *value = "test value";

    TEST_ASSERT_TRUE(mt.put(key, reinterpret_cast<const uint8_t *>(value), strlen(value)));

    uint8_t *retrieved_value;
    size_t retrieved_size;
    bool is_tombstone;

    TEST_ASSERT_TRUE(mt.get(key, &retrieved_value, &retrieved_size, &is_tombstone));
    TEST_ASSERT_EQUAL(strlen(value), retrieved_size);
    TEST_ASSERT_EQUAL_MEMORY(value, retrieved_value, retrieved_size);
    TEST_ASSERT_FALSE(is_tombstone);
}

void test_memtable_update()
{
    Memtable mt(32);

    CompositeKey key(0x123, 1);
    const char *value1 = "first";
    const char *value2 = "second value";

    mt.put(key, reinterpret_cast<const uint8_t *>(value1), strlen(value1));
    mt.put(key, reinterpret_cast<const uint8_t *>(value2), strlen(value2)); // Update

    uint8_t *retrieved_value;
    size_t retrieved_size;
    bool is_tombstone;

    mt.get(key, &retrieved_value, &retrieved_size, &is_tombstone);
    TEST_ASSERT_EQUAL(strlen(value2), retrieved_size);
    TEST_ASSERT_EQUAL_MEMORY(value2, retrieved_value, retrieved_size);
}

void test_memtable_delete()
{
    Memtable mt(32);

    CompositeKey key(0x123, 1);
    const char *value = "to be deleted";

    mt.put(key, reinterpret_cast<const uint8_t *>(value), strlen(value));
    TEST_ASSERT_TRUE(mt.del(key));

    uint8_t *retrieved_value;
    size_t retrieved_size;
    bool is_tombstone;

    TEST_ASSERT_TRUE(mt.get(key, &retrieved_value, &retrieved_size, &is_tombstone));
    TEST_ASSERT_TRUE(is_tombstone);
}

void test_memtable_sorted_order()
{
    Memtable mt(32);

    // Insert in random order
    mt.put(CompositeKey(0x300, 1), reinterpret_cast<const uint8_t *>("c"), 1);
    mt.put(CompositeKey(0x100, 1), reinterpret_cast<const uint8_t *>("a"), 1);
    mt.put(CompositeKey(0x200, 1), reinterpret_cast<const uint8_t *>("b"), 1);

    // Iterate and verify sorted order
    auto it = mt.begin();
    TEST_ASSERT_TRUE(it.valid());
    TEST_ASSERT_EQUAL_UINT64(CompositeKey(0x100, 1).value, it.key().value);

    it.next();
    TEST_ASSERT_TRUE(it.valid());
    TEST_ASSERT_EQUAL_UINT64(CompositeKey(0x200, 1).value, it.key().value);

    it.next();
    TEST_ASSERT_TRUE(it.valid());
    TEST_ASSERT_EQUAL_UINT64(CompositeKey(0x300, 1).value, it.key().value);

    it.next();
    TEST_ASSERT_FALSE(it.valid());
}

// ============================================================================
// Bloom Filter Tests
// ============================================================================

void test_bloom_add_contains()
{
    BloomFilter filter(100, 8.0f); // 100 keys, 8 bits per key

    CompositeKey k1(0x100, 1);
    CompositeKey k2(0x200, 1);
    CompositeKey k3(0x300, 1);

    filter.add(k1);
    filter.add(k2);

    TEST_ASSERT_TRUE(filter.maybe_contains(k1));
    TEST_ASSERT_TRUE(filter.maybe_contains(k2));

    // k3 not added, but bloom filter can have false positives
    // We can't assert false, but we can check it doesn't crash
    filter.maybe_contains(k3);
}

void test_bloom_serialize()
{
    BloomFilter filter(100, 8.0f);

    filter.add(CompositeKey(0x100, 1));
    filter.add(CompositeKey(0x200, 1));

    std::vector<uint8_t> serialized;
    TEST_ASSERT_TRUE(filter.serialize(serialized));
    TEST_ASSERT_TRUE(serialized.size() > 0);

    BloomFilter filter2;
    TEST_ASSERT_TRUE(filter2.deserialize(serialized.data(), serialized.size()));

    TEST_ASSERT_TRUE(filter2.maybe_contains(CompositeKey(0x100, 1)));
    TEST_ASSERT_TRUE(filter2.maybe_contains(CompositeKey(0x200, 1)));
}

// ============================================================================
// Manifest Tests
// ============================================================================

void test_manifest_add_remove()
{
    Manifest manifest("/tmp", "test-manifest");

    SortedTableMeta meta;
    meta.file_id = 1;
    meta.level = 0;
    meta.shard = 0;
    meta.key_range = KeyRange(CompositeKey(0x100, 1), CompositeKey(0x200, 1));

    TEST_ASSERT_TRUE(manifest.add_table(meta));
    TEST_ASSERT_EQUAL(1, manifest.get_entries().size());

    TEST_ASSERT_TRUE(manifest.remove_table(1));
    TEST_ASSERT_EQUAL(0, manifest.get_entries().size());
}

void test_manifest_levels()
{
    Manifest manifest("/tmp", "test-manifest");

    for (uint8_t i = 0; i < 5; i++) {
        SortedTableMeta meta;
        meta.file_id = i;
        meta.level = i % 3;
        meta.shard = 0;
        manifest.add_table(meta);
    }

    auto level0 = manifest.get_tables_at_level(0);
    auto level1 = manifest.get_tables_at_level(1);
    auto level2 = manifest.get_tables_at_level(2);

    TEST_ASSERT_EQUAL(2, level0.size());
    TEST_ASSERT_EQUAL(2, level1.size());
    TEST_ASSERT_EQUAL(1, level2.size());
}

// ============================================================================
// Shadow Index Tests
// ============================================================================

void test_shadow_index_basic()
{
    NodeShadow shadow(0x12345678, 1000);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, shadow.node_id);
    TEST_ASSERT_EQUAL_UINT32(1000, shadow.last_heard);
    TEST_ASSERT_FALSE(shadow.is_favorite);
    TEST_ASSERT_FALSE(shadow.has_user);
}

void test_shadow_index_sorting()
{
    std::vector<NodeShadow> shadows;

    // Create test shadows
    NodeShadow s1(0x100, 1000); // Node 0x100, heard at 1000
    NodeShadow s2(0x200, 2000); // Node 0x200, heard at 2000 (more recent)
    NodeShadow s3(0x300, 500);  // Node 0x300, heard at 500 (oldest)

    s2.is_favorite = true; // Make s2 a favorite

    // Update sort keys (assume 0x999 is our node)
    s1.update_sort_key(0x999);
    s2.update_sort_key(0x999);
    s3.update_sort_key(0x999);

    shadows.push_back(s1);
    shadows.push_back(s2);
    shadows.push_back(s3);

    // Sort using shadow's operator<
    std::sort(shadows.begin(), shadows.end());

    // Expected order: favorites first (s2), then by recency (s1, s3)
    TEST_ASSERT_EQUAL_UINT32(0x200, shadows[0].node_id); // Favorite first
    TEST_ASSERT_TRUE(shadows[0].is_favorite);
}

// ============================================================================
// Field Tag Tests
// ============================================================================

void test_field_tag_names()
{
    TEST_ASSERT_EQUAL_STRING("DURABLE", field_tag_name(WHOLE_DURABLE));
    TEST_ASSERT_EQUAL_STRING("LAST_HEARD", field_tag_name(LAST_HEARD));
    TEST_ASSERT_EQUAL_STRING("NEXT_HOP", field_tag_name(NEXT_HOP));
    TEST_ASSERT_EQUAL_STRING("SNR", field_tag_name(SNR));
    TEST_ASSERT_EQUAL_STRING("HOP_LIMIT", field_tag_name(HOP_LIMIT));
    TEST_ASSERT_EQUAL_STRING("CHANNEL", field_tag_name(CHANNEL));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", field_tag_name(999));
}

// ============================================================================
// Integration Tests
// ============================================================================

void test_durable_ephemeral_split()
{
    // Verify DurableRecord and EphemeralRecord have reasonable sizes (padding may vary)
    TEST_ASSERT_LESS_OR_EQUAL(96, sizeof(DurableRecord));    // Max 96 bytes with padding
    TEST_ASSERT_GREATER_OR_EQUAL(84, sizeof(DurableRecord)); // Min 84 bytes data

    TEST_ASSERT_LESS_OR_EQUAL(32, sizeof(EphemeralRecord));    // Max 32 bytes with padding
    TEST_ASSERT_GREATER_OR_EQUAL(24, sizeof(EphemeralRecord)); // Min 24 bytes data

    // Verify CompositeKey ordering groups by node_id
    CompositeKey durable_key(0x1234, WHOLE_DURABLE);
    CompositeKey ephemeral_key(0x1234, LAST_HEARD);

    TEST_ASSERT_TRUE(durable_key < ephemeral_key); // Same node, sorted by field

    CompositeKey other_node(0x1235, WHOLE_DURABLE);
    TEST_ASSERT_TRUE(ephemeral_key < other_node); // Different node
}

void test_cache_lru_eviction()
{
    // Simulate LRU cache behavior
    const size_t CACHE_SIZE = 3;

    struct TestCache {
        uint32_t node_id;
        uint32_t last_access;
        bool valid;
    };

    TestCache cache[CACHE_SIZE] = {};

    // Add 3 nodes
    for (size_t i = 0; i < CACHE_SIZE; i++) {
        cache[i].node_id = 100 + i;
        cache[i].last_access = i * 10;
        cache[i].valid = true;
    }

    // Add 4th node - should evict oldest (index 0)
    size_t evict_idx = 0;
    uint32_t oldest = cache[0].last_access;

    for (size_t i = 1; i < CACHE_SIZE; i++) {
        if (cache[i].last_access < oldest) {
            oldest = cache[i].last_access;
            evict_idx = i;
        }
    }

    TEST_ASSERT_EQUAL(0, evict_idx); // Oldest is at index 0

    cache[evict_idx].node_id = 104;
    cache[evict_idx].last_access = 100;

    TEST_ASSERT_EQUAL_UINT32(104, cache[0].node_id); // Evicted and replaced
}

// ============================================================================
// Stress Tests
// ============================================================================

void test_memtable_many_entries()
{
    Memtable mt(64); // 64 KB

    // Add 500 small entries
    for (uint32_t i = 0; i < 500; i++) {
        CompositeKey key(i, LAST_HEARD);
        uint32_t value = i * 100;
        TEST_ASSERT_TRUE(mt.put(key, reinterpret_cast<uint8_t *>(&value), sizeof(value)));
    }

    TEST_ASSERT_EQUAL(500, mt.size_entries());

    // Verify all entries are retrievable
    for (uint32_t i = 0; i < 500; i++) {
        CompositeKey key(i, LAST_HEARD);
        uint8_t *value_ptr;
        size_t value_size;
        bool is_tombstone;

        TEST_ASSERT_TRUE(mt.get(key, &value_ptr, &value_size, &is_tombstone));
        TEST_ASSERT_EQUAL(sizeof(uint32_t), value_size);

        uint32_t retrieved_value = *reinterpret_cast<uint32_t *>(value_ptr);
        TEST_ASSERT_EQUAL_UINT32(i * 100, retrieved_value);
    }
}

void test_bloom_false_positive_rate()
{
    BloomFilter filter(1000, 8.0f); // 1000 keys, 8 bits/key

    // Add 500 keys
    for (uint32_t i = 0; i < 500; i++) {
        filter.add(CompositeKey(i, LAST_HEARD));
    }

    // Check added keys (should all return true)
    for (uint32_t i = 0; i < 500; i++) {
        TEST_ASSERT_TRUE(filter.maybe_contains(CompositeKey(i, LAST_HEARD)));
    }

    // Check non-added keys and count false positives
    uint32_t false_positives = 0;
    for (uint32_t i = 1000; i < 2000; i++) {
        if (filter.maybe_contains(CompositeKey(i, LAST_HEARD))) {
            false_positives++;
        }
    }

    // False positive rate should be < 5% for 8 bits/key
    float fp_rate = 100.0f * false_positives / 1000.0f;
    TEST_ASSERT_LESS_THAN(5.0f, fp_rate);
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp(void)
{
    // Set up before each test
}

void tearDown(void)
{
    // Clean up after each test
}

int main(int argc, char **argv)
{
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    UNITY_BEGIN();

    // CRC32 tests
    RUN_TEST(test_crc32_basic);
    RUN_TEST(test_crc32_empty);

    // Key encoding tests
    RUN_TEST(test_key_encoding);
    RUN_TEST(test_key_comparison);

    // Memtable tests
    RUN_TEST(test_memtable_put_get);
    RUN_TEST(test_memtable_update);
    RUN_TEST(test_memtable_delete);
    RUN_TEST(test_memtable_sorted_order);

    // Bloom filter tests
    RUN_TEST(test_bloom_add_contains);
    RUN_TEST(test_bloom_serialize);

    // Manifest tests
    RUN_TEST(test_manifest_add_remove);
    RUN_TEST(test_manifest_levels);

    // Shadow index tests
    RUN_TEST(test_shadow_index_basic);
    RUN_TEST(test_shadow_index_sorting);

    // Field tag tests
    RUN_TEST(test_field_tag_names);

    // Integration tests
    RUN_TEST(test_durable_ephemeral_split);
    RUN_TEST(test_cache_lru_eviction);

    // Stress tests
    RUN_TEST(test_memtable_many_entries);
    RUN_TEST(test_bloom_false_positive_rate);

    return UNITY_END();
}

// PlatformIO test entry point
#ifdef ARDUINO
void setup()
{
    delay(2000); // Wait for serial
    main(0, NULL);
}

void loop()
{
    // Tests run once in setup
}
#endif
