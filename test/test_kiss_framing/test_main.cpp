// Unit tests for kiss::Deframer and kiss::encode() in src/platform/portduino/KissFraming.h - the
// byte framing between meshtasticd's raw modem mode (RawModem.cpp) and its TCP client.
//
// What is pinned:
//   - Deframer::feed() reports a frame only on the FEND that closes a non-empty one, decodes
//     FESC TFEND / FESC TFESC, drops the byte after an invalid escape, ignores bytes before the
//     first FEND, and discards a frame that outgrows its buffer while still recovering the next.
//   - encode() escapes FEND and FESC in the type byte and both data spans, and what it emits
//     round-trips through the deframer byte for byte.
//
// The regression guarded: the KISS client counts on this exact behaviour to stay in sync - a
// frame delivered with a stray leading byte, or a FEND inside a payload passed through unescaped,
// desynchronises every later command and reply on the connection.

// Deliberately does NOT include TestUtil.h: this suite is pure-function (no NodeDB, router,
// sockets or radio), so the harness-wide guards there would assert conditions it cannot reach.
#include "platform/portduino/KissFraming.h"
#include <Arduino.h> // setup()/loop() are the portduino entry points, declared extern "C"
#include <cstdlib>
#include <cstring>
#include <unity.h>
#include <vector>

void setUp(void) {}
void tearDown(void) {}

/// Feeds every byte and returns a copy of each frame the deframer completed, in order.
static std::vector<std::vector<uint8_t>> feedAll(kiss::Deframer &d, const std::vector<uint8_t> &bytes)
{
    std::vector<std::vector<uint8_t>> frames;
    for (uint8_t b : bytes)
        if (d.feed(b))
            frames.emplace_back(d.buf, d.buf + d.len);
    return frames;
}

void test_plain_frame_completes_on_closing_fend()
{
    kiss::Deframer d;
    auto frames = feedAll(d, {0xC0, 0x06, 0x17, 0xC0});
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(2, frames[0].size());
    TEST_ASSERT_EQUAL_HEX8(0x06, frames[0][0]);
    TEST_ASSERT_EQUAL_HEX8(0x17, frames[0][1]);
}

void test_bytes_before_first_fend_ignored()
{
    kiss::Deframer d;
    auto frames = feedAll(d, {0x11, 0x22, 0xC0, 0x00, 0xAA, 0xC0});
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(2, frames[0].size());
    TEST_ASSERT_EQUAL_HEX8(0x00, frames[0][0]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, frames[0][1]);
}

void test_empty_frames_skipped()
{
    kiss::Deframer d;
    // Repeated FENDs between frames are idle markers, not empty frames.
    auto frames = feedAll(d, {0xC0, 0xC0, 0xC0, 0x00, 0x01, 0xC0, 0xC0});
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(2, frames[0].size());
}

void test_back_to_back_frames_share_one_fend()
{
    kiss::Deframer d;
    auto frames = feedAll(d, {0xC0, 0x00, 0x01, 0xC0, 0x00, 0x02, 0xC0});
    TEST_ASSERT_EQUAL(2, frames.size());
    TEST_ASSERT_EQUAL_HEX8(0x01, frames[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, frames[1][1]);
}

void test_escapes_decoded()
{
    kiss::Deframer d;
    auto frames = feedAll(d, {0xC0, 0x00, 0xDB, 0xDC, 0xDB, 0xDD, 0x7E, 0xC0});
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(4, frames[0].size());
    TEST_ASSERT_EQUAL_HEX8(0xC0, frames[0][1]);
    TEST_ASSERT_EQUAL_HEX8(0xDB, frames[0][2]);
    TEST_ASSERT_EQUAL_HEX8(0x7E, frames[0][3]);
}

void test_invalid_escape_drops_only_that_byte()
{
    kiss::Deframer d;
    auto frames = feedAll(d, {0xC0, 0x00, 0xDB, 0x41, 0x42, 0xC0});
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(2, frames[0].size());
    TEST_ASSERT_EQUAL_HEX8(0x42, frames[0][1]);
}

void test_consecutive_fesc_drops_second_and_keeps_next_byte_literal()
{
    // FESC FESC TFEND: the second FESC is an invalid escaped byte and is dropped; TFEND is then a plain 0xDC.
    kiss::Deframer d;
    auto frames = feedAll(d, {0xC0, 0x00, 0xDB, 0xDB, 0xDC, 0xC0});
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(2, frames[0].size());
    TEST_ASSERT_EQUAL_HEX8(0xDC, frames[0][1]);
}

void test_oversize_frame_discarded_and_next_recovered()
{
    kiss::Deframer d;
    std::vector<uint8_t> bytes = {0xC0};
    for (size_t i = 0; i < kiss::Deframer::MAX_FRAME + 1; i++)
        bytes.push_back(0x55);
    bytes.push_back(0xC0);                         // the oversize frame ends here: nothing reported
    bytes.insert(bytes.end(), {0x00, 0x99, 0xC0}); // the next frame is delivered
    auto frames = feedAll(d, bytes);
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(2, frames[0].size());
    TEST_ASSERT_EQUAL_HEX8(0x99, frames[0][1]);
}

void test_max_frame_accepted_exactly()
{
    kiss::Deframer d;
    std::vector<uint8_t> bytes = {0xC0};
    for (size_t i = 0; i < kiss::Deframer::MAX_FRAME; i++)
        bytes.push_back((uint8_t)(i & 0x7F)); // never FEND or FESC
    bytes.push_back(0xC0);
    auto frames = feedAll(d, bytes);
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(kiss::Deframer::MAX_FRAME, frames[0].size());
}

void test_encode_escapes_type_and_both_spans()
{
    std::vector<uint8_t> out;
    const uint8_t data[] = {0xC0, 0x01};
    const uint8_t data2[] = {0xDB};
    kiss::encode(out, 0xC0, data, sizeof(data), data2, sizeof(data2));
    const uint8_t want[] = {0xC0, 0xDB, 0xDC, 0xDB, 0xDC, 0x01, 0xDB, 0xDD, 0xC0};
    TEST_ASSERT_EQUAL(sizeof(want), out.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out.data(), sizeof(want));
}

void test_encode_round_trips_through_deframer()
{
    std::vector<uint8_t> payload;
    for (int i = 0; i < 256; i++)
        payload.push_back((uint8_t)i); // covers FEND and FESC in the data
    std::vector<uint8_t> wire;
    kiss::encode(wire, 0x00, payload.data(), payload.size());

    kiss::Deframer d;
    auto frames = feedAll(d, wire);
    TEST_ASSERT_EQUAL(1, frames.size());
    TEST_ASSERT_EQUAL(payload.size() + 1, frames[0].size());
    TEST_ASSERT_EQUAL_HEX8(0x00, frames[0][0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload.data(), frames[0].data() + 1, payload.size());
}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_plain_frame_completes_on_closing_fend);
    RUN_TEST(test_bytes_before_first_fend_ignored);
    RUN_TEST(test_empty_frames_skipped);
    RUN_TEST(test_back_to_back_frames_share_one_fend);
    RUN_TEST(test_escapes_decoded);
    RUN_TEST(test_invalid_escape_drops_only_that_byte);
    RUN_TEST(test_consecutive_fesc_drops_second_and_keeps_next_byte_literal);
    RUN_TEST(test_oversize_frame_discarded_and_next_recovered);
    RUN_TEST(test_max_frame_accepted_exactly);
    RUN_TEST(test_encode_escapes_type_and_both_spans);
    RUN_TEST(test_encode_round_trips_through_deframer);
    exit(UNITY_END());
}

void loop() {}
