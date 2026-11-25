#include "MeshTypes.h"
#include "TestUtil.h"
#include "CryptoEngine.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PhoneAPI.h"
#include "Router.h"
#include "UptimeClock.h"
#include <functional>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <unity.h>

extern bool heartbeatReceived;
extern size_t encodePhoneApiChunkForTest(const uint8_t *, uint16_t, uint16_t &, uint8_t *);

namespace
{
using Bytes = std::vector<uint8_t>;
class PhoneAPITestShim : public PhoneAPI
{
  public:
    unsigned dispatches = 0;
    Bytes lastDispatch;
    bool dispatchOnly = false;
    std::function<void()> duringDispatch;
    bool handleToRadio(const uint8_t *bytes, size_t size) override
    {
        ++dispatches;
        lastDispatch.assign(bytes, bytes + size);
        if (duringDispatch)
            duringDispatch();
        return dispatchOnly ? false : PhoneAPI::handleToRadio(bytes, size);
    }
    bool receive(const Bytes &bytes) { return PhoneAPI::handleToRadio(bytes.data(), bytes.size()); }

  protected:
    bool checkIsConnected() override { return true; }
    void onNowHasData(uint32_t) override {}
};

class TestRouter : public Router
{
  public:
    ~TestRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }
};

struct Globals {
    MeshService *service;
    Router *router;
    NodeDB *nodeDB;
    concurrency::Lock *cryptLock;
    meshtastic_MyNodeInfo myNodeInfo;
    meshtastic_LocalConfig config;
    meshtastic_LocalModuleConfig moduleConfig;
    meshtastic_DeviceState deviceState;
    Channels channels;
    meshtastic_ChannelFile channelFile;
};
Globals *saved;
PhoneAPITestShim *clients[3];

Bytes encode(meshtastic_ToRadio &message)
{
    Bytes bytes(meshtastic_ToRadio_size);
    size_t size = pb_encode_to_bytes(bytes.data(), bytes.size(), &meshtastic_ToRadio_msg, &message);
    TEST_ASSERT_GREATER_THAN_UINT(0, size);
    bytes.resize(size);
    return bytes;
}

Bytes chunk(const Bytes &payload, uint32_t total)
{
    meshtastic_ToRadio message = meshtastic_ToRadio_init_zero;
    message.which_payload_variant = meshtastic_ToRadio_chunked_payload_tag;
    message.chunked_payload.payload_size = total;
    message.chunked_payload.payload_chunk.size = payload.size();
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(message.chunked_payload.payload_chunk.bytes), payload.size());
    memcpy(message.chunked_payload.payload_chunk.bytes, payload.data(), payload.size());
    return encode(message);
}

Bytes configRequest()
{
    meshtastic_ToRadio message = meshtastic_ToRadio_init_zero;
    message.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    message.want_config_id = 69420;
    return encode(message);
}

void sendFragments(PhoneAPITestShim &api, const Bytes &bytes)
{
    for (size_t offset = 0; offset < bytes.size();) {
        const size_t count = std::min<size_t>(12, bytes.size() - offset);
        Bytes part(bytes.begin() + offset, bytes.begin() + offset + count);
        Bytes frame = chunk(part, offset ? 0 : bytes.size());
        TEST_ASSERT_LESS_OR_EQUAL_UINT(20, frame.size());
        api.receive(frame);
        offset += count;
    }
}

Bytes readRaw(PhoneAPITestShim &api)
{
    Bytes bytes(meshtastic_FromRadio_size);
    bytes.resize(api.getFromRadio(bytes.data()));
    return bytes;
}

