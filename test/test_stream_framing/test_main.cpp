#include "MeshTypes.h"
#include "TestUtil.h"
#include "configuration.h"
#include "mesh/MeshService.h"
#include "mesh/StreamAPI.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <unity.h>
#include <vector>

// Framing constants mirrored from StreamAPI.cpp (defined only in that translation unit).
static constexpr uint8_t kStart1 = 0x94;
static constexpr uint8_t kStart2 = 0xc3;
static constexpr size_t kHeaderLen = 4;

/// Input-scripted stream feeding queued bytes through the readStream() polling path.
class InputScriptedStream : public Stream
{
  public:
    /// Report how many queued input bytes remain.
    int available() override { return (int)input.size(); }

    /// Return the next queued byte as an unsigned value, or -1 when drained.
    int read() override
    {
        if (input.empty())
            return -1;
        int value = input.front();
        input.pop_front();
        return value;
    }

    /// Return the next queued byte without consuming it.
    int peek() override { return input.empty() ? -1 : input.front(); }

    /// Accept unlimited output; this suite only exercises the receive side.
    int availableForWrite() override { return std::numeric_limits<int>::max(); }
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *, size_t size) override { return size; }
    void flush() override {}

    /// Queue bytes for the next readStream() poll.
    void feed(const std::vector<uint8_t> &bytes) { input.insert(input.end(), bytes.begin(), bytes.end()); }

    std::deque<uint8_t> input;
};

// The global `service` is installed in setUp() and restored in tearDown() rather than by RAII
// because a failed TEST_ASSERT longjmps out of the test without running destructors, which would
// leave `service` dangling for the rest of the suite. testService is intentionally never freed:
// it stays reachable through the static, so LeakSanitizer does not flag it.
static MeshService *testService = nullptr;
static MeshService *previousService = nullptr;

/// Records every framed ToRadio payload the receive state machine delivers.
class FramingStreamAPIShim : public StreamAPI
{
  public:
    /// Construct the shim over a scripted input stream.
    explicit FramingStreamAPIShim(Stream *stream) : StreamAPI(stream) {}

    /// Keep connection-timeout handling inactive during tests.
    bool checkIsConnected() override { return true; }

    /// Capture one delivered payload instead of running the real PhoneAPI decode.
    bool handleToRadio(const uint8_t *buf, size_t len) override
    {
        deliveries.emplace_back(buf, buf + len);
        return true;
    }

    std::vector<std::vector<uint8_t>> deliveries;
};

/// Wrap a payload in the 0x94C3 big-endian-length stream framing.
static std::vector<uint8_t> makeFrame(const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> frame = {kStart1, kStart2, (uint8_t)(payload.size() >> 8), (uint8_t)(payload.size() & 0xff)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

/// Drive the buffer-fed receive path (SerialModule/native callers) with one burst.
static void feedBufferPath(FramingStreamAPIShim &api, const std::vector<uint8_t> &bytes)
{
    std::vector<uint8_t> copy = bytes; // runOncePart takes a mutable char*
    api.runOncePart(reinterpret_cast<char *>(copy.data()), (uint16_t)copy.size());
}

/// Drive the stream-polling receive path with one burst.
static void feedStreamPath(FramingStreamAPIShim &api, InputScriptedStream &stream, const std::vector<uint8_t> &bytes)
{
    stream.feed(bytes);
    api.runOncePart();
}

/// Assert delivery `index` matches the expected payload, size first so a short delivery is a
/// clean assertion failure rather than an out-of-bounds read.
static void assertDeliveryAt(const FramingStreamAPIShim &api, size_t index, const std::vector<uint8_t> &expected)
{
    TEST_ASSERT_TRUE_MESSAGE(index < api.deliveries.size(), "delivery index out of range");
    TEST_ASSERT_EQUAL_UINT(expected.size(), api.deliveries[index].size());
    if (!expected.empty()) // Unity rejects zero-length array asserts as pointless
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), api.deliveries[index].data(), expected.size());
}

/// Assert the shim recorded exactly one delivery matching the expected payload.
static void assertSingleDelivery(const FramingStreamAPIShim &api, const std::vector<uint8_t> &expected)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, api.deliveries.size(), "expected exactly one handleToRadio delivery");
    assertDeliveryAt(api, 0, expected);
}

