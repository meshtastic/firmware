// Unit tests for the per-subsystem heap accounting registry - src/memory/MemAudit.cpp.
// Covers add/set arithmetic, snapshot, tag reuse (pointer and strcmp fallback),
// unknown/null tags and table-full behavior. The registry is a process-global
// with no reset, so tests use distinct tags and the fill-the-table test runs last.
#include "TestUtil.h"
#include "memory/MemAudit.h" // BEFORE TestUtil.h - provides Arduino.h via configuration.h
#include <cstdio>
#include <cstring>
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define MA_TEST_ENTRY extern "C"
#else
#define MA_TEST_ENTRY
#endif

#if MESHTASTIC_MEM_AUDIT

namespace
{

// Look a tag up by text in a fresh snapshot; returns its bytes, sets *found.
int32_t bytesFor(const char *tag, bool *found = nullptr)
{
    memaudit::Tag rows[memaudit::kMaxTags];
    size_t n = memaudit::snapshot(rows, memaudit::kMaxTags);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(rows[i].tag, tag) == 0) {
            if (found)
                *found = true;
            return rows[i].bytes;
        }
    }
    if (found)
        *found = false;
    return 0;
}

size_t registeredCount()
{
    memaudit::Tag rows[memaudit::kMaxTags];
    return memaudit::snapshot(rows, memaudit::kMaxTags);
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_ma_set_registersAndOverwrites()
{
    bool found = false;
    memaudit::set("t_set", 1234);
    TEST_ASSERT_EQUAL_INT32(1234, bytesFor("t_set", &found));
    TEST_ASSERT_TRUE(found);

    memaudit::set("t_set", 42); // set overwrites, it never accumulates
    TEST_ASSERT_EQUAL_INT32(42, bytesFor("t_set"));

    memaudit::set("t_set", 0); // zero keeps the tag registered
    TEST_ASSERT_EQUAL_INT32(0, bytesFor("t_set", &found));
    TEST_ASSERT_TRUE(found);
}

void test_ma_add_accumulatesSignedDeltas()
{
    memaudit::add("t_add", 100);
    memaudit::add("t_add", 50);
    TEST_ASSERT_EQUAL_INT32(150, bytesFor("t_add"));

    memaudit::add("t_add", -60);
    TEST_ASSERT_EQUAL_INT32(90, bytesFor("t_add"));

    memaudit::add("t_neg", -5); // unbalanced frees just go negative, no clamping
    TEST_ASSERT_EQUAL_INT32(-5, bytesFor("t_neg"));
}

void test_ma_sameTag_reusesSlot()
{
    const size_t before = registeredCount();
    memaudit::add("t_add", 10); // same literal as the previous test
    TEST_ASSERT_EQUAL_INT32(100, bytesFor("t_add"));
    TEST_ASSERT_EQUAL(before, registeredCount());
}

void test_ma_duplicateText_sharesSlot()
{
    // Same text at different addresses (literals duplicated across translation
    // units are not pointer-identical) - the strcmp fallback must merge them.
    static char a[] = "t_dup";
    static char b[] = "t_dup";
    TEST_ASSERT_TRUE(a != b);

    const size_t before = registeredCount();
    memaudit::add(a, 30);
    memaudit::add(b, 12);
    TEST_ASSERT_EQUAL_INT32(42, bytesFor("t_dup"));
    TEST_ASSERT_EQUAL(before + 1, registeredCount());
}

void test_ma_unknownAndNullTags()
{
    bool found = true;
    bytesFor("t_never_registered", &found); // lookups of unknown tags find nothing
    TEST_ASSERT_FALSE(found);

    const size_t before = registeredCount();
    memaudit::add(nullptr, 99); // null tags are dropped, not crashed on
    memaudit::set(nullptr, 99);
    TEST_ASSERT_EQUAL(before, registeredCount());
}

void test_ma_snapshot_respectsMax()
{
    TEST_ASSERT_GREATER_OR_EQUAL(2, registeredCount());
    memaudit::Tag one;
    TEST_ASSERT_EQUAL(1, memaudit::snapshot(&one, 1));
    TEST_ASSERT_NOT_NULL(one.tag);
    TEST_ASSERT_EQUAL(0, memaudit::snapshot(&one, 0));
}

// Must run last: fills every remaining slot.
void test_ma_tableFull_dropsNewTagsKeepsExisting()
{
    // Storage must outlive the registry (it keeps the pointers), hence static.
    static char names[memaudit::kMaxTags][8];
    for (size_t i = 0; registeredCount() < memaudit::kMaxTags; i++) {
        TEST_ASSERT_LESS_THAN(memaudit::kMaxTags, i);
        snprintf(names[i], sizeof(names[i]), "fill%u", (unsigned)i);
        memaudit::set(names[i], i + 1);
    }
    TEST_ASSERT_EQUAL(memaudit::kMaxTags, registeredCount());

    bool found = true;
    memaudit::add("t_overflow", 77); // no slot left: update dropped
    memaudit::set("t_overflow", 77);
    bytesFor("t_overflow", &found);
    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL(memaudit::kMaxTags, registeredCount());

    memaudit::set("t_set", 7); // existing tags keep working at capacity
    TEST_ASSERT_EQUAL_INT32(7, bytesFor("t_set"));

    memaudit::logBreakdown("test"); // smoke: full table renders one log line
}

MA_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_ma_set_registersAndOverwrites);
    RUN_TEST(test_ma_add_accumulatesSignedDeltas);
    RUN_TEST(test_ma_sameTag_reusesSlot);
    RUN_TEST(test_ma_duplicateText_sharesSlot);
    RUN_TEST(test_ma_unknownAndNullTags);
    RUN_TEST(test_ma_snapshot_respectsMax);
    RUN_TEST(test_ma_tableFull_dropsNewTagsKeepsExisting);
    exit(UNITY_END());
}

#else

void setUp(void) {}
void tearDown(void) {}

MA_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif // MESHTASTIC_MEM_AUDIT

MA_TEST_ENTRY void loop() {}
