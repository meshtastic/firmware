#include "TestUtil.h"
#if defined(ARCH_PORTDUINO) && __has_include(<ulfius.h>)
#include "mesh/raspihttp/PiWebServer.h"
#include "support/MockMeshService.h"
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#endif
#include <cstdlib>
#include <unity.h>

#if defined(ARCH_PORTDUINO) && __has_include(<ulfius.h>)
class RecordingHttpAPI : public HttpAPI
{
  public:
    bool handleToRadio(const uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> guard(recordMutex);
        dispatchThread = std::this_thread::get_id();
        values.push_back(len ? buf[0] : 0);
        return true;
    }

    std::mutex recordMutex;
    std::thread::id dispatchThread;
    std::vector<uint8_t> values;
};

class BlockingHttpAPI : public RecordingHttpAPI
{
  public:
    explicit BlockingHttpAPI(std::shared_future<void> release) : release(release) {}

    bool handleToRadio(const uint8_t *buf, size_t len) override
    {
        dispatchEntered.set_value();
        release.wait();
        return RecordingHttpAPI::handleToRadio(buf, len);
    }

    std::promise<void> dispatchEntered;

  private:
    std::shared_future<void> release;
};

static MockMeshService *mockMeshService;

void setUp()
{
    mockMeshService = new MockMeshService();
    service = mockMeshService;
}

void tearDown()
{
    service = nullptr;
    delete mockMeshService;
    mockMeshService = nullptr;
}

