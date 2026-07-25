#include "MeshTypes.h"
#include "SerialConsole.h"
#include "TestUtil.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/StreamAPI.h"
#include "mesh/StreamFrameWriter.h"
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <deque>
#include <limits>
#include <unity.h>
#include <vector>

/// Output-only stream whose write quotas deterministically simulate backpressure.
class ScriptedStream : public Stream
{
  public:
    /// Report that no input bytes are queued.
    int available() override { return 0; }
    /// Return end-of-input for the output-only stream.
    int read() override { return -1; }
    /// Return end-of-input without consuming data.
    int peek() override { return -1; }
    /// Report the configured output capacity.
    int availableForWrite() override { return availableCapacity; }

    /// Route single-byte writes through the quota-aware buffer writer.
    size_t write(uint8_t value) override { return write(&value, 1); }

    /// Accept at most the next scripted quota and capture accepted bytes.
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

    /// Record flush calls without changing captured output.
    void flush() override { flushCount++; }

    /// Set the maximum bytes accepted by the next write call.
    void queueWrite(size_t quota) { writeQuotas.push_back(quota); }

    int availableCapacity = std::numeric_limits<int>::max();
    unsigned flushCount = 0;
    std::deque<size_t> writeQuotas;
    std::vector<size_t> requestedLengths;
    std::vector<uint8_t> output;
};

/// Print sink that records bytes emitted by the real SerialConsole.
class RecordingPrint : public Print
{
  public:
    /// Capture one output byte.
    size_t write(uint8_t value) override
    {
        output.push_back(value);
        return 1;
    }

    std::vector<uint8_t> output;
};

/// Installs a MeshService for a test and restores the previous global service.
class ScopedMeshService
{
  public:
    /// Install the scoped service.
    ScopedMeshService() : previous(service) { service = &instance; }
    /// Restore the prior service after StreamAPI fixtures are destroyed.
    ~ScopedMeshService() { service = previous; }

  private:
    MeshService instance;
    MeshService *previous;
};

/// Exposes generic StreamAPI hooks and records frame-write behavior.
class StreamAPITestShim : public StreamAPI
{
  public:
    /// Construct the shim over a scripted stream.
    explicit StreamAPITestShim(Stream *stream) : StreamAPI(stream) {}

    /// Keep connection-timeout handling inactive during tests.
    bool checkIsConnected() override { return true; }

    /// Invoke the generic transport implementation rather than this shim's capture hook.
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
    /// Record the pending-frame gate and return its configured state.
    bool finishPendingFrame() override
    {
        finishCalls++;
        return finishReady;
    }

    /// Apply the configured generic write-readiness result.
    bool canWriteFrame(size_t) override { return allowWrite; }

    /// Capture generic short-write failure metadata.
    void onFrameWriteFailed(size_t frameLen, size_t writtenLen) override
    {
        failureCalls++;
        failedFrameLen = frameLen;
        failedWrittenLen = writtenLen;
    }

    /// Capture one encoded PhoneAPI payload without writing it.
    bool writeFrame(uint8_t *buf, size_t len, bool bestEffort) override
    {
        (void)bestEffort;
        frameWriteCalls++;
        capturedPayload.assign(buf + 4, buf + 4 + len);
        return false;
    }
};

/// Minimal PhoneAPI transport for config-stream tests.
class PhoneAPITestShim : public PhoneAPI
{
  protected:
    bool checkIsConnected() override { return true; }
};

/// Exposes framed-log hooks and records best-effort writes.
class LogHookStreamAPI : public StreamAPI
{
  public:
    /// Construct the log shim over a scripted stream.
    explicit LogHookStreamAPI(Stream *stream) : StreamAPI(stream) {}

    /// Keep connection-timeout handling inactive during tests.
    bool checkIsConnected() override { return true; }

    /// Encode a formatted log through StreamAPI's production log path.
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
    /// Apply the configured log-encoding gate.
    bool canEncodeLogRecord() override { return allowLogEncoding; }

    /// Record whether the encoded log was marked best-effort.
    bool writeFrame(uint8_t *, size_t, bool bestEffort) override
    {
        frameWriteCalls++;
        lastBestEffort = bestEffort;
        return true;
    }
};

/// Assert byte-for-byte equality between expected and captured stream output.
static void assertBytesEqual(const std::vector<uint8_t> &expected, const std::vector<uint8_t> &actual)
{
    TEST_ASSERT_EQUAL_UINT(expected.size(), actual.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), actual.data(), expected.size());
}

/// Verify retries append only the unwritten tail and reproduce the frame exactly once.
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