Bytes readReassembled(PhoneAPITestShim &api)
{
    Bytes result;
    uint32_t total = 0;
    do {
        Bytes frame = readRaw(api);
        TEST_ASSERT_GREATER_THAN_UINT(0, frame.size());
        TEST_ASSERT_LESS_OR_EQUAL_UINT(20, frame.size());
        meshtastic_FromRadio message = meshtastic_FromRadio_init_zero;
        TEST_ASSERT_TRUE(pb_decode_from_bytes(frame.data(), frame.size(), &meshtastic_FromRadio_msg, &message));
        if (message.which_payload_variant != meshtastic_FromRadio_chunked_payload_tag) {
            TEST_ASSERT_TRUE(result.empty());
            return frame;
        }
        auto &part = message.chunked_payload;
        if (result.empty()) {
            total = part.payload_size;
            TEST_ASSERT_GREATER_THAN_UINT(20, total);
        } else {
            TEST_ASSERT_EQUAL_UINT(0, part.payload_size);
        }
        TEST_ASSERT_GREATER_THAN_UINT(0, part.payload_chunk.size);
        result.insert(result.end(), part.payload_chunk.bytes, part.payload_chunk.bytes + part.payload_chunk.size);
        TEST_ASSERT_LESS_OR_EQUAL_UINT(total, result.size());
    } while (result.size() != total);
    return result;
}

void test_legacy_and_negotiated_config_are_identical()
{
    clients[0]->receive(configRequest());
    sendFragments(*clients[1], configRequest());
    bool sawLarge = false;
    for (unsigned i = 0; i < 100; ++i) {
        Bytes plain = readRaw(*clients[0]);
        TEST_ASSERT_FALSE(plain.empty());
        sawLarge |= plain.size() > 20;
        Bytes reassembled = readReassembled(*clients[1]);
        TEST_ASSERT_EQUAL_UINT(plain.size(), reassembled.size());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(plain.data(), reassembled.data(), plain.size());
        meshtastic_FromRadio message = meshtastic_FromRadio_init_zero;
        TEST_ASSERT_TRUE(pb_decode_from_bytes(plain.data(), plain.size(), &meshtastic_FromRadio_msg, &message));
        if (message.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag) {
            TEST_ASSERT_TRUE(sawLarge);
            return;
        }
    }
    TEST_FAIL_MESSAGE("config dump did not complete");
}

void test_fragmented_initial_handshake_and_invalid_frames()
{
    const Bytes request = configRequest();
    const Bytes first(request.begin(), request.begin() + 1);
    const Bytes rest(request.begin() + 1, request.end());
    auto &api = *clients[0];
    api.receive(chunk(first, request.size()));
    TEST_ASSERT_FALSE(api.isConnected());
    api.receive(chunk(rest, 0));
    TEST_ASSERT_EQUAL_UINT(1, api.dispatches);
    TEST_ASSERT_TRUE(api.isConnected());
    for (const Bytes &invalid : {chunk({}, 0), chunk(first, 513), chunk(rest, 1), chunk(request, 0)}) {
        api.receive(chunk(first, request.size()));
        api.receive(invalid);
        api.receive(chunk(rest, 0));
        TEST_ASSERT_EQUAL_UINT(1, api.dispatches);
    }
    api.receive(chunk(rest, 0));
    TEST_ASSERT_EQUAL_UINT(1, api.dispatches);
    sendFragments(api, request);
    TEST_ASSERT_EQUAL_UINT(2, api.dispatches);
}

void test_timeout_exact_boundary_and_wrap()
{
    const Bytes request = configRequest();
    for (uint32_t start : {100u, 0xfffffff0u, 0u}) {
        for (uint32_t elapsed : {29999u, 30000u}) {
            clients[0]->close();
            unsigned before = clients[0]->dispatches;
            Time::setTestMillis(start);
            clients[0]->receive(chunk(Bytes(request.begin(), request.begin() + 1), request.size()));
            Time::advanceTestMillis(elapsed);
            clients[0]->receive(chunk(Bytes(request.begin() + 1, request.end()), 0));
            TEST_ASSERT_EQUAL_UINT(before + (elapsed == 29999), clients[0]->dispatches);
        }
    }
}

