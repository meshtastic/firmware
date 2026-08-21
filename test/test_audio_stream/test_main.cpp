#include "TestUtil.h"
#include "configuration.h"
#include "modules/Audio/AudioStream.h"
#include <unity.h>
#include <vector>

static constexpr NodeNum ALICE = 0x11111111;
static constexpr NodeNum BOB = 0x22222222;
static constexpr uint8_t SESSION = 0x5a;
static constexpr uint8_t MODE = AUDIO_C2_700C;

struct Emitted {
    uint8_t first; // payload tag
    uint8_t mode;
    bool conceal;
};
static std::vector<Emitted> emitted;

static void onEmit(void *, const uint8_t *frames, size_t len, uint8_t mode, bool conceal)
{
    emitted.push_back({len ? frames[0] : (uint8_t)0, mode, conceal});
}

static AudioStreamAssembler asm_;

void setUp()
{
    emitted.clear();
    asm_.begin(onEmit, nullptr);
    asm_.setPrimeHoldMs(0);
}
void tearDown() {}

// Payload bytes are all `tag`, so emissions are identifiable.
static void feed(NodeNum from, uint16_t seq, uint8_t tag, uint32_t nowMs, bool last = false, uint8_t session = SESSION,
                 uint8_t mode = MODE)
{
    uint8_t buf[AUDIO_HEADER_LEN + 8];
    AudioHeader h{AUDIO_MAGIC, mode, session, (uint8_t)(last ? 1 : 0), seq};
    memcpy(buf, &h, sizeof(h));
    memset(buf + sizeof(h), tag, 8);
    asm_.accept(from, buf, sizeof(buf), nowMs);
}

static void drain(uint32_t nowMs, int steps = 8)
{
    for (int i = 0; i < steps; i++)
        asm_.tick(nowMs, true);
}