/// Verify a replacement session receives a complete old frame before its new frame.
void test_frame_writer_completes_retained_tail_before_new_session_frame()
{
    ScriptedStream stream;
    StreamFrameWriter writer;
    std::vector<uint8_t> oldFrame = {0x94, 0xc3, 0x00, 0x03, 0xa1, 0xa2, 0xa3};
    std::vector<uint8_t> newFrame = {0x94, 0xc3, 0x00, 0x02, 0xb1, 0xb2};
    stream.queueWrite(3);
    stream.queueWrite(oldFrame.size());
    stream.queueWrite(newFrame.size());

    TEST_ASSERT_FALSE(writer.writeFrame(stream, oldFrame.data(), oldFrame.size(), false));
    TEST_ASSERT_FALSE(writer.isIdle());

    // A replacement client starts without discarding the accepted old prefix.
    TEST_ASSERT_TRUE(writer.writeFrame(stream, newFrame.data(), newFrame.size(), false));
    TEST_ASSERT_TRUE(writer.isIdle());

    std::vector<uint8_t> expected = oldFrame;
    expected.insert(expected.end(), newFrame.begin(), newFrame.end());
    assertBytesEqual(expected, stream.output);
}

/// Verify a required main frame remains ordered behind a partial log frame.
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

/// Verify best-effort output starts only when the complete frame fits.
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

/// Verify each zero-progress continuation makes one bounded write attempt.
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

/// Verify generic StreamAPI framing and successful-write flush behavior.
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

/// Verify generic transports report short writes without flushing or retrying.
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

/// Verify retained output blocks PhoneAPI from dequeuing the next payload.
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

/// Verify framed logs honor the encoding gate and use best-effort writes.
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

/// Verify the real SerialConsole emits no unframed bytes in protobuf mode.
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

// Build a phone->radio ADMIN_APP packet carrying `admin`, with an arbitrary wire `from`.
static meshtastic_MeshPacket makeAdminPacket(NodeNum from, const meshtastic_AdminMessage &admin)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_AdminMessage_msg, &admin);
    return p;
}

// The lockdown admin gate must decide on the connection's authorization, not the wire `from`. A
// client that sets from != 0 previously skipped the gate, so an unauthorized connection could run
// admin. classifyLocalAdminPacket ignores `from`, so the same spoofed packet is still dropped.
static void test_lockdown_admin_gate_ignores_wire_from(void)
{
    meshtastic_AdminMessage setter = meshtastic_AdminMessage_init_zero;
    setter.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
    meshtastic_MeshPacket spoofed = makeAdminPacket(0x12345678, setter); // from != 0, the bypass

    meshtastic_AdminMessage out;
    TEST_ASSERT_EQUAL_MESSAGE((int)PhoneAPI::LocalAdminGate::DropUnauthorized,
                              (int)PhoneAPI::classifyLocalAdminPacket(spoofed, /*adminAuthorized=*/false, out),
                              "unauthorized admin with from != 0 must still be dropped");
    // Control: an authorized connection's identical packet passes through.
    TEST_ASSERT_EQUAL_MESSAGE((int)PhoneAPI::LocalAdminGate::AuthorizedPassThrough,
                              (int)PhoneAPI::classifyLocalAdminPacket(spoofed, /*adminAuthorized=*/true, out),
                              "authorized admin must not be dropped");

    // lockdown_auth is the authentication itself, so it is delivered inline regardless of from/auth.
    meshtastic_AdminMessage la = meshtastic_AdminMessage_init_zero;
    la.which_payload_variant = meshtastic_AdminMessage_lockdown_auth_tag;
    meshtastic_MeshPacket authPkt = makeAdminPacket(0x99, la);
    TEST_ASSERT_EQUAL((int)PhoneAPI::LocalAdminGate::LockdownAuth,
                      (int)PhoneAPI::classifyLocalAdminPacket(authPkt, /*adminAuthorized=*/false, out));

    // A non-admin packet is outside the gate entirely.
    meshtastic_MeshPacket text = meshtastic_MeshPacket_init_zero;
    text.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    text.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL((int)PhoneAPI::LocalAdminGate::NotAdmin,
                      (int)PhoneAPI::classifyLocalAdminPacket(text, /*adminAuthorized=*/false, out));
}

