#include "MeshTypes.h"
#include "SerialConsole.h"
#include "TestUtil.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/MeshService.h"
#include "mesh/StreamAPI.h"
#include "mesh/StreamFrameWriter.h"
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <deque>
#include <limits>
#include <unity.h>
#include <vector>

class ScriptedStream : public Stream
{
  public:
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    int availableForWrite() override { return availableCapacity; }

    size_t write(uint8_t value) override { return write(&value, 1); }

    size_t write(const uint8_t *buffer, size_t size) override
    {
        requestedLengths.push_back(size);
        size_t quota = size;
        if (!writeQuotas.empty()) {
            quota = writeQuotas.front();
            writeQuotas.pop_front();
        }
        size_t accepted = std::min(quota, size);
        output.insert(output.end(), buffer, buffer + accepted);
        return accepted;
    }

    void flush() override { flushCount++; }

    void queueWrite(size_t quota) { writeQuotas.push_back(quota); }

    int availableCapacity = std::numeric_limits<int>::max();
    unsigned flushCount = 0;
    std::deque<size_t> writeQuotas;
    std::vector<size_t> requestedLengths;
    std::vector<uint8_t> output;
};

class RecordingPrint : public Print
{
  public:
    size_t write(uint8_t value) override
    {
        output.push_back(value);
        return 1;
    }

    std::vector<uint8_t> output;
};

class ScopedMeshService
{
  public:
    ScopedMeshService() : previous(service) { service = &instance; }
    ~ScopedMeshService() { service = previous; }

  private:
    MeshService instance;
    MeshService *previous;
};

class StreamAPITestShim : public StreamAPI
{
  public:
    explicit StreamAPITestShim(Stream *stream) : StreamAPI(stream) {}

    bool checkIsConnected() override { return true; }

    bool writeBaseFrame(uint8_t *buf, size_t len, bool bestEffort = false) { return StreamAPI::writeFrame(buf, len, bestEffort); }

    bool finishReady = true;
    bool allowWrite = true;
    unsigned finishCalls = 0;
    unsigned frameWriteCalls = 0;
    unsigned failureCalls = 0;
    size_t failedFrameLen = 0;
    size_t failedWrittenLen = 0;
    std::vector<uint8_t> capturedPayload;

  protected:
    bool finishPendingFrame() override
    {
        finishCalls++;
        return finishReady;
    }

    bool canWriteFrame(size_t) override { return allowWrite; }

    void onFrameWriteFailed(size_t frameLen, size_t writtenLen) override
    {
        failureCalls++;
        failedFrameLen = frameLen;
        failedWrittenLen = writtenLen;
    }

    bool writeFrame(uint8_t *buf, size_t len, bool bestEffort) override
    {
        (void)bestEffort;
        frameWriteCalls++;
        capturedPayload.assign(buf + 4, buf + 4 + len);
        return false;
    }
};

class LogHookStreamAPI : public StreamAPI
{
  public:
    explicit LogHookStreamAPI(Stream *stream) : StreamAPI(stream) {}

    bool checkIsConnected() override { return true; }

    void emitTestLog(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        emitLogRecord(meshtastic_LogRecord_Level_INFO, "test", format, args);
        va_end(args);
    }

    bool allowLogEncoding = false;
    unsigned frameWriteCalls = 0;
    bool lastBestEffort = false;

  protected:
    bool canEncodeLogRecord() override { return allowLogEncoding; }

    bool writeFrame(uint8_t *, size_t, bool bestEffort) override
    {
        frameWriteCalls++;
        lastBestEffort = bestEffort;
        return true;
    }
};

static void assertBytesEqual(const std::vector<uint8_t> &expected, const std::vector<uint8_t> &actual)
{
    TEST_ASSERT_EQUAL_UINT(expected.size(), actual.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), actual.data(), expected.size());
}

void test_frame_writer_continues_only_unwritten_tail()
{
    ScriptedStream stream;
    StreamFrameWriter writer;
    std::vector<uint8_t> frame = {0x94, 0xc3, 0x00, 0x06, 1, 2, 3, 4, 5, 6};
    stream.queueWrite(3);
    stream.queueWrite(2);
    stream.queueWrite(frame.size());

    TEST_ASSERT_FALSE(writer.writeFrame(stream, frame.data(), frame.size(), false));
    TEST_ASSERT_FALSE(writer.isIdle());
    TEST_ASSERT_FALSE(writer.finishPendingFrame(stream));
    TEST_ASSERT_FALSE(writer.isIdle());
    TEST_ASSERT_TRUE(writer.finishPendingFrame(stream));
    TEST_ASSERT_TRUE(writer.isIdle());

    std::vector<size_t> expectedRequests = {10, 7, 5};
    TEST_ASSERT_EQUAL_UINT(expectedRequests.size(), stream.requestedLengths.size());
    TEST_ASSERT_EQUAL_UINT64_ARRAY(expectedRequests.data(), stream.requestedLengths.data(), expectedRequests.size());
    assertBytesEqual(frame, stream.output);
    TEST_ASSERT_EQUAL_UINT(0, stream.flushCount);
}

