#include "configuration.h" // for the extern "C" setup()/loop() the portduino runner links against
#include "modules/Native/SfppFrame.h"

#include <cstring>
#include <pb_encode.h>
#include <unity.h>
#include <vector>

using sfppframe::Reassembler;
using sfppframe::Replay;

namespace
{
constexpr uint32_t kFrom = 0x11111111;
constexpr uint32_t kId = 0x22223333;
constexpr uint32_t kDirect = 0x44445555;

std::vector<uint8_t> cipher(size_t len, uint8_t seed = 0x5A)
{
    std::vector<uint8_t> out(len);
    for (size_t i = 0; i < len; i++)
        out[i] = (uint8_t)(seed + i * 37u + (i >> 3));
    return out;
}

size_t bodySize(const meshtastic_StoreForwardPlusPlus &frame)
{
    size_t size = 0;
    TEST_ASSERT_TRUE(pb_get_encoded_size(&size, &meshtastic_StoreForwardPlusPlus_msg, &frame));
    return size;
}

std::vector<meshtastic_StoreForwardPlusPlus> split(const std::vector<uint8_t> &data, uint32_t to = sfppframe::kBroadcastNodeNum,
                                                   uint32_t heardAgoS = 7, bool originVerified = true)
{
    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    TEST_ASSERT_TRUE(sfppframe::split(data.data(), data.size(), to, kFrom, kId, heardAgoS, originVerified, frames));
    return frames;
}

const uint8_t *keyOf(const meshtastic_StoreForwardPlusPlus &frame)
{
    return frame.message_hash.bytes;
}
} // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Hash contract ---

// Pins the exact bytes the archive and the app must hash: ciphertext, then to/from/id as 32-bit
// little-endian words. A change here silently stops every replay from verifying.
void test_messageHashMatchesTheWireLayout()
{
    const uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
    const uint8_t expected[16] = {0x1f, 0x15, 0xd3, 0x65, 0x6f, 0x2e, 0xd5, 0xcc, 0xc0, 0xa7, 0x5f, 0x0e, 0xe2, 0x33, 0x85, 0x30};
    uint8_t out[sfppframe::kMessageHashBytes];
    sfppframe::messageHash(data, sizeof(data), 0xAABBCCDD, 0x11223344, 0x55667788, out);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, sizeof(expected));
}

// --- Splitter / reassembler round trips ---

void test_singleFrameRoundTrips()
{
    const auto data = cipher(40);
    const auto frames = split(data);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)frames.size());
    TEST_ASSERT_EQUAL_UINT32(0, frames[0].frame);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sfppframe::kMaxFrameBodyBytes, (uint32_t)bodySize(frames[0]));

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_TRUE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_TRUE(replay.ciphertext == data);
    TEST_ASSERT_EQUAL_UINT32(sfppframe::kBroadcastNodeNum, replay.to);
    TEST_ASSERT_EQUAL_UINT32(kFrom, replay.from);
    TEST_ASSERT_EQUAL_UINT32(kId, replay.id);
    TEST_ASSERT_EQUAL_UINT32(7, replay.heardAgoS);
    TEST_ASSERT_TRUE(replay.originVerified);
}

void test_twoFrameRoundTrips()
{
    const auto data = cipher(220);
    const auto frames = split(data);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)frames.size());
    TEST_ASSERT_EQUAL_UINT32(0x0200, frames[0].frame);
    TEST_ASSERT_EQUAL_UINT32(0x0201, frames[1].frame);

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_TRUE(reassembler.offer(frames[1], 1, replay));
    TEST_ASSERT_TRUE(replay.ciphertext == data);
}

void test_fourFrameRoundTrips()
{
    const auto data = cipher(500, 0xC3);
    const auto frames = split(data);
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)frames.size());

    Reassembler reassembler;
    Replay replay;
    for (size_t i = 0; i < frames.size(); i++) {
        const bool last = i + 1 == frames.size();
        TEST_ASSERT_EQUAL_UINT32(last ? 1u : 0u, (uint32_t)reassembler.offer(frames[i], (uint32_t)i, replay));
    }
    TEST_ASSERT_TRUE(replay.ciphertext == data);
}

void test_shuffledOrderCompletes()
{
    const auto data = cipher(500, 0x77);
    const auto frames = split(data);

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(frames[2], 0, replay));
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 1, replay));
    TEST_ASSERT_FALSE(reassembler.offer(frames[3], 2, replay));
    TEST_ASSERT_TRUE(reassembler.offer(frames[1], 3, replay));
    TEST_ASSERT_TRUE(replay.ciphertext == data);
}

void test_duplicateFrameIsIgnored()
{
    const auto data = cipher(220);
    const auto frames = split(data);

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 1, replay));
    TEST_ASSERT_TRUE(reassembler.offer(frames[1], 2, replay));
    TEST_ASSERT_TRUE(replay.ciphertext == data);
}

// Two archives split the same message the same way, so a frame from either completes the set. The
// envelope (heard_ago, verdict) comes from frame 0, which is also what the archive signed.
void test_secondArchiveCompletesTheSet()
{
    const auto data = cipher(220, 0x91);
    const auto fromA = split(data, sfppframe::kBroadcastNodeNum, 7, true);
    const auto fromB = split(data, sfppframe::kBroadcastNodeNum, 99, false);

    TEST_ASSERT_EQUAL_MEMORY(keyOf(fromA[0]), keyOf(fromB[1]), sfppframe::kFrameKeyBytes);
    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(fromA[0], 0, replay));
    TEST_ASSERT_TRUE(reassembler.offer(fromB[1], 1, replay));
    TEST_ASSERT_TRUE(replay.ciphertext == data);
    TEST_ASSERT_EQUAL_UINT32(7, replay.heardAgoS);
    TEST_ASSERT_TRUE(replay.originVerified);
}