void test_two_slots_close_and_destructor_cleanup()
{
    for (auto *client : clients)
        sendFragments(*client, configRequest());
    TEST_ASSERT_EQUAL_UINT(1, clients[0]->dispatches);
    TEST_ASSERT_EQUAL_UINT(1, clients[1]->dispatches);
    TEST_ASSERT_EQUAL_UINT(0, clients[2]->dispatches);
    clients[0]->close();
    sendFragments(*clients[2], configRequest());
    TEST_ASSERT_EQUAL_UINT(1, clients[2]->dispatches);
    delete clients[1];
    clients[1] = new PhoneAPITestShim();
    sendFragments(*clients[1], configRequest());
    TEST_ASSERT_EQUAL_UINT(1, clients[1]->dispatches);
    // Closing drops pending outbound fragments and opt-in, not only the input buffer.
    clients[2]->close();
    clients[2]->receive(configRequest());
    bool sawLarge = false;
    for (unsigned i = 0; i < 8; ++i)
        sawLarge |= readRaw(*clients[2]).size() > 20;
    TEST_ASSERT_TRUE(sawLarge);
}

void test_nested_chunk_is_rejected_and_guard_recovers()
{
    Bytes nested = configRequest();
    while (nested.size() <= 228)
        nested = chunk(nested, nested.size());
    sendFragments(*clients[0], nested);
    TEST_ASSERT_EQUAL_UINT(1, clients[0]->dispatches);
    TEST_ASSERT_FALSE(clients[0]->isConnected());
    sendFragments(*clients[0], configRequest());
    TEST_ASSERT_EQUAL_UINT(2, clients[0]->dispatches);
    TEST_ASSERT_TRUE(clients[0]->isConnected());
}

void test_dispatch_pins_slot_through_close_without_blocking_other_client()
{
    clients[0]->duringDispatch = [] {
        clients[0]->close();
        sendFragments(*clients[0], configRequest());
        sendFragments(*clients[1], configRequest());
        sendFragments(*clients[2], configRequest());
    };
    sendFragments(*clients[0], configRequest());
    clients[0]->duringDispatch = nullptr;
    TEST_ASSERT_EQUAL_UINT(1, clients[0]->dispatches);
    TEST_ASSERT_EQUAL_UINT(1, clients[1]->dispatches);
    TEST_ASSERT_EQUAL_UINT(0, clients[2]->dispatches);
    sendFragments(*clients[2], configRequest());
    TEST_ASSERT_EQUAL_UINT(1, clients[2]->dispatches);
}

void test_reassembled_disconnect_releases_slot()
{
    Bytes disconnect{0x20, 0x01};
    sendFragments(*clients[0], disconnect);
    TEST_ASSERT_FALSE(clients[0]->isConnected());
    sendFragments(*clients[1], configRequest());
    sendFragments(*clients[2], configRequest());
    TEST_ASSERT_EQUAL_UINT(1, clients[1]->dispatches);
    TEST_ASSERT_EQUAL_UINT(1, clients[2]->dispatches);
}

void test_concurrent_close_and_other_client_during_dispatch()
{
    clients[0]->duringDispatch = [] {
        std::thread transport([] {
            clients[0]->close();
            sendFragments(*clients[0], configRequest());
            sendFragments(*clients[1], configRequest());
            sendFragments(*clients[2], configRequest());
        });
        transport.join();
    };
    sendFragments(*clients[0], configRequest());
    clients[0]->duringDispatch = nullptr;
    TEST_ASSERT_EQUAL_UINT(1, clients[0]->dispatches);
    TEST_ASSERT_EQUAL_UINT(1, clients[1]->dispatches);
    TEST_ASSERT_EQUAL_UINT(0, clients[2]->dispatches);
    sendFragments(*clients[2], configRequest());
    TEST_ASSERT_EQUAL_UINT(1, clients[2]->dispatches);
}

