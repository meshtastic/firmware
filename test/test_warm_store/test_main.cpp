// Unit tests for the warm ("long-tail") node tier - src/mesh/WarmNodeStore.cpp.
// Covers admission/eviction policy (keyed entries outrank keyless), take()
// rehydration semantics, and a tolerant persistence round trip.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides WARM_NODE_COUNT via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define WS_TEST_ENTRY extern "C"
#else
#define WS_TEST_ENTRY
#endif

#if WARM_NODE_COUNT > 0

#include "FSCommon.h"
#include "mesh/WarmNodeStore.h"
#include <cstring>
#include <vector>

namespace
{

void makeKey(uint8_t out[32], uint8_t seed)
{
    memset(out, 0, 32);
    out[0] = seed;
    out[31] = seed ^ 0xA5;
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_ws_absorb_and_copyKey_roundTrip()
{
    WarmNodeStore ws;
    uint8_t key[32], got[32];
    makeKey(key, 7);
    TEST_ASSERT_TRUE(ws.absorb(0x100, 1000, key));
    TEST_ASSERT_TRUE(ws.contains(0x100));
    TEST_ASSERT_TRUE(ws.copyKey(0x100, got));
    TEST_ASSERT_EQUAL_MEMORY(key, got, 32);
    TEST_ASSERT_EQUAL(1, ws.count());
}

void test_ws_keylessEntry_hasNoKey()
{
    WarmNodeStore ws;
    uint8_t got[32];
    TEST_ASSERT_TRUE(ws.absorb(0x200, 1000, NULL));
    TEST_ASSERT_TRUE(ws.contains(0x200));
    TEST_ASSERT_FALSE(ws.copyKey(0x200, got));
}

void test_ws_absorb_rejectsNodeNumZero()
{
    WarmNodeStore ws;
    TEST_ASSERT_FALSE(ws.absorb(0, 1000, NULL));
    TEST_ASSERT_EQUAL(0, ws.count());
}

void test_ws_absorb_updatesExistingEntry()
{
    WarmNodeStore ws;
    uint8_t key[32], got[32];
    makeKey(key, 9);
    TEST_ASSERT_TRUE(ws.absorb(0x300, 1000, NULL));
    TEST_ASSERT_TRUE(ws.absorb(0x300, 2000, key)); // later eviction learned a key
    TEST_ASSERT_EQUAL(1, ws.count());
    TEST_ASSERT_TRUE(ws.copyKey(0x300, got));
    TEST_ASSERT_EQUAL_MEMORY(key, got, 32);
}

void test_ws_take_removesEntry()
{
    WarmNodeStore ws;
    uint8_t key[32];
    makeKey(key, 3);
    ws.absorb(0x400, 1234, key, 5 /* TRACKER */, (uint8_t)WarmProtected::Role);

    WarmNodeEntry e;
    TEST_ASSERT_TRUE(ws.take(0x400, e));
    TEST_ASSERT_EQUAL(0x400, e.num);
    // last_heard is quantised to the metadata quantum; role/protected ride the low bits.
    TEST_ASSERT_EQUAL(1234u & WARM_TIME_MASK, warmTimeOf(e));
    TEST_ASSERT_EQUAL(5, warmRoleOf(e));
    TEST_ASSERT_EQUAL((uint8_t)WarmProtected::Role, warmProtOf(e));
    TEST_ASSERT_EQUAL_MEMORY(key, e.public_key, 32);
    TEST_ASSERT_FALSE(ws.contains(0x400));
    TEST_ASSERT_FALSE(ws.take(0x400, e));
    TEST_ASSERT_EQUAL(0, ws.count());
}

void test_ws_keylessCandidate_neverEvictsKeyedEntries()
{
    WarmNodeStore ws;
    uint8_t key[32];
    // Fill the store entirely with keyed entries
    for (size_t i = 0; i < ws.capacity(); i++) {
        makeKey(key, (uint8_t)i);
        TEST_ASSERT_TRUE(ws.absorb(0x1000 + i, 100 + i, key));
    }
    TEST_ASSERT_EQUAL(ws.capacity(), ws.count());
    // A keyless candidate (even a fresh one) must be rejected
    TEST_ASSERT_FALSE(ws.absorb(0x9999, 999999, NULL));
    TEST_ASSERT_FALSE(ws.contains(0x9999));
}

void test_ws_keyedCandidate_evictsOldestKeylessFirst()
{
    WarmNodeStore ws;
    uint8_t key[32];
    makeKey(key, 0x42);
    // Fill with keyed entries except two keyless ones in the middle
    for (size_t i = 0; i < ws.capacity(); i++) {
        const bool keyless = (i == 5 || i == 10);
        // Timestamps spaced by the 64 s warm metadata quantum (<<6) so LRU order survives
        // quantisation: keyless i=10 is oldest (50), i=5 next (60), keyed all older (10).
        TEST_ASSERT_TRUE(ws.absorb(0x1000 + i, (keyless ? (i == 10 ? 50u : 60u) : 10u) << 6, keyless ? NULL : key));
    }
    // Keyed candidate must displace the OLDEST KEYLESS entry (0x100A, ts=50),
    // even though every keyed entry is older (ts=10)
    uint8_t k2[32];
    makeKey(k2, 0x43);
    TEST_ASSERT_TRUE(ws.absorb(0x8888, 70u << 6, k2));
    TEST_ASSERT_FALSE(ws.contains(0x1000 + 10));
    TEST_ASSERT_TRUE(ws.contains(0x1000 + 5));
    TEST_ASSERT_TRUE(ws.contains(0x8888));
}

void test_ws_keyedCandidate_evictsOldestKeyedWhenNoKeyless()
{
    WarmNodeStore ws;
    uint8_t key[32];
    for (size_t i = 0; i < ws.capacity(); i++) {
        makeKey(key, (uint8_t)i);
        TEST_ASSERT_TRUE(ws.absorb(0x1000 + i, 1000 + i, key)); // 0x1000 is the oldest
    }
    uint8_t k2[32];
    makeKey(k2, 0x44);
    TEST_ASSERT_TRUE(ws.absorb(0x7777, 999999, k2));
    TEST_ASSERT_TRUE(ws.contains(0x7777));
    TEST_ASSERT_FALSE(ws.contains(0x1000)); // oldest keyed evicted
    TEST_ASSERT_EQUAL(ws.capacity(), ws.count());
}

void test_ws_meta_roundTrip()
{
    WarmNodeStore ws;
    uint8_t key[32];
    makeKey(key, 0x77);
    // Keyed TRACKER(5)/Role-protected and keyless SENSOR(6)/unprotected.
    TEST_ASSERT_TRUE(ws.absorb(0x700, 1234, key, 5 /* TRACKER */, (uint8_t)WarmProtected::Role));
    TEST_ASSERT_TRUE(ws.absorb(0x701, 5678, NULL, 6 /* SENSOR */, (uint8_t)WarmProtected::None));

    uint8_t role = 0xFF, prot = 0xFF;
    TEST_ASSERT_TRUE(ws.lookupMeta(0x700, role, prot));
    TEST_ASSERT_EQUAL(5, role);
    TEST_ASSERT_EQUAL((uint8_t)WarmProtected::Role, prot);
    TEST_ASSERT_TRUE(ws.lookupMeta(0x701, role, prot));
    TEST_ASSERT_EQUAL(6, role);
    TEST_ASSERT_EQUAL((uint8_t)WarmProtected::None, prot);
    // Absent node yields false and leaves outputs untouched-by-contract (just check return).
    TEST_ASSERT_FALSE(ws.lookupMeta(0x999, role, prot));
    // Default args still compile (role/protected = 0 = CLIENT/None).
    TEST_ASSERT_TRUE(ws.absorb(0x702, 9999, NULL));
    TEST_ASSERT_TRUE(ws.lookupMeta(0x702, role, prot));
    TEST_ASSERT_EQUAL(0, role);
    TEST_ASSERT_EQUAL((uint8_t)WarmProtected::None, prot);
}

void test_ws_remove_and_clear()
{
    WarmNodeStore ws;
    ws.absorb(0x500, 1, NULL);
    ws.absorb(0x501, 2, NULL);
    ws.remove(0x500);
    TEST_ASSERT_FALSE(ws.contains(0x500));
    TEST_ASSERT_EQUAL(1, ws.count());
    ws.clear();
    TEST_ASSERT_EQUAL(0, ws.count());
}

void test_ws_persistence_roundTrip()
{
    WarmNodeStore a;
    uint8_t key[32], got[32];
    makeKey(key, 0x55);
    a.absorb(0x600, 4242, key);
    a.absorb(0x601, 4243, NULL);
    if (!a.saveIfDirty()) {
        TEST_IGNORE_MESSAGE("Filesystem not available in this test environment");
        return;
    }

    WarmNodeStore b;
    b.load();
    TEST_ASSERT_TRUE(b.contains(0x600));
    TEST_ASSERT_TRUE(b.contains(0x601));
    TEST_ASSERT_TRUE(b.copyKey(0x600, got));
    TEST_ASSERT_EQUAL_MEMORY(key, got, 32);

    // Cleanup so reruns start fresh
    b.clear();
    b.saveIfDirty();
}

// Migration: a v1 (WRM1) warm.dat must keep identity + key but discard last_heard
// (so its low bits aren't misread as role/protected). File backend only.
void test_ws_v1_migration_discardsLastHeard()
{
    WarmNodeStore a;
    uint8_t key[32], got[32];
    makeKey(key, 0x66);
    a.absorb(0x900, 123456, key, 5 /* TRACKER */, (uint8_t)WarmProtected::Role);
    if (!a.saveIfDirty()) {
        TEST_IGNORE_MESSAGE("Filesystem not available in this test environment");
        return;
    }

    // Read the whole v2 file, flip the 4-byte header magic to v1 ("WRM1"), write it back.
    // (CRC covers only the entry bytes, so patching the header magic keeps it valid.)
    std::vector<uint8_t> buf;
    {
        auto f = FSCom.open("/prefs/warm.dat", FILE_O_READ);
        if (!f) {
            TEST_IGNORE_MESSAGE("warm.dat not readable in this environment");
            return;
        }
        buf.resize(f.size());
        f.read(buf.data(), buf.size());
        f.close();
    }
    TEST_ASSERT_TRUE(buf.size() >= 4);
    const uint32_t v1magic = 0x314D5257u; // "WRM1"
    memcpy(buf.data(), &v1magic, sizeof(v1magic));
    {
        auto f = FSCom.open("/prefs/warm.dat", FILE_O_WRITE);
        TEST_ASSERT_TRUE((bool)f);
        f.write(buf.data(), buf.size());
        f.close();
    }

    WarmNodeStore b;
    b.load();
    TEST_ASSERT_TRUE(b.contains(0x900));     // identity survived migration
    TEST_ASSERT_TRUE(b.copyKey(0x900, got)); // public key survived
    TEST_ASSERT_EQUAL_MEMORY(key, got, 32);
    uint8_t role = 0xFF, prot = 0xFF;
    TEST_ASSERT_TRUE(b.lookupMeta(0x900, role, prot));
    TEST_ASSERT_EQUAL(0, role); // last_heard discarded → role/protected reset
    TEST_ASSERT_EQUAL((uint8_t)WarmProtected::None, prot);

    b.clear();
    b.saveIfDirty();
}

// Shrink safety: a warm.dat snapshot recording more entries than this build's
// WARM_NODE_COUNT (e.g. written before a per-platform tier reduction) must be
// rejected cleanly at the header check - load() starts empty instead of reading
// past entries[]. (The nRF52840 raw-flash ring backend has no such cliff: it
// replays records through place(), whose LRU admission keeps the newest.)
void test_ws_load_rejectsOversizedSnapshot()
{
    WarmNodeStore a;
    a.absorb(0xA00, 111, NULL);
    if (!a.saveIfDirty()) {
        TEST_IGNORE_MESSAGE("Filesystem not available in this test environment");
        return;
    }

    // Patch the header's count field (offset 8) to one past capacity.
    std::vector<uint8_t> buf;
    {
        auto f = FSCom.open("/prefs/warm.dat", FILE_O_READ);
        if (!f) {
            TEST_IGNORE_MESSAGE("warm.dat not readable in this environment");
            return;
        }
        buf.resize(f.size());
        f.read(buf.data(), buf.size());
        f.close();
    }
    TEST_ASSERT_TRUE(buf.size() >= 16);
    const uint16_t oversized = (uint16_t)(WARM_NODE_COUNT + 1);
    memcpy(buf.data() + 8, &oversized, sizeof(oversized));
    {
        auto f = FSCom.open("/prefs/warm.dat", FILE_O_WRITE);
        TEST_ASSERT_TRUE((bool)f);
        f.write(buf.data(), buf.size());
        f.close();
    }

    WarmNodeStore b;
    b.load();
    TEST_ASSERT_EQUAL(0, b.count()); // rejected as invalid, started empty

    b.clear();
    b.saveIfDirty();
}

WS_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_ws_absorb_and_copyKey_roundTrip);
    RUN_TEST(test_ws_keylessEntry_hasNoKey);
    RUN_TEST(test_ws_absorb_rejectsNodeNumZero);
    RUN_TEST(test_ws_absorb_updatesExistingEntry);
    RUN_TEST(test_ws_take_removesEntry);
    RUN_TEST(test_ws_keylessCandidate_neverEvictsKeyedEntries);
    RUN_TEST(test_ws_keyedCandidate_evictsOldestKeylessFirst);
    RUN_TEST(test_ws_keyedCandidate_evictsOldestKeyedWhenNoKeyless);
    RUN_TEST(test_ws_meta_roundTrip);
    RUN_TEST(test_ws_remove_and_clear);
    RUN_TEST(test_ws_persistence_roundTrip);
    RUN_TEST(test_ws_v1_migration_discardsLastHeard);
    RUN_TEST(test_ws_load_rejectsOversizedSnapshot);
    exit(UNITY_END());
}

WS_TEST_ENTRY void loop() {}

#else

void setUp(void) {}
void tearDown(void) {}

WS_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

WS_TEST_ENTRY void loop() {}

#endif
