// MessageStore text pool - src/MessageStore.cpp. The pool is a ring with no free list; every test here
// asserts one invariant: a record that is still live reads back exactly the text it was stored with.
#include "MeshTypes.h" // BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "configuration.h"

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)

#include "MessageStore.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Text for message n: a unique tag, then filler up to len bytes
static std::string textFor(uint32_t n, size_t len)
{
    char tag[16];
    snprintf(tag, sizeof(tag), "<%u>", n);
    std::string s = tag;
    while (s.size() < len)
        s += static_cast<char>('a' + (s.size() + n) % 26);
    s.resize(len);
    return s;
}

// Same path as CannedMessageModule and the InkHUD migration: allocate text,
// then push the record
static void addMessage(uint32_t n, size_t len)
{
    std::string t = textFor(n, len);
    StoredMessage sm;
    sm.sender = 0x1000 + n;
    sm.timestamp = n;
    sm.textLength = t.size();
    sm.textOffset = MessageStore::storeText(t.c_str(), t.size());
    messageStore.addLiveMessage(sm);
}

static void assertLiveTextIntact(const char *when)
{
    for (const StoredMessage &m : messageStore.getLiveMessages()) {
        uint32_t n = m.sender - 0x1000;
        std::string expect = textFor(n, m.textLength);
        char why[96];
        snprintf(why, sizeof(why), "%s: record %u reads foreign text", when, n);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expect.c_str(), MessageStore::getText(m), why);
    }
}

void setUp(void)
{
    messageStore.loadFromFlash(); // no file in the sandbox: clears the deque and
                                  // resets the pool
}
void tearDown(void) {}

// A full store of maximum-length messages is exactly one pool; the wrap that follows used to
// overwrite the oldest live record's text while the record stayed on screen.
void test_wrap_never_rewrites_a_live_record()
{
    for (uint32_t n = 1; n <= MAX_MESSAGES_SAVED + 2; n++) {
        addMessage(n, MAX_MESSAGE_SIZE - 1);
        assertLiveTextIntact("after max-length insert");
    }
    TEST_ASSERT_LESS_OR_EQUAL(MAX_MESSAGES_SAVED, messageStore.getLiveMessages().size());
}

// Text allocated without a record (as the InkHUD notification cache once did on every broadcast)
// churns the pool underneath the records. It may evict them; it may not corrupt them.
void test_recordless_allocations_do_not_corrupt_records()
{
    for (uint32_t n = 1; n <= 3; n++)
        addMessage(n, 40);
    std::string filler(200, 'z');
    for (int i = 0; i < 100; i++) {
        MessageStore::storeText(filler.c_str(), filler.size());
        assertLiveTextIntact("after recordless allocation");
    }
}

// Mixed sizes, many laps of the pool
void test_mixed_sizes_over_many_laps()
{
    uint32_t seed = 12345;
    for (uint32_t n = 1; n <= 500; n++) {
        seed = seed * 1103515245u + 12345u;
        size_t len = 1 + (seed >> 8) % (MAX_MESSAGE_SIZE - 1);
        addMessage(n, len);
        assertLiveTextIntact("after mixed insert");
    }
}

// The pool is sized for exactly MAX_MESSAGES_SAVED maximum-length messages; all of them must fit
void test_full_pool_holds_exactly_max_messages()
{
    for (uint32_t n = 1; n <= MAX_MESSAGES_SAVED; n++)
        addMessage(n, MAX_MESSAGE_SIZE - 1);
    TEST_ASSERT_EQUAL(MAX_MESSAGES_SAVED, messageStore.getLiveMessages().size());
    TEST_ASSERT_EQUAL_UINT32(0x1001, messageStore.getLiveMessages().front().sender);
    assertLiveTextIntact("full pool");
}

// One past full: the wrap evicts the oldest record and the newest reads back intact
void test_eviction_keeps_newest_after_wrap()
{
    for (uint32_t n = 1; n <= MAX_MESSAGES_SAVED + 1; n++)
        addMessage(n, MAX_MESSAGE_SIZE - 1);
    const StoredMessage &last = messageStore.getLiveMessages().back();
    TEST_ASSERT_EQUAL_UINT32(0x1000 + MAX_MESSAGES_SAVED + 1, last.sender);
    TEST_ASSERT_EQUAL_STRING(textFor(MAX_MESSAGES_SAVED + 1, MAX_MESSAGE_SIZE - 1).c_str(), MessageStore::getText(last));
    assertLiveTextIntact("after wrap");
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_wrap_never_rewrites_a_live_record);
    RUN_TEST(test_recordless_allocations_do_not_corrupt_records);
    RUN_TEST(test_mixed_sizes_over_many_laps);
    RUN_TEST(test_full_pool_holds_exactly_max_messages);
    RUN_TEST(test_eviction_keeps_newest_after_wrap);
    exit(UNITY_END());
}

void loop() {}

#else

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

void loop() {}

#endif
