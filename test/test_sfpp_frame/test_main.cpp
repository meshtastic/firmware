/*
 * Framed replay: the message hash key, split at the 168-byte signable body bound, and all-or-nothing
 * reassembly. Hash-keyed frames let two archives complete one set and make duplicates harmless;
 * the body bound is what keeps a broadcast frame inside an XEdDSA signature.
 *
 * The regression guarded: a change to the hash field order, the frame cap, the corrupt-slice or
 * expiry rejection, or the unsigned LRU age would silently break recovery while the other suites
 * stayed green, because no other suite exercises the splitter and the slot pool together.
 */

#include "configuration.h" // for the extern "C" setup()/loop() the portduino runner links against
#include "modules/Native/SfppFrame.h"
#include "platform/portduino/PortduinoGlue.h"

#include <cstdio>
#include <cstring>
#include <pb_encode.h>
#include <string>
#include <unistd.h>
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

// Walks the encoded top-level fields and reports whether `field` appears on the wire.
bool wireHasField(const meshtastic_StoreForwardPlusPlus &frame, uint32_t field)
{
    uint8_t buf[512];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE(pb_encode(&stream, &meshtastic_StoreForwardPlusPlus_msg, &frame));

    size_t i = 0;
    while (i < stream.bytes_written) {
        uint64_t key = 0;
        unsigned shift = 0;
        while (i < stream.bytes_written && (buf[i] & 0x80)) {
            key |= (uint64_t)(buf[i] & 0x7F) << shift;
            shift += 7;
            i++;
        }
        if (i >= stream.bytes_written)
            return false;
        key |= (uint64_t)buf[i++] << shift;
        const uint32_t number = (uint32_t)(key >> 3);
        const uint32_t wireType = (uint32_t)(key & 7);
        if (number == field)
            return true;

        if (wireType == 0) {
            while (i < stream.bytes_written && (buf[i++] & 0x80)) {
            }
        } else if (wireType == 1) {
            i += 8;
        } else if (wireType == 2) {
            uint64_t len = 0;
            shift = 0;
            while (i < stream.bytes_written && (buf[i] & 0x80)) {
                len |= (uint64_t)(buf[i] & 0x7F) << shift;
                shift += 7;
                i++;
            }
            if (i >= stream.bytes_written)
                return false;
            len |= (uint64_t)buf[i++] << shift;
            i += (size_t)len;
        } else if (wireType == 5) {
            i += 4;
        } else {
            return false;
        }
    }
    return false;
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

bool loadSfppYaml(const std::string &body)
{
    char path[] = "/tmp/meshtastic-sfpp-frame-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0)
        return false;
    const ssize_t written = write(fd, body.data(), body.size());
    close(fd);
    const bool loaded = written == (ssize_t)body.size() && loadConfig(path);
    unlink(path);
    return loaded;
}
} // namespace

void setUp(void)
{
    portduino_config.sfpp_max_ids_per_frame = 40;
    portduino_config.sfpp_item_body_max = 168;
}
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

// --- Age minifloat ---

// Exact whole seconds to 17 minutes, and a code that only grows with the age it names.
void test_age_minifloat_is_monotonic_and_exact_to_seventeen_minutes()
{
    for (uint32_t age = 0; age <= 1023; age++)
        TEST_ASSERT_EQUAL_UINT32(age, sfppframe::decodeAge14(sfppframe::encodeAge14(age)));

    uint16_t previousCode = 0;
    uint32_t previousAge = 0;
    for (uint32_t age = 0; age <= 400u * 24u * 60u * 60u; age += 997) {
        const uint16_t code = sfppframe::encodeAge14(age);
        const uint32_t decoded = sfppframe::decodeAge14(code);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT16(previousCode, code);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(previousAge, decoded);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(age, decoded);
        previousCode = code;
        previousAge = decoded;
    }
}

// The quantisation is under three minutes at 48 hours.
void test_age_minifloat_is_within_three_minutes_at_48_hours()
{
    const uint32_t age = 48u * 60u * 60u;
    const uint32_t decoded = sfppframe::decodeAge14(sfppframe::encodeAge14(age));
    const uint32_t error = decoded > age ? decoded - age : age - decoded;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(3u * 60u, error);
}