void test_encoder_boundaries_match_nanopb()
{
    for (uint16_t total : {21, 127, 128, 511, 512}) {
        Bytes bytes(total);
        for (size_t i = 0; i < total; ++i)
            bytes[i] = i;
        uint16_t offset = 0;
        while (offset < total) {
            uint16_t before = offset;
            uint8_t frame[20];
            size_t size = encodePhoneApiChunkForTest(bytes.data(), total, offset, frame);
            TEST_ASSERT_GREATER_THAN_UINT(0, size);
            TEST_ASSERT_LESS_OR_EQUAL_UINT(20, size);
            meshtastic_FromRadio expected = meshtastic_FromRadio_init_zero;
            expected.which_payload_variant = meshtastic_FromRadio_chunked_payload_tag;
            expected.chunked_payload.payload_size = before ? 0 : total;
            expected.chunked_payload.payload_chunk.size = offset - before;
            memcpy(expected.chunked_payload.payload_chunk.bytes, bytes.data() + before, offset - before);
            uint8_t canonical[meshtastic_FromRadio_size];
            size_t expectedSize = pb_encode_to_bytes(canonical, sizeof(canonical), &meshtastic_FromRadio_msg, &expected);
            TEST_ASSERT_EQUAL_UINT(expectedSize, size);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(canonical, frame, size);
        }
        // Arbitrary payloads are dispatched once, byte-for-byte, even at the 512-byte bound.
        clients[0]->close();
        unsigned before = clients[0]->dispatches;
        sendFragments(*clients[0], bytes);
        TEST_ASSERT_EQUAL_UINT(before + 1, clients[0]->dispatches);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), clients[0]->lastDispatch.data(), total);
    }
}

void test_legacy_chunked_payload_vector_is_unchanged()
{
    const uint8_t vector[] = {0x08, 0x01, 0x10, 0x02, 0x18, 0x03, 0x22, 0x01, 0x04};
    meshtastic_ChunkedPayload message = meshtastic_ChunkedPayload_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(vector, sizeof(vector), &meshtastic_ChunkedPayload_msg, &message));
    uint8_t encoded[meshtastic_ChunkedPayload_size];
    size_t size = pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_ChunkedPayload_msg, &message);
    TEST_ASSERT_EQUAL_UINT(sizeof(vector), size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(vector, encoded, size);
}

void test_ble_filters_preserve_identical_continuations_and_legacy_dedup()
{
    Bytes request{0x7a, 48}; // Unknown length-delimited field followed by an ordinary config request.
    request.insert(request.end(), 48, 0);
    Bytes config = configRequest();
    request.insert(request.end(), config.begin(), config.end());
    uint8_t previous[MAX_TO_FROM_RADIO_SIZE] = {};
    unsigned repeats = 0;
    for (size_t offset = 0; offset < request.size(); offset += 12) {
        size_t count = std::min<size_t>(12, request.size() - offset);
        Bytes frame = chunk(Bytes(request.begin() + offset, request.begin() + offset + count), offset ? 0 : request.size());
        repeats += memcmp(previous, frame.data(), frame.size()) == 0;
        TEST_ASSERT_TRUE(PhoneAPI::shouldProcessToRadio(frame.data(), frame.size(), previous));
        memcpy(previous, frame.data(), frame.size());
        clients[0]->receive(frame);
    }
    TEST_ASSERT_GREATER_THAN_UINT(1, repeats);
    TEST_ASSERT_EQUAL_UINT(1, clients[0]->dispatches);
    TEST_ASSERT_TRUE(clients[0]->isConnected());
    TEST_ASSERT_TRUE(PhoneAPI::shouldProcessToRadio(config.data(), config.size(), previous));
    memcpy(previous, config.data(), config.size());
    TEST_ASSERT_FALSE(PhoneAPI::shouldProcessToRadio(config.data(), config.size(), previous));

    Bytes frame = chunk({1}, 0);
    frame.insert(frame.begin(), {0x78, 1}); // Unknown fields need not follow the oneof.
    memcpy(previous, frame.data(), frame.size());
    TEST_ASSERT_TRUE(PhoneAPI::shouldProcessToRadio(frame.data(), frame.size(), previous));
    frame.insert(frame.end(), config.begin(), config.end()); // Last oneof wins: this is not a chunk.
    memcpy(previous, frame.data(), frame.size());
    TEST_ASSERT_FALSE(PhoneAPI::shouldProcessToRadio(frame.data(), frame.size(), previous));
    for (const Bytes &invalid : {Bytes{0x42, 0xff}, Bytes{0x42, 2, 1}, Bytes{0x40, 1}}) {
        memcpy(previous, invalid.data(), invalid.size());
        TEST_ASSERT_FALSE(PhoneAPI::shouldProcessToRadio(invalid.data(), invalid.size(), previous));
    }
    TEST_ASSERT_FALSE(PhoneAPI::shouldProcessToRadio(previous, 0, previous));
    TEST_ASSERT_FALSE(PhoneAPI::shouldProcessToRadio(previous, 513, previous));
}