void test_frame_writer_defers_main_behind_partial_log()
{
    ScriptedStream stream;
    StreamFrameWriter writer;
    std::vector<uint8_t> logFrame = {0x94, 0xc3, 0x00, 0x02, 0xa1, 0xa2};
    std::vector<uint8_t> mainFrame = {0x94, 0xc3, 0x00, 0x03, 0xb1, 0xb2, 0xb3};
    stream.queueWrite(2);
    stream.queueWrite(0);
    stream.queueWrite(logFrame.size());
    stream.queueWrite(mainFrame.size());

    TEST_ASSERT_FALSE(writer.writeFrame(stream, logFrame.data(), logFrame.size(), true));
    TEST_ASSERT_FALSE(writer.isIdle());
    TEST_ASSERT_FALSE(writer.writeFrame(stream, mainFrame.data(), mainFrame.size(), false));
    TEST_ASSERT_FALSE(writer.isIdle());
    TEST_ASSERT_FALSE(writer.finishPendingFrame(stream));
    TEST_ASSERT_FALSE(writer.isIdle());
    TEST_ASSERT_TRUE(writer.finishPendingFrame(stream));
    TEST_ASSERT_TRUE(writer.isIdle());

    std::vector<uint8_t> expected = logFrame;
    expected.insert(expected.end(), mainFrame.begin(), mainFrame.end());
    assertBytesEqual(expected, stream.output);
    TEST_ASSERT_EQUAL_UINT(4, stream.requestedLengths.size());
    TEST_ASSERT_EQUAL_UINT(0, stream.flushCount);
}

void test_frame_writer_rejects_best_effort_without_full_capacity()
{
    ScriptedStream stream;
    StreamFrameWriter writer;
    std::vector<uint8_t> frame = {0x94, 0xc3, 0x00, 0x02, 1, 2};
    stream.availableCapacity = frame.size() - 1;

    TEST_ASSERT_FALSE(writer.writeFrame(stream, frame.data(), frame.size(), true));
    TEST_ASSERT_TRUE(writer.isIdle());
    TEST_ASSERT_EQUAL_UINT(0, stream.requestedLengths.size());

    stream.availableCapacity = frame.size();
    TEST_ASSERT_TRUE(writer.writeFrame(stream, frame.data(), frame.size(), true));
    TEST_ASSERT_TRUE(writer.isIdle());
    TEST_ASSERT_EQUAL_UINT(1, stream.requestedLengths.size());
    assertBytesEqual(frame, stream.output);
}

void test_frame_writer_zero_progress_is_one_bounded_attempt()
{
    ScriptedStream stream;
    StreamFrameWriter writer;
    std::vector<uint8_t> frame = {0x94, 0xc3, 0x00, 0x02, 1, 2};
    stream.queueWrite(1);
    stream.queueWrite(0);
    stream.queueWrite(0);
    stream.queueWrite(frame.size());

    TEST_ASSERT_FALSE(writer.writeFrame(stream, frame.data(), frame.size(), false));
    TEST_ASSERT_EQUAL_UINT(1, stream.requestedLengths.size());
    TEST_ASSERT_FALSE(writer.finishPendingFrame(stream));
    TEST_ASSERT_EQUAL_UINT(2, stream.requestedLengths.size());
    TEST_ASSERT_FALSE(writer.finishPendingFrame(stream));
    TEST_ASSERT_EQUAL_UINT(3, stream.requestedLengths.size());
    TEST_ASSERT_TRUE(writer.finishPendingFrame(stream));
    TEST_ASSERT_EQUAL_UINT(4, stream.requestedLengths.size());
    assertBytesEqual(frame, stream.output);
}

void test_stream_api_full_write_frames_and_flushes()
{
    ScopedMeshService scopedService;
    ScriptedStream stream;
    StreamAPITestShim api(&stream);
    uint8_t frame[7] = {0, 0, 0, 0, 0x11, 0x22, 0x33};

    TEST_ASSERT_TRUE(api.writeBaseFrame(frame, 3));

    std::vector<uint8_t> expected = {0x94, 0xc3, 0x00, 0x03, 0x11, 0x22, 0x33};
    assertBytesEqual(expected, stream.output);
    TEST_ASSERT_EQUAL_UINT(1, stream.requestedLengths.size());
    TEST_ASSERT_EQUAL_UINT(1, stream.flushCount);
    TEST_ASSERT_EQUAL_UINT(0, api.failureCalls);
}