// The largest code covers 388 days, and a larger age saturates rather than wrapping.
void test_age_minifloat_covers_388_days()
{
    const uint32_t maxAge = sfppframe::decodeAge14(0x3FFF);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(387u * 24u * 60u * 60u, maxAge);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(388u * 24u * 60u * 60u, maxAge);
    TEST_ASSERT_EQUAL_UINT16(0x3FFF, sfppframe::encodeAge14(388u * 24u * 60u * 60u));
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

// --- Age on the wire ---

// The item frame carries the age in field 21 and reassembly decodes it back to seconds.
void test_item_frame_reads_age_from_field_21()
{
    const auto frames = split(cipher(40), sfppframe::kBroadcastNodeNum, 12345);
    TEST_ASSERT_EQUAL_UINT32(sfppframe::encodeAge14(12345), frames[0].age);

    Reassembler reassembler;
    Replay replay;
    TEST_ASSERT_TRUE(reassembler.offer(frames[0], 0, replay));
    TEST_ASSERT_EQUAL_UINT32(sfppframe::decodeAge14((uint16_t)frames[0].age), replay.heardAgoS);
}

// The old seconds field 17 is gone from the wire, not merely left unset.
void test_item_frame_does_not_populate_heard_ago_s()
{
    const auto frames = split(cipher(40), sfppframe::kBroadcastNodeNum, 12345);
    TEST_ASSERT_TRUE(wireHasField(frames[0], meshtastic_StoreForwardPlusPlus_age_tag));
    TEST_ASSERT_FALSE(wireHasField(frames[0], 17));
}

// Item frames name their schema version; a frame from an older or unknown one is discarded.
void test_frame_version_three_is_required()
{
    const auto frames = split(cipher(40));
    TEST_ASSERT_EQUAL_UINT32(sfppframe::kFrameVersion, frames[0].sr_version);

    Reassembler reassembler;
    Replay replay;
    meshtastic_StoreForwardPlusPlus old = frames[0];
    old.sr_version = 2;
    TEST_ASSERT_FALSE(reassembler.offer(old, 0, replay));
    old.sr_version = 0;
    TEST_ASSERT_FALSE(reassembler.offer(old, 0, replay));
    TEST_ASSERT_TRUE(reassembler.offer(frames[0], 0, replay));
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

// --- Configurable frame limits ---

// The default config is the old 168-byte split, so each length produces the same frame fields,
// body sizes and slice sizes as the fixed-bound splitter did.
void test_default_frames_change_only_by_the_age_encoding()
{
    struct Golden {
        size_t len;
        size_t count;
        uint32_t frame[4];
        size_t body[4];
        size_t size[4];
    };
    const Golden goldens[] = {
        {40, 1, {0}, {74}, {40}},
        {220, 2, {0x0200, 0x0201}, {167, 112}, {128, 92}},
        {500, 4, {0x0400, 0x0401, 0x0402, 0x0403}, {167, 168, 168, 98}, {128, 147, 147, 78}},
    };
    for (const Golden &g : goldens) {
        const auto frames = split(cipher(g.len, (uint8_t)g.len));
        TEST_ASSERT_EQUAL_UINT32(g.count, (uint32_t)frames.size());
        for (size_t i = 0; i < g.count; i++) {
            TEST_ASSERT_EQUAL_UINT32(g.frame[i], frames[i].frame);
            TEST_ASSERT_EQUAL_UINT32(g.body[i], (uint32_t)bodySize(frames[i]));
            TEST_ASSERT_EQUAL_UINT32(g.size[i], (uint32_t)frames[i].message.size);
        }
    }
}

// A smaller item body limit splits a 100-byte object into more frames, each inside the limit, and
// the set still reassembles to the same bytes.
void test_itemBodyMaxSplitsAndReassembles()
{
    TEST_ASSERT_TRUE(loadSfppYaml("StoreAndForward:\n  Enabled: true\n  ItemBodyMax: 64\n"));

    const auto data = cipher(100, 0x6B);
    const auto frames = split(data);
    TEST_ASSERT_GREATER_THAN_UINT32(1, (uint32_t)frames.size());
    for (const auto &frame : frames)
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(64, (uint32_t)bodySize(frame));

    Reassembler reassembler;
    Replay replay;
    for (size_t i = 0; i < frames.size(); i++) {
        const bool last = i + 1 == frames.size();
        TEST_ASSERT_EQUAL_UINT32(last ? 1u : 0u, (uint32_t)reassembler.offer(frames[i], (uint32_t)i, replay));
    }
    TEST_ASSERT_TRUE(replay.ciphertext == data);
}

void setup()
{
    UNITY_BEGIN();

    RUN_TEST(test_messageHashMatchesTheWireLayout);

    RUN_TEST(test_age_minifloat_is_monotonic_and_exact_to_seventeen_minutes);
    RUN_TEST(test_age_minifloat_is_within_three_minutes_at_48_hours);
    RUN_TEST(test_age_minifloat_covers_388_days);

    RUN_TEST(test_singleFrameRoundTrips);
    RUN_TEST(test_twoFrameRoundTrips);
    RUN_TEST(test_fourFrameRoundTrips);
    RUN_TEST(test_shuffledOrderCompletes);
    RUN_TEST(test_duplicateFrameIsIgnored);
    RUN_TEST(test_secondArchiveCompletesTheSet);
    RUN_TEST(test_directRecipientRoundTrips);
    RUN_TEST(test_item_frame_reads_age_from_field_21);
    RUN_TEST(test_item_frame_does_not_populate_heard_ago_s);
    RUN_TEST(test_frame_version_three_is_required);

    RUN_TEST(test_corruptSliceIsDiscarded);
    RUN_TEST(test_emptyFrameIsRefused);
    RUN_TEST(test_expiryDropsPartialSet);
    RUN_TEST(test_lruEvictsLeastRecentlyUsed);
    RUN_TEST(test_lruIsWrapSafeNearUint32Max);

    RUN_TEST(test_everyFrameBodyStaysWithin168Bytes);
    RUN_TEST(test_oversizedMessageIsRefused);

    RUN_TEST(test_default_frames_change_only_by_the_age_encoding);
    RUN_TEST(test_itemBodyMaxSplitsAndReassembles);

    exit(UNITY_END());
}

void loop() {}