void test_plain_handshake_resets_opt_in_and_pending_output()
{
    sendFragments(*clients[0], configRequest());
    Bytes first;
    meshtastic_FromRadio message = meshtastic_FromRadio_init_zero;
    do {
        first = readRaw(*clients[0]);
        TEST_ASSERT_FALSE(first.empty());
        TEST_ASSERT_TRUE(pb_decode_from_bytes(first.data(), first.size(), &meshtastic_FromRadio_msg, &message));
    } while (message.which_payload_variant != meshtastic_FromRadio_chunked_payload_tag);
    TEST_ASSERT_TRUE(clients[0]->available());

    clients[0]->receive(configRequest());
    clients[1]->receive(configRequest());
    bool sawLarge = false;
    for (unsigned i = 0; i < 8; ++i) {
        Bytes actual = readRaw(*clients[0]);
        Bytes expected = readRaw(*clients[1]);
        TEST_ASSERT_EQUAL_UINT(expected.size(), actual.size());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), actual.data(), actual.size());
        sawLarge |= actual.size() > 20;
    }
    TEST_ASSERT_TRUE(sawLarge);
    sendFragments(*clients[0], configRequest());
    clients[1]->receive(configRequest());
    for (unsigned i = 0; i < 8; ++i) {
        Bytes actual = readReassembled(*clients[0]);
        Bytes expected = readRaw(*clients[1]);
        TEST_ASSERT_EQUAL_UINT(expected.size(), actual.size());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), actual.data(), actual.size());
    }
}

void test_chunked_heartbeat_and_pending_output_available()
{
    sendFragments(*clients[0], Bytes{0x3a, 0});
    TEST_ASSERT_TRUE(heartbeatReceived);
    Bytes response = readRaw(*clients[0]);
    meshtastic_FromRadio message = meshtastic_FromRadio_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(response.data(), response.size(), &meshtastic_FromRadio_msg, &message));
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_queueStatus_tag, message.which_payload_variant);
    TEST_ASSERT_FALSE(heartbeatReceived);
    TEST_ASSERT_FALSE(clients[0]->isConnected());
    // Use a complete dump to reach SEND_PACKETS, then leave only a chunked notification pending.
    sendFragments(*clients[0], configRequest());
    for (unsigned i = 0; i < 100; ++i) {
        Bytes bytes = readReassembled(*clients[0]);
        TEST_ASSERT_TRUE(pb_decode_from_bytes(bytes.data(), bytes.size(), &meshtastic_FromRadio_msg, &message));
        if (message.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
            break;
    }
    clients[0]->sendNotification(meshtastic_LogRecord_Level_INFO, 0, "A notification long enough to require several chunks",
                                 meshtastic_ClientNotification_NotificationType_UNSET);
    do {
        response = readRaw(*clients[0]);
        TEST_ASSERT_FALSE(response.empty());
        TEST_ASSERT_TRUE(pb_decode_from_bytes(response.data(), response.size(), &meshtastic_FromRadio_msg, &message));
    } while (message.which_payload_variant != meshtastic_FromRadio_chunked_payload_tag);
    TEST_ASSERT_TRUE(clients[0]->available());
    while (message.which_payload_variant == meshtastic_FromRadio_chunked_payload_tag) {
        response = readRaw(*clients[0]);
        if (response.empty())
            break;
        TEST_ASSERT_TRUE(pb_decode_from_bytes(response.data(), response.size(), &meshtastic_FromRadio_msg, &message));
    }
}