// An ADMIN_APP packet whose payload is not a decodable AdminMessage must fall through to the
// normal reject path (NotAdmin), never be acted on as an admin command. The authorized control
// proves the decode-failure check runs before the auth branch, so it can't pass for the wrong reason.
static void test_lockdown_admin_gate_rejects_undecodable_admin(void)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    // Length-delimited field (tag 0x0A) claiming 16 bytes with none following: pb_decode fails.
    p.decoded.payload.bytes[0] = 0x0A;
    p.decoded.payload.bytes[1] = 0x10;
    p.decoded.payload.size = 2;

    meshtastic_AdminMessage out;
    TEST_ASSERT_EQUAL_MESSAGE((int)PhoneAPI::LocalAdminGate::NotAdmin,
                              (int)PhoneAPI::classifyLocalAdminPacket(p, /*adminAuthorized=*/false, out),
                              "undecodable ADMIN_APP payload must fall through to the reject path");
    TEST_ASSERT_EQUAL_MESSAGE((int)PhoneAPI::LocalAdminGate::NotAdmin,
                              (int)PhoneAPI::classifyLocalAdminPacket(p, /*adminAuthorized=*/true, out),
                              "undecodable ADMIN_APP payload must not pass through even when authorized");
}

static void test_want_config_includes_status_message_module_config(void)
{
    ScopedMeshService scopedService;
    NodeDB testNodeDB;
    NodeDB *const savedNodeDB = nodeDB;
    nodeDB = &testNodeDB;
    const auto savedModuleConfig = moduleConfig;
    moduleConfig.has_statusmessage = true;
    strncpy(moduleConfig.statusmessage.node_status, "Ready", sizeof(moduleConfig.statusmessage.node_status) - 1);
    moduleConfig.statusmessage.node_status[sizeof(moduleConfig.statusmessage.node_status) - 1] = '\0';

    meshtastic_ToRadio request = meshtastic_ToRadio_init_zero;
    request.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    request.want_config_id = SPECIAL_NONCE_ONLY_CONFIG;
    uint8_t requestBytes[meshtastic_ToRadio_size];
    const size_t requestSize = pb_encode_to_bytes(requestBytes, sizeof(requestBytes), &meshtastic_ToRadio_msg, &request);

    PhoneAPITestShim api;
    api.handleToRadio(requestBytes, requestSize);

    bool foundStatusMessageConfig = false;
    for (unsigned i = 0; i < 64 && !foundStatusMessageConfig; ++i) {
        uint8_t responseBytes[meshtastic_FromRadio_size];
        const size_t responseSize = api.getFromRadio(responseBytes);
        meshtastic_FromRadio response = meshtastic_FromRadio_init_zero;
        TEST_ASSERT_TRUE(pb_decode_from_bytes(responseBytes, responseSize, &meshtastic_FromRadio_msg, &response));
        if (response.which_payload_variant == meshtastic_FromRadio_moduleConfig_tag &&
            response.moduleConfig.which_payload_variant == meshtastic_ModuleConfig_statusmessage_tag) {
            foundStatusMessageConfig = true;
            TEST_ASSERT_EQUAL_STRING("Ready", response.moduleConfig.payload_variant.statusmessage.node_status);
        }
    }

    api.close();
    moduleConfig = savedModuleConfig;
    nodeDB = savedNodeDB;
    TEST_ASSERT_TRUE(foundStatusMessageConfig);
}

/// Unity per-test setup; fixtures are local to each test.
void setUp(void) {}
/// Unity per-test teardown; fixtures clean themselves up.
void tearDown(void) {}

/// Initialize the native environment and run the stream regression suite.
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_frame_writer_continues_only_unwritten_tail);
    RUN_TEST(test_frame_writer_completes_retained_tail_before_new_session_frame);
    RUN_TEST(test_frame_writer_defers_main_behind_partial_log);
    RUN_TEST(test_frame_writer_rejects_best_effort_without_full_capacity);
    RUN_TEST(test_frame_writer_zero_progress_is_one_bounded_attempt);
    RUN_TEST(test_stream_api_full_write_frames_and_flushes);
    RUN_TEST(test_stream_api_short_write_reports_failure_without_flush);
    RUN_TEST(test_stream_api_finishes_pending_before_advancing_phone_api);
    RUN_TEST(test_stream_api_gates_logs_and_marks_them_best_effort);
    RUN_TEST(test_lockdown_admin_gate_ignores_wire_from);
    RUN_TEST(test_lockdown_admin_gate_rejects_undecodable_admin);
    RUN_TEST(test_want_config_includes_status_message_module_config);
    // usingProtobufs intentionally has no reset path, so this must run last.
    RUN_TEST(test_serial_console_suppresses_raw_output_in_protobuf_mode);
    exit(UNITY_END());
}

/// Unused Arduino loop required by the native Unity runner.
void loop() {}