/// Verify one well-formed frame off the scripted stream delivers its exact payload once.
void test_stream_single_frame_delivers_exact_payload()
{
    InputScriptedStream stream;
    FramingStreamAPIShim api(&stream);
    std::vector<uint8_t> payload = {0x08, 0x01, 0x2a, 0x00, 0x7f};

    feedStreamPath(api, stream, makeFrame(payload));

    assertSingleDelivery(api, payload);
    TEST_ASSERT_TRUE_MESSAGE(stream.input.empty(), "readStream must drain everything available");
}

/// Verify parser state persists across stream polls split mid-header and mid-payload.
void test_stream_partial_reads_persist_state()
{
    InputScriptedStream stream;
    FramingStreamAPIShim api(&stream);
    std::vector<uint8_t> payload = {0xaa, 0xbb, 0xcc};
    std::vector<uint8_t> frame = makeFrame(payload);

    // First poll sees only 3 of the 4 header bytes.
    feedStreamPath(api, stream, std::vector<uint8_t>(frame.begin(), frame.begin() + 3));
    TEST_ASSERT_EQUAL_UINT(0, api.deliveries.size());

    // Second poll supplies the length byte and part of the payload.
    feedStreamPath(api, stream, std::vector<uint8_t>(frame.begin() + 3, frame.begin() + 5));
    TEST_ASSERT_EQUAL_UINT(0, api.deliveries.size());

    // Final poll completes the payload: exactly one delivery.
    feedStreamPath(api, stream, std::vector<uint8_t>(frame.begin() + 5, frame.end()));
    assertSingleDelivery(api, payload);
}

/// Verify rxPtr persists across buffer-path invocations fed one byte at a time.
void test_buffer_path_one_byte_per_call_persists_state()
{
    InputScriptedStream stream;
    FramingStreamAPIShim api(&stream);
    std::vector<uint8_t> payload = {0x12, 0x34};
    std::vector<uint8_t> frame = makeFrame(payload);

    for (size_t i = 0; i + 1 < frame.size(); i++) {
        feedBufferPath(api, {frame[i]});
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, api.deliveries.size(), "no delivery before the final byte");
    }
    feedBufferPath(api, {frame.back()});

    assertSingleDelivery(api, payload);
}

/// Verify the parser hunts past leading ASCII boot-log garbage to the frame marker.
void test_leading_garbage_resyncs_to_frame()
{
    InputScriptedStream stream;
    FramingStreamAPIShim api(&stream);
    std::vector<uint8_t> payload = {0x55, 0x66};

    const char *bootLog = "INFO  | ??:??:?? 1 Booting\r\n";
    std::vector<uint8_t> burst(bootLog, bootLog + strlen(bootLog));
    std::vector<uint8_t> frame = makeFrame(payload);
    burst.insert(burst.end(), frame.begin(), frame.end());

    feedStreamPath(api, stream, burst);

    assertSingleDelivery(api, payload);
}

/// Verify a header advertising len 513 is rejected and a later frame in the burst still delivers.
void test_bogus_length_rejected_then_next_frame_recovered()
{
    InputScriptedStream stream;
    FramingStreamAPIShim api(&stream);
    std::vector<uint8_t> payload = {0x77};

    // MAX_TO_FROM_RADIO_SIZE is 512, so a big-endian length of 513 must fail header validation.
    std::vector<uint8_t> burst = {kStart1, kStart2, 0x02, 0x01};
    const char *junk = "junk";
    burst.insert(burst.end(), junk, junk + strlen(junk));
    std::vector<uint8_t> frame = makeFrame(payload);
    burst.insert(burst.end(), frame.begin(), frame.end());

    feedBufferPath(api, burst);

    assertSingleDelivery(api, payload);
}