void test_stream_api_short_write_reports_failure_without_flush()
{
    ScopedMeshService scopedService;
    ScriptedStream stream;
    StreamAPITestShim api(&stream);
    uint8_t frame[7] = {0, 0, 0, 0, 0x11, 0x22, 0x33};
    stream.queueWrite(5);

    TEST_ASSERT_FALSE(api.writeBaseFrame(frame, 3));

    TEST_ASSERT_EQUAL_UINT(1, stream.requestedLengths.size());
    TEST_ASSERT_EQUAL_UINT(0, stream.flushCount);
    TEST_ASSERT_EQUAL_UINT(1, api.failureCalls);
    TEST_ASSERT_EQUAL_UINT(7, api.failedFrameLen);
    TEST_ASSERT_EQUAL_UINT(5, api.failedWrittenLen);
}

void test_stream_api_finishes_pending_before_advancing_phone_api()
{
    ScopedMeshService scopedService;
    ScriptedStream stream;
    StreamAPITestShim api(&stream);
    api.sendConfigComplete();
    api.sendNotification(meshtastic_LogRecord_Level_WARNING, 42, "still queued");
    api.finishReady = false;

    api.runOncePart(nullptr, 0);
    TEST_ASSERT_EQUAL_UINT(1, api.finishCalls);
    TEST_ASSERT_EQUAL_UINT(0, api.frameWriteCalls);

    api.finishReady = true;
    api.runOncePart(nullptr, 0);
    TEST_ASSERT_EQUAL_UINT(2, api.finishCalls);
    TEST_ASSERT_EQUAL_UINT(1, api.frameWriteCalls);

    meshtastic_FromRadio decoded = meshtastic_FromRadio_init_zero;
    TEST_ASSERT_TRUE(
        pb_decode_from_bytes(api.capturedPayload.data(), api.capturedPayload.size(), &meshtastic_FromRadio_msg, &decoded));
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_clientNotification_tag, decoded.which_payload_variant);
    TEST_ASSERT_EQUAL_UINT32(42, decoded.clientNotification.reply_id);
}

void test_stream_api_gates_logs_and_marks_them_best_effort()
{
    ScopedMeshService scopedService;
    ScriptedStream stream;
    LogHookStreamAPI api(&stream);

    api.emitTestLog("blocked %u", 1U);
    TEST_ASSERT_EQUAL_UINT(0, api.frameWriteCalls);

    api.allowLogEncoding = true;
    api.emitTestLog("allowed %u", 2U);
    TEST_ASSERT_EQUAL_UINT(1, api.frameWriteCalls);
    TEST_ASSERT_TRUE(api.lastBestEffort);
}

void test_serial_console_suppresses_raw_output_in_protobuf_mode()
{
    RecordingPrint sink;
    const bool oldHasLora = config.has_lora;
    const bool oldHasSecurity = config.has_security;
    const bool oldSerialEnabled = config.security.serial_enabled;
    const bool oldDebugLogApiEnabled = config.security.debug_log_api_enabled;

    config.has_lora = true;
    config.has_security = true;
    config.security.serial_enabled = true;
    config.security.debug_log_api_enabled = false;
    console->setDestination(&sink);

    console->write('A');
    const bool rawBeforeProtobuf = sink.output.size() == 1 && sink.output[0] == 'A';
    sink.output.clear();

    const uint8_t emptyToRadio = 0;
    console->handleToRadio(&emptyToRadio, 0);
    console->write('B');
    console->write('\n');
    console->log(MESHTASTIC_LOG_LEVEL_ERROR, "must stay framed");
    const bool emptyAfterProtobuf = sink.output.empty();

    console->setDestination(&Serial);
    config.has_lora = oldHasLora;
    config.has_security = oldHasSecurity;
    config.security.serial_enabled = oldSerialEnabled;
    config.security.debug_log_api_enabled = oldDebugLogApiEnabled;

    TEST_ASSERT_TRUE(rawBeforeProtobuf);
    TEST_ASSERT_TRUE(emptyAfterProtobuf);
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_frame_writer_continues_only_unwritten_tail);
    RUN_TEST(test_frame_writer_defers_main_behind_partial_log);
    RUN_TEST(test_frame_writer_rejects_best_effort_without_full_capacity);
    RUN_TEST(test_frame_writer_zero_progress_is_one_bounded_attempt);
    RUN_TEST(test_stream_api_full_write_frames_and_flushes);
    RUN_TEST(test_stream_api_short_write_reports_failure_without_flush);
    RUN_TEST(test_stream_api_finishes_pending_before_advancing_phone_api);
    RUN_TEST(test_stream_api_gates_logs_and_marks_them_best_effort);
    // usingProtobufs intentionally has no reset path, so this must run last.
    RUN_TEST(test_serial_console_suppresses_raw_output_in_protobuf_mode);
    exit(UNITY_END());
}

void loop() {}