void test_two_dispatches_overlap_and_pin_both_slots()
{
    std::mutex mutex;
    std::condition_variable cv;
    unsigned entered = 0;
    bool release[2] = {false, false};
    Bytes frame = chunk(configRequest(), configRequest().size());
    for (unsigned i = 0; i < 2; ++i) {
        clients[i]->dispatchOnly = true;
        clients[i]->duringDispatch = [&, i] {
            std::unique_lock<std::mutex> lock(mutex);
            ++entered;
            cv.notify_all();
            cv.wait(lock, [&] { return release[i]; });
        };
    }
    std::thread first([&] { clients[0]->receive(frame); });
    std::thread second([&] { clients[1]->receive(frame); });
    bool overlapped;
    {
        std::unique_lock<std::mutex> lock(mutex);
        overlapped = cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered == 2; });
        if (!overlapped) {
            release[0] = release[1] = true;
            cv.notify_all();
        }
    }
    if (!overlapped) {
        first.join();
        second.join();
        for (unsigned i = 0; i < 2; ++i)
            clients[i]->duringDispatch = nullptr;
        TEST_FAIL_MESSAGE("independent clients could not dispatch concurrently");
    }
    clients[0]->close();
    clients[1]->close();
    clients[0]->receive(frame);
    clients[2]->receive(frame);
    unsigned beforeRelease = clients[2]->dispatches;
    {
        std::lock_guard<std::mutex> lock(mutex);
        release[0] = true;
        cv.notify_all();
    }
    first.join();
    clients[2]->receive(frame);
    unsigned afterOneRelease = clients[2]->dispatches;
    {
        std::lock_guard<std::mutex> lock(mutex);
        release[1] = true;
        cv.notify_all();
    }
    second.join();
    for (unsigned i = 0; i < 2; ++i)
        clients[i]->duringDispatch = nullptr;
    TEST_ASSERT_EQUAL_UINT(0, beforeRelease);
    TEST_ASSERT_EQUAL_UINT(1, afterOneRelease);
    TEST_ASSERT_EQUAL_UINT(1, clients[0]->dispatches);
    TEST_ASSERT_EQUAL_UINT(1, clients[1]->dispatches);
}
} // namespace

void setUp()
{
    saved = new Globals{service, router, nodeDB, cryptLock, myNodeInfo, config, moduleConfig, devicestate, channels, channelFile};
    service = new MeshService();
    nodeDatabase.version = 0;
    nodeDatabase.nodes.clear();
    nodeDB = new NodeDB();
    cryptLock = nullptr;
    router = new TestRouter();
    for (auto &client : clients)
        client = new PhoneAPITestShim();
    heartbeatReceived = false;
    Time::setTestMillis(100);
}

void tearDown()
{
    for (auto *client : clients)
        delete client;
    delete router;
    delete nodeDB;
    delete service;
    service = saved->service;
    router = saved->router;
    nodeDB = saved->nodeDB;
    cryptLock = saved->cryptLock;
    myNodeInfo = saved->myNodeInfo;
    config = saved->config;
    moduleConfig = saved->moduleConfig;
    devicestate = saved->deviceState;
    channels = saved->channels;
    channelFile = saved->channelFile;
    delete saved;
    heartbeatReceived = false;
    Time::useRealClock();
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_legacy_and_negotiated_config_are_identical);
    RUN_TEST(test_fragmented_initial_handshake_and_invalid_frames);
    RUN_TEST(test_timeout_exact_boundary_and_wrap);
    RUN_TEST(test_two_slots_close_and_destructor_cleanup);
    RUN_TEST(test_nested_chunk_is_rejected_and_guard_recovers);
    RUN_TEST(test_dispatch_pins_slot_through_close_without_blocking_other_client);
    RUN_TEST(test_reassembled_disconnect_releases_slot);
    RUN_TEST(test_concurrent_close_and_other_client_during_dispatch);
    RUN_TEST(test_encoder_boundaries_match_nanopb);
    RUN_TEST(test_legacy_chunked_payload_vector_is_unchanged);
    RUN_TEST(test_ble_filters_preserve_identical_continuations_and_legacy_dedup);
    RUN_TEST(test_plain_handshake_resets_opt_in_and_pending_output);
    RUN_TEST(test_chunked_heartbeat_and_pending_output_available);
    RUN_TEST(test_two_dispatches_overlap_and_pin_both_slots);
    exit(UNITY_END());
}

void loop() {}