/// Verify a len==512 frame (the exact cap, filling rxBuf to its last byte) is delivered intact
/// on both receive paths.
void test_max_length_frame_accepted_exactly()
{
    std::vector<uint8_t> payload(MAX_TO_FROM_RADIO_SIZE);
    for (size_t i = 0; i < payload.size(); i++)
        payload[i] = (uint8_t)(i & 0xff);
    std::vector<uint8_t> frame = makeFrame(payload);

    // Total frame is 516 bytes == sizeof(rxBuf); ASan in the coverage env guards the bound.
    InputScriptedStream stream;
    FramingStreamAPIShim streamApi(&stream);
    feedStreamPath(streamApi, stream, frame);
    assertSingleDelivery(streamApi, payload);

    // Buffer path, split so the cap is reached with rxPtr state persisted across calls.
    InputScriptedStream unusedStream;
    FramingStreamAPIShim bufferApi(&unusedStream);
    const size_t split = frame.size() / 2;
    feedBufferPath(bufferApi, std::vector<uint8_t>(frame.begin(), frame.begin() + split));
    TEST_ASSERT_EQUAL_UINT(0, bufferApi.deliveries.size());
    feedBufferPath(bufferApi, std::vector<uint8_t>(frame.begin() + split, frame.end()));
    assertSingleDelivery(bufferApi, payload);
}

/// Verify a zero-length payload is a valid frame delivering len 0 on both receive paths.
void test_zero_length_payload_delivers_empty()
{
    std::vector<uint8_t> frame = makeFrame({});
    TEST_ASSERT_EQUAL_UINT(kHeaderLen, frame.size());

    InputScriptedStream stream;
    FramingStreamAPIShim streamApi(&stream);
    feedStreamPath(streamApi, stream, frame);
    assertSingleDelivery(streamApi, {});

    InputScriptedStream unusedStream;
    FramingStreamAPIShim bufferApi(&unusedStream);
    feedBufferPath(bufferApi, frame);
    assertSingleDelivery(bufferApi, {});
}

/// Verify two back-to-back frames in one burst deliver twice, in order, on both paths.
void test_back_to_back_frames_deliver_in_order()
{
    std::vector<uint8_t> first = {0x01, 0x02, 0x03};
    std::vector<uint8_t> second = {0xf0, 0x0d};
    std::vector<uint8_t> burst = makeFrame(first);
    std::vector<uint8_t> secondFrame = makeFrame(second);
    burst.insert(burst.end(), secondFrame.begin(), secondFrame.end());

    InputScriptedStream stream;
    FramingStreamAPIShim streamApi(&stream);
    feedStreamPath(streamApi, stream, burst);
    TEST_ASSERT_EQUAL_UINT(2, streamApi.deliveries.size());
    assertDeliveryAt(streamApi, 0, first);
    assertDeliveryAt(streamApi, 1, second);

    InputScriptedStream unusedStream;
    FramingStreamAPIShim bufferApi(&unusedStream);
    feedBufferPath(bufferApi, burst);
    TEST_ASSERT_EQUAL_UINT(2, bufferApi.deliveries.size());
    assertDeliveryAt(bufferApi, 0, first);
    assertDeliveryAt(bufferApi, 1, second);
}

/// Verify payload bytes >= 0x80 survive the buffer path identically to the stream path.
/// Pins the unsigned read in StreamAPI::handleRecStream(const char *, uint16_t): a plain
/// (signed) char compare treated any high byte - START1 itself is 0x94 - as EOF and
/// silently dropped frames mid-buffer.
void test_high_bytes_in_payload_delivered_on_both_paths()
{
    // Includes the framing bytes themselves mid-payload: length counts them as data.
    std::vector<uint8_t> payload = {0x80, kStart1, kStart2, 0xff, 0x00, 0xfe, 0x7f, 0x81};
    std::vector<uint8_t> frame = makeFrame(payload);

    InputScriptedStream stream;
    FramingStreamAPIShim streamApi(&stream);
    feedStreamPath(streamApi, stream, frame);
    assertSingleDelivery(streamApi, payload);

    InputScriptedStream unusedStream;
    FramingStreamAPIShim bufferApi(&unusedStream);
    feedBufferPath(bufferApi, frame);
    assertSingleDelivery(bufferApi, payload);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(streamApi.deliveries[0].data(), bufferApi.deliveries[0].data(), payload.size());
}

