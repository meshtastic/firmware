# Tiny-LSM Test Suite

Comprehensive unit tests for the LSM storage backend.

## Running Tests

### Option 1: PlatformIO (Recommended)

```bash
# Run all tests on native (host)
pio test -e native

# Run specific test
pio test -e native -f test_memtable

# Run with verbose output
pio test -e native -v

# Run on device (ESP32)
pio test -e esp32-s3-devkitc-1
```

### Option 2: Makefile (Quick Local Testing)

```bash
cd test/test_tinylsm

# Compile and run
make run

# Just compile
make

# Clean up
make clean
```

### Option 3: Manual Compilation

```bash
cd test/test_tinylsm

# Compile
g++ -std=c++11 -I../../src -I../../.pio/libdeps/native/Unity/src \
    test_main.cpp \
    ../../src/libtinylsm/tinylsm_*.cpp \
    ../../.pio/libdeps/native/Unity/src/unity.c \
    -o test_tinylsm

# Run
./test_tinylsm
```

---

## Test Coverage

### Component Tests

**CRC32:**

- ✅ Basic CRC computation
- ✅ Empty buffer handling
- ✅ Consistency checks

**Key Encoding:**

- ✅ Encode/decode round-trip
- ✅ CompositeKey comparison operators
- ✅ node_id and field_tag extraction

**Memtable:**

- ✅ Put/get operations
- ✅ Update (replace value)
- ✅ Delete (tombstone)
- ✅ Sorted order iteration
- ✅ Stress test (500 entries)

**Bloom Filter:**

- ✅ Add and contains
- ✅ Serialization/deserialization
- ✅ False positive rate (<5%)

**Manifest:**

- ✅ Add/remove tables
- ✅ Query by level
- ✅ Generation tracking

### Integration Tests

**Shadow Index:**

- ✅ Basic creation and update
- ✅ Sorting by priority (our node, favorites, recency)
- ✅ 16-byte size verification

**Field Tags:**

- ✅ Human-readable name mapping
- ✅ All enum values covered

**Durable/Ephemeral Split:**

- ✅ Record sizes (84B and 24B)
- ✅ CompositeKey grouping by node_id

**LRU Cache:**

- ✅ Eviction policy (oldest first)
- ✅ Cache hit/miss tracking

### Stress Tests

**Memtable Capacity:**

- ✅ 500 entries insert
- ✅ All entries retrievable
- ✅ No data corruption

**Bloom Filter Accuracy:**

- ✅ 1000-key dataset
- ✅ False positive rate measurement
- ✅ <5% FP rate verified

---

## Expected Output

### All Tests Passing

```
test/test_tinylsm/test_main.cpp:18:test_crc32_basic:PASS
test/test_tinylsm/test_main.cpp:29:test_crc32_empty:PASS
test/test_tinylsm/test_main.cpp:38:test_key_encoding:PASS
test/test_tinylsm/test_main.cpp:51:test_key_comparison:PASS
test/test_tinylsm/test_main.cpp:66:test_memtable_put_get:PASS
test/test_tinylsm/test_main.cpp:86:test_memtable_update:PASS
test/test_tinylsm/test_main.cpp:106:test_memtable_delete:PASS
test/test_tinylsm/test_main.cpp:124:test_memtable_sorted_order:PASS
test/test_tinylsm/test_main.cpp:154:test_bloom_add_contains:PASS
test/test_tinylsm/test_main.cpp:173:test_bloom_serialize:PASS
test/test_tinylsm/test_main.cpp:195:test_manifest_add_remove:PASS
test/test_tinylsm/test_main.cpp:212:test_manifest_levels:PASS
test/test_tinylsm/test_main.cpp:235:test_shadow_index_basic:PASS
test/test_tinylsm/test_main.cpp:245:test_shadow_index_sorting:PASS
test/test_tinylsm/test_main.cpp:275:test_field_tag_names:PASS
test/test_tinylsm/test_main.cpp:286:test_durable_ephemeral_split:PASS
test/test_tinylsm/test_main.cpp:301:test_cache_lru_eviction:PASS
test/test_tinylsm/test_main.cpp:340:test_memtable_many_entries:PASS
test/test_tinylsm/test_main.cpp:365:test_bloom_false_positive_rate:PASS

-----------------------
19 Tests 0 Failures 0 Ignored
OK
```

---

## Adding New Tests

### Template

```cpp
void test_your_feature()
{
    // Setup
    YourComponent component;

    // Test
    component.doSomething();

    // Assert
    TEST_ASSERT_EQUAL(expected, actual);
}

// Add to main():
RUN_TEST(test_your_feature);
```

### Assertions Available

```cpp
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)
TEST_ASSERT_EQUAL(expected, actual)
TEST_ASSERT_EQUAL_UINT32(expected, actual)
TEST_ASSERT_EQUAL_STRING(expected, actual)
TEST_ASSERT_EQUAL_MEMORY(expected, actual, size)
TEST_ASSERT_LESS_THAN(threshold, actual)
TEST_ASSERT_NOT_NULL(pointer)
```

---

## CI Integration

### GitHub Actions

```yaml
# .github/workflows/tests.yml
- name: Run LSM Tests
  run: pio test -e native -f test_tinylsm
```

### Pre-Commit Hook

```bash
#!/bin/bash
# .git/hooks/pre-commit
pio test -e native -f test_tinylsm || exit 1
```

---

## Troubleshooting

### "Unity not found"

```bash
# Install Unity test framework
pio lib install Unity
```

### "Cannot find tinylsm headers"

Check that paths in test_main.cpp are correct:

```cpp
#include "../../src/libtinylsm/tinylsm_types.h"
```

### Tests hang on device

Increase delay in setup():

```cpp
delay(5000);  // Wait for serial connection
```

---

## Future Test Additions

- [ ] SortedTable write/read round-trip
- [ ] WAL replay verification
- [ ] Compaction correctness
- [ ] Power-loss simulation (with mocks)
- [ ] Concurrent access (thread safety)
- [ ] Memory leak detection (Valgrind)
- [ ] Performance benchmarks (with timing)