static bool waitForPending(HttpAPI &api, size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (api.pendingRequestCount() != count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return api.pendingRequestCount() == count;
}

static void test_toRadioSubmissionWaitsForMainPump()
{
    RecordingHttpAPI api;
    std::promise<std::thread::id> submitThreadPromise;
    auto submitThread = submitThreadPromise.get_future();
    uint8_t payload[] = {42};
    auto submitted = std::async(std::launch::async, [&] {
        submitThreadPromise.set_value(std::this_thread::get_id());
        return api.submitToRadio(payload, sizeof(payload));
    });
    const std::thread::id workerThread = submitThread.get();

    const bool pending = waitForPending(api, 1);
    const auto beforePump = submitted.wait_for(std::chrono::milliseconds(0));
    if (pending)
        api.processPendingRequests();
    else
        api.stopAcceptingRequests();
    const bool result = submitted.get();

    TEST_ASSERT_TRUE(pending);
    TEST_ASSERT_EQUAL(std::future_status::timeout, beforePump);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT8(1, api.values.size());
    TEST_ASSERT_EQUAL_UINT8(42, api.values[0]);
    TEST_ASSERT_FALSE(api.dispatchThread == workerThread);
}

static void test_toRadioRequestsRemainFifo()
{
    RecordingHttpAPI api;
    uint8_t first[] = {1};
    uint8_t second[] = {2};
    auto firstResult = std::async(std::launch::async, [&] { return api.submitToRadio(first, sizeof(first)); });
    const bool firstPending = waitForPending(api, 1);
    auto secondResult = std::async(std::launch::async, [&] { return api.submitToRadio(second, sizeof(second)); });
    const bool bothPending = waitForPending(api, 2);
    if (bothPending)
        api.processPendingRequests();
    else
        api.stopAcceptingRequests();
    const bool firstAccepted = firstResult.get();
    const bool secondAccepted = secondResult.get();

    TEST_ASSERT_TRUE(firstPending);
    TEST_ASSERT_TRUE(bothPending);
    TEST_ASSERT_TRUE(firstAccepted);
    TEST_ASSERT_TRUE(secondAccepted);
    TEST_ASSERT_EQUAL_UINT8(2, api.values.size());
    TEST_ASSERT_EQUAL_UINT8(1, api.values[0]);
    TEST_ASSERT_EQUAL_UINT8(2, api.values[1]);
}

static void test_toRadioQueueRejectsOverflowWithoutOverwriting()
{
    RecordingHttpAPI api;
    std::vector<std::future<bool>> results;
    uint8_t payloads[9] = {};
    for (uint8_t i = 0; i < 8; ++i) {
        payloads[i] = i;
        results.push_back(std::async(std::launch::async, [&, i] { return api.submitToRadio(&payloads[i], 1); }));
    }
    const bool queueFull = waitForPending(api, 8);
    bool overflowAccepted = false;
    if (queueFull) {
        auto overflow = std::async(std::launch::async, [&] { return api.submitToRadio(&payloads[8], 1); });
        overflowAccepted = overflow.get();
        api.processPendingRequests();
    } else {
        api.stopAcceptingRequests();
    }

    uint8_t accepted = 0;
    for (auto &result : results)
        accepted += result.get() ? 1 : 0;
    TEST_ASSERT_TRUE(queueFull);
    TEST_ASSERT_FALSE(overflowAccepted);
    TEST_ASSERT_EQUAL_UINT8(8, accepted);
    TEST_ASSERT_EQUAL_UINT8(8, api.values.size());
}

static void test_shutdownCancelsQueuedRequest()
{
    RecordingHttpAPI api;
    uint8_t payload[] = {7};
    auto submitted = std::async(std::launch::async, [&] { return api.submitToRadio(payload, sizeof(payload)); });
    const bool pending = waitForPending(api, 1);

    api.stopAcceptingRequests();
    const bool result = submitted.get();

    TEST_ASSERT_TRUE(pending);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(api.values.empty());
}

static void test_shutdownCancelsInFlightRequest()
{
    std::promise<void> releaseDispatch;
    BlockingHttpAPI api(releaseDispatch.get_future().share());
    auto dispatchEntered = api.dispatchEntered.get_future();
    uint8_t payload[] = {9};
    auto submitted = std::async(std::launch::async, [&] { return api.submitToRadio(payload, sizeof(payload)); });
    const bool pending = waitForPending(api, 1);
    std::future<void> pump;
    std::future_status entered = std::future_status::timeout;
    if (pending) {
        pump = std::async(std::launch::async, [&] { api.processPendingRequests(); });
        entered = dispatchEntered.wait_for(std::chrono::seconds(1));
    }

    api.stopAcceptingRequests();
    const bool result = submitted.get();
    releaseDispatch.set_value();
    if (pump.valid())
        pump.get();

    TEST_ASSERT_TRUE(pending);
    TEST_ASSERT_EQUAL(std::future_status::ready, entered);
    TEST_ASSERT_FALSE(result);
}

static void test_fromRadioSubmissionWaitsForMainPump()
{
    RecordingHttpAPI api;
    uint8_t payload[MAX_TO_FROM_RADIO_SIZE] = {};
    size_t length = sizeof(payload);
    auto submitted = std::async(std::launch::async, [&] { return api.submitFromRadio(payload, length); });
    const bool pending = waitForPending(api, 1);
    const auto beforePump = submitted.wait_for(std::chrono::milliseconds(0));
    if (pending)
        api.processPendingRequests();
    else
        api.stopAcceptingRequests();
    const bool result = submitted.get();

    TEST_ASSERT_TRUE(pending);
    TEST_ASSERT_EQUAL(std::future_status::timeout, beforePump);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT32(0, length);
}

static void test_invalidToRadioSizesAreRejected()
{
    RecordingHttpAPI api;
    uint8_t payload[MAX_TO_FROM_RADIO_SIZE + 1] = {};
    TEST_ASSERT_FALSE(api.submitToRadio(payload, 0));
    TEST_ASSERT_FALSE(api.submitToRadio(payload, sizeof(payload)));
}
#else
static void test_placeholder()
{
    TEST_ASSERT_TRUE(true);
}
#endif

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
#if defined(ARCH_PORTDUINO) && __has_include(<ulfius.h>)
    RUN_TEST(test_toRadioSubmissionWaitsForMainPump);
    RUN_TEST(test_toRadioRequestsRemainFifo);
    RUN_TEST(test_toRadioQueueRejectsOverflowWithoutOverwriting);
    RUN_TEST(test_shutdownCancelsQueuedRequest);
    RUN_TEST(test_shutdownCancelsInFlightRequest);
    RUN_TEST(test_fromRadioSubmissionWaitsForMainPump);
    RUN_TEST(test_invalidToRadioSizesAreRejected);
#else
    RUN_TEST(test_placeholder);
#endif
    exit(UNITY_END());
}

void loop() {}
}