/// A byte that fails START2 is re-tested as START1, so 0x94 0x94 0xc3 ... keeps the frame behind
/// the stray marker instead of consuming its real marker in the reset.
void test_stray_start1_before_frame_still_delivers()
{
    std::vector<uint8_t> payload = {0x42};
    std::vector<uint8_t> frame = makeFrame(payload);
    std::vector<uint8_t> burst = {kStart1}; // stray marker, then the real frame
    burst.insert(burst.end(), frame.begin(), frame.end());

    // The byte that fails START2 is itself START1 here, so the frame behind it must survive.
    InputScriptedStream bufStream;
    FramingStreamAPIShim bufferApi(&bufStream);
    feedBufferPath(bufferApi, burst);
    assertSingleDelivery(bufferApi, payload);

    InputScriptedStream stream;
    FramingStreamAPIShim streamApi(&stream);
    feedStreamPath(streamApi, stream, burst);
    assertSingleDelivery(streamApi, payload);
}

/// A run of stray markers before a frame must not consume it either.
void test_repeated_stray_start1_before_frame_still_delivers()
{
    std::vector<uint8_t> payload = {0x43, 0x44};
    std::vector<uint8_t> frame = makeFrame(payload);
    std::vector<uint8_t> burst = {kStart1, kStart1, kStart1};
    burst.insert(burst.end(), frame.begin(), frame.end());

    InputScriptedStream bufStream;
    FramingStreamAPIShim bufferApi(&bufStream);
    feedBufferPath(bufferApi, burst);
    assertSingleDelivery(bufferApi, payload);

    InputScriptedStream stream;
    FramingStreamAPIShim streamApi(&stream);
    feedStreamPath(streamApi, stream, burst);
    assertSingleDelivery(streamApi, payload);
}

/// START1 followed by a non-START1, non-START2 byte still resyncs on the next real frame.
void test_start1_then_unrelated_byte_resyncs()
{
    std::vector<uint8_t> payload = {0x45};
    std::vector<uint8_t> frame = makeFrame(payload);
    std::vector<uint8_t> burst = {kStart1, 0x00};
    burst.insert(burst.end(), frame.begin(), frame.end());

    InputScriptedStream bufStream;
    FramingStreamAPIShim bufferApi(&bufStream);
    feedBufferPath(bufferApi, burst);
    assertSingleDelivery(bufferApi, payload);

    InputScriptedStream stream;
    FramingStreamAPIShim streamApi(&stream);
    feedStreamPath(streamApi, stream, burst);
    assertSingleDelivery(streamApi, payload);
}

/// Unity per-test setup: install the test MeshService the StreamAPI fixtures expect.
void setUp(void)
{
    previousService = service;
    if (!testService)
        testService = new MeshService();
    service = testService;
}

/// Unity per-test teardown: runs even after an aborted test, so the restore is failure-safe.
void tearDown(void)
{
    service = previousService;
}

/// Initialize the native environment and run the receive-framing suite.
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== Frame delivery ===\n");
    RUN_TEST(test_stream_single_frame_delivers_exact_payload);
    RUN_TEST(test_zero_length_payload_delivers_empty);
    RUN_TEST(test_back_to_back_frames_deliver_in_order);
    RUN_TEST(test_high_bytes_in_payload_delivered_on_both_paths);

    printf("\n=== Partial reads / state persistence ===\n");
    RUN_TEST(test_stream_partial_reads_persist_state);
    RUN_TEST(test_buffer_path_one_byte_per_call_persists_state);
    RUN_TEST(test_max_length_frame_accepted_exactly);

    printf("\n=== Resync and rejection ===\n");
    RUN_TEST(test_leading_garbage_resyncs_to_frame);
    RUN_TEST(test_bogus_length_rejected_then_next_frame_recovered);

    printf("\n=== Stray framing markers ===\n");
    RUN_TEST(test_stray_start1_before_frame_still_delivers);
    RUN_TEST(test_repeated_stray_start1_before_frame_still_delivers);
    RUN_TEST(test_start1_then_unrelated_byte_resyncs);

    exit(UNITY_END());
}

/// Unused Arduino loop required by the native Unity runner.
void loop() {}