static void test_in_order_plays_in_order()
{
    for (uint16_t i = 0; i < 4; i++)
        feed(ALICE, i, (uint8_t)(10 + i), 100);
    drain(100);
    TEST_ASSERT_EQUAL_INT(4, (int)emitted.size());
    for (int i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_UINT8(10 + i, emitted[i].first);
}

// Relayed and direct copies arrive out of order.
static void test_reversed_is_reordered()
{
    feed(ALICE, 3, 13, 100);
    feed(ALICE, 2, 12, 100);
    feed(ALICE, 1, 11, 100);
    feed(ALICE, 0, 10, 100);
    drain(100);
    TEST_ASSERT_EQUAL_INT(4, (int)emitted.size());
    for (int i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_UINT8(10 + i, emitted[i].first);
}

static void test_duplicate_plays_once()
{
    feed(ALICE, 0, 10, 100);
    feed(ALICE, 0, 99, 100); // same seq, different payload
    feed(ALICE, 1, 11, 100);
    drain(100);
    TEST_ASSERT_EQUAL_INT(2, (int)emitted.size());
    TEST_ASSERT_EQUAL_UINT8(10, emitted[0].first);
    TEST_ASSERT_EQUAL_UINT8(11, emitted[1].first);
}

// Must not rewind playout.
static void test_late_copy_is_dropped()
{
    feed(ALICE, 0, 10, 100);
    feed(ALICE, 1, 11, 100);
    drain(100);
    const size_t before = emitted.size();
    feed(ALICE, 0, 10, 200);
    drain(200);
    TEST_ASSERT_EQUAL_INT((int)before, (int)emitted.size());
}

static void test_gap_conceals_and_advances()
{
    feed(ALICE, 0, 10, 100);
    feed(ALICE, 2, 12, 100); // 1 never arrives
    drain(100);
    TEST_ASSERT_EQUAL_INT(3, (int)emitted.size());
    TEST_ASSERT_EQUAL_UINT8(10, emitted[0].first);
    TEST_ASSERT_TRUE(emitted[1].conceal);
    TEST_ASSERT_EQUAL_UINT8(10, emitted[1].first);
    TEST_ASSERT_EQUAL_UINT8(12, emitted[2].first);
}

static void test_last_flag_ends_stream()
{
    feed(ALICE, 0, 10, 100);
    feed(ALICE, 1, 11, 100, true);
    drain(100);
    TEST_ASSERT_EQUAL_INT(2, (int)emitted.size());
    TEST_ASSERT_FALSE(asm_.isActive());
}

// Half duplex: one stream at a time. The second is queued, never interleaved.
static void test_second_talker_not_interleaved()
{
    feed(ALICE, 0, 10, 100);
    feed(BOB, 0, 77, 100, false, 0x33);
    feed(ALICE, 1, 11, 100);
    drain(100);
    for (auto &e : emitted)
        TEST_ASSERT_NOT_EQUAL_UINT8(77, e.first);
}

// Fresh session from the same node, after the previous stream ended.
static void test_new_session_after_last_is_accepted()
{
    feed(ALICE, 0, 10, 100, true);
    drain(100);
    TEST_ASSERT_FALSE(asm_.isActive());
    TEST_ASSERT_FALSE(asm_.hasQueued());
    feed(ALICE, 0, 20, 200, false, 0x77);
    drain(200);
    bool sawNew = false;
    for (auto &e : emitted)
        if (e.first == 20)
            sawNew = true;
    TEST_ASSERT_TRUE(sawNew);
}

static void test_timeout_ends_stream()
{
    feed(ALICE, 0, 10, 100);
    drain(100);
    asm_.tick(100 + AUDIO_IDLE_MAX_MS + 1, true);
    TEST_ASSERT_FALSE(asm_.isActive());
}

// 5 s was 1.7 packet intervals at 450, so one lost packet ended a live stream.
static void test_idle_timeout_scales_with_mode()
{
    asm_.setPacketMs(3000); // 450
    feed(ALICE, 0, 10, 100);
    drain(100);
    asm_.tick(100 + 12000, true); // would have expired under the old 5 s
    TEST_ASSERT_TRUE(asm_.isActive());
    asm_.tick(100 + AUDIO_IDLE_MAX_MS + 1, true);
    TEST_ASSERT_FALSE(asm_.isActive());
    asm_.setPacketMs(2240);
}

// A second sender is held, then played once the first sends `last`.
static void test_second_sender_is_queued_then_played()
{
    feed(ALICE, 0, 10, 100);
    feed(BOB, 0, 77, 100, false, 0x33);
    feed(BOB, 1, 78, 100, true, 0x33);
    TEST_ASSERT_TRUE(asm_.hasQueued());

    feed(ALICE, 1, 11, 100, true);
    drain(100);

    bool sawAlice = false, sawBob = false;
    size_t firstBob = 0;
    for (size_t i = 0; i < emitted.size(); i++) {
        if (emitted[i].first == 10 || emitted[i].first == 11)
            sawAlice = true;
        if (emitted[i].first == 77 && !sawBob) {
            sawBob = true;
            firstBob = i;
        }
    }
    TEST_ASSERT_TRUE(sawAlice);
    TEST_ASSERT_TRUE(sawBob);
    // Bob must not start before Alice has finished.
    TEST_ASSERT_TRUE(emitted[firstBob - 1].first == 10 || emitted[firstBob - 1].first == 11);
}

// Only one sender is held; a third is dropped rather than interleaved.
static void test_third_sender_not_queued()
{
    feed(ALICE, 0, 10, 100);
    feed(BOB, 0, 77, 100, false, 0x33);
    feed(0x33333333, 0, 88, 100, false, 0x44);
    feed(ALICE, 1, 11, 100, true);
    drain(100, 16);
    for (auto &e : emitted)
        TEST_ASSERT_NOT_EQUAL_UINT8(88, e.first);
}

// 3200 wraps seq inside minutes of talking.
static void test_seq_wraps_at_65535()
{
    feed(ALICE, 65534, 10, 100);
    feed(ALICE, 65535, 11, 100);
    feed(ALICE, 0, 12, 100);
    feed(ALICE, 1, 13, 100);
    drain(100);
    TEST_ASSERT_EQUAL_INT(4, (int)emitted.size());
    for (int i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_UINT8(10 + i, emitted[i].first);
}

// The receiver picks its decoder from the packet, not from local config.
static void test_mode_travels_with_the_packet()
{
    feed(ALICE, 0, 10, 100, false, SESSION, AUDIO_C2_450);
    drain(100);
    TEST_ASSERT_EQUAL_INT(1, (int)emitted.size());
    TEST_ASSERT_EQUAL_UINT8(AUDIO_C2_450, emitted[0].mode);
}

static void test_rejects_foreign_payload()
{
    uint8_t buf[16] = {0};
    buf[0] = 0x00; // wrong magic
    TEST_ASSERT_FALSE(asm_.accept(ALICE, buf, sizeof(buf), 100));
    TEST_ASSERT_FALSE(asm_.accept(ALICE, buf, 2, 100)); // shorter than the header
}

// Gap at the head with a full ring must not deadlock.
static void test_ring_full_forces_progress()
{
    feed(ALICE, 1, 11, 100); // seq 0 missing
    feed(ALICE, 2, 12, 100);
    feed(ALICE, 3, 13, 100);
    feed(ALICE, 4, 14, 100);
    feed(ALICE, 5, 15, 100);
    drain(100);
    TEST_ASSERT_TRUE(emitted.size() > 0);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_in_order_plays_in_order);
    RUN_TEST(test_reversed_is_reordered);
    RUN_TEST(test_duplicate_plays_once);
    RUN_TEST(test_late_copy_is_dropped);
    RUN_TEST(test_gap_conceals_and_advances);
    RUN_TEST(test_last_flag_ends_stream);
    RUN_TEST(test_second_talker_not_interleaved);
    RUN_TEST(test_new_session_after_last_is_accepted);
    RUN_TEST(test_timeout_ends_stream);
    RUN_TEST(test_idle_timeout_scales_with_mode);
    RUN_TEST(test_second_sender_is_queued_then_played);
    RUN_TEST(test_third_sender_not_queued);
    RUN_TEST(test_seq_wraps_at_65535);
    RUN_TEST(test_mode_travels_with_the_packet);
    RUN_TEST(test_rejects_foreign_payload);
    RUN_TEST(test_ring_full_forces_progress);
    exit(UNITY_END());
}

void loop() {}