void test_directRecipientRoundTrips()
{
    const auto data = cipher(220, 0x42);
    const auto frames = split(data, kDirect);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)frames.size());
    TEST_ASSERT_EQUAL_UINT32(kDirect, frames[0].encapsulated_to);

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_TRUE(reassembler.offer(frames[1], 1, replay));
    TEST_ASSERT_EQUAL_UINT32(kDirect, replay.to);
    TEST_ASSERT_TRUE(replay.ciphertext == data);
}

// --- Rejection and lifecycle ---

// A frame with no ciphertext is malformed; accepting it would let an empty set "complete".
void test_emptyFrameIsRefused()
{
    auto frames = split(cipher(40));
    frames[0].message.size = 0;

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)reassembler.activeSlots());
}

// A spliced or corrupt frame set must never be handed on; the slot is freed so a clean retry works.
void test_corruptSliceIsDiscarded()
{
    const auto data = cipher(220, 0x33);
    auto frames = split(data);
    frames[1].message.bytes[0] ^= 0xFF;

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_FALSE(reassembler.offer(frames[1], 1, replay));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)reassembler.activeSlots());

    const auto clean = split(data, sfppframe::kBroadcastNodeNum, 7, true);
    TEST_ASSERT_FALSE(reassembler.offer(clean[0], 2, replay));
    TEST_ASSERT_TRUE(reassembler.offer(clean[1], 3, replay));
}

// A partial set is dropped at its deadline; a frame arriving after it cannot complete by itself.
void test_expiryDropsPartialSet()
{
    const auto data = cipher(220, 0x24);
    const auto frames = split(data);

    Reassembler reassembler(2, 1000);
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_FALSE(reassembler.offer(frames[1], 1001, replay));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)reassembler.activeSlots());
}

// With two slots, a third key evicts the least recently used one and its partial set is lost.
void test_lruEvictsLeastRecentlyUsed()
{
    const auto a = split(cipher(220, 0xA1));
    const auto b = split(cipher(220, 0xB2));
    const auto c = split(cipher(220, 0xC3));

    Reassembler reassembler(2);
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(a[0], 1, replay));
    TEST_ASSERT_FALSE(reassembler.offer(b[0], 2, replay));
    TEST_ASSERT_FALSE(reassembler.offer(c[0], 3, replay));

    TEST_ASSERT_FALSE(reassembler.hasSlot(keyOf(a[0])));
    TEST_ASSERT_TRUE(reassembler.hasSlot(keyOf(b[0])));
    TEST_ASSERT_TRUE(reassembler.hasSlot(keyOf(c[0])));
}

// The same eviction choice across the millis() wrap: the timestamp comparison would invert here.
void test_lruIsWrapSafeNearUint32Max()
{
    const auto a = split(cipher(220, 0xA1));
    const auto b = split(cipher(220, 0xB2));
    const auto c = split(cipher(220, 0xC3));

    const uint32_t base = 0xFFFFFFF0u;
    Reassembler reassembler(2);
    Replay replay;
    TEST_ASSERT_FALSE(reassembler.offer(a[0], base, replay));      // oldest
    TEST_ASSERT_FALSE(reassembler.offer(b[0], base + 8, replay));  // newer
    TEST_ASSERT_FALSE(reassembler.offer(c[0], base + 20, replay)); // wraps to 4

    TEST_ASSERT_FALSE(reassembler.hasSlot(keyOf(a[0])));
    TEST_ASSERT_TRUE(reassembler.hasSlot(keyOf(b[0])));
    TEST_ASSERT_TRUE(reassembler.hasSlot(keyOf(c[0])));
}

// --- Size bound ---
void test_everyFrameBodyStaysWithin168Bytes()
{
    for (size_t len : {40u, 220u, 500u}) {
        const auto frames = split(cipher(len, (uint8_t)len));
        for (const auto &frame : frames)
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(sfppframe::kMaxFrameBodyBytes, (uint32_t)bodySize(frame));
    }
}

// More frames than the cap must be refused rather than emitted in part.
void test_oversizedMessageIsRefused()
{
    const auto data = cipher(1000, 0xEE);
    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    TEST_ASSERT_FALSE(sfppframe::split(data.data(), data.size(), sfppframe::kBroadcastNodeNum, kFrom, kId, 7, true, frames));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)frames.size());
}

void setup()
{
    UNITY_BEGIN();

    RUN_TEST(test_messageHashMatchesTheWireLayout);

    RUN_TEST(test_singleFrameRoundTrips);
    RUN_TEST(test_twoFrameRoundTrips);
    RUN_TEST(test_fourFrameRoundTrips);
    RUN_TEST(test_shuffledOrderCompletes);
    RUN_TEST(test_duplicateFrameIsIgnored);
    RUN_TEST(test_secondArchiveCompletesTheSet);
    RUN_TEST(test_directRecipientRoundTrips);

    RUN_TEST(test_corruptSliceIsDiscarded);
    RUN_TEST(test_emptyFrameIsRefused);
    RUN_TEST(test_expiryDropsPartialSet);
    RUN_TEST(test_lruEvictsLeastRecentlyUsed);
    RUN_TEST(test_lruIsWrapSafeNearUint32Max);

    RUN_TEST(test_everyFrameBodyStaysWithin168Bytes);
    RUN_TEST(test_oversizedMessageIsRefused);

    exit(UNITY_END());
}

void loop() {}
