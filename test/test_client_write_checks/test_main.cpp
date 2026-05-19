#include "TestUtil.h"
#include "mesh/api/ClientWriteChecks.h"
#include <unity.h>

namespace
{
class ClientWithAvailable
{
  public:
    bool connected() const { return connected_; }
    int availableForWrite() const { return writable_; }

    bool connected_ = true;
    int writable_ = 64;
};

class ClientWithoutAvailable
{
  public:
    bool connected() const { return connected_; }

    bool connected_ = true;
};
} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_availableForWrite_is_detected_when_present()
{
    TEST_ASSERT_TRUE((stream_api::HasAvailableForWrite<ClientWithAvailable>::value));
    TEST_ASSERT_FALSE((stream_api::HasAvailableForWrite<ClientWithoutAvailable>::value));
}

void test_availableForWriteOrUnknown_returns_client_capacity()
{
    ClientWithAvailable client;
    TEST_ASSERT_EQUAL_INT(64, stream_api::availableForWriteOrUnknown(client));
}

void test_availableForWriteOrUnknown_returns_unknown_when_absent()
{
    ClientWithoutAvailable client;
    TEST_ASSERT_EQUAL_INT(-1, stream_api::availableForWriteOrUnknown(client));
}

void test_clientReadyForWrite_rejects_disconnected_clients()
{
    ClientWithAvailable client;
    client.connected_ = false;
    TEST_ASSERT_FALSE(stream_api::clientReadyForWrite(client));
}

void test_clientReadyForWrite_rejects_non_writable_clients()
{
    ClientWithAvailable client;
    client.writable_ = 0;
    TEST_ASSERT_FALSE(stream_api::clientReadyForWrite(client));
}

void test_clientReadyForWrite_allows_clients_without_availableForWrite()
{
    ClientWithoutAvailable client;
    TEST_ASSERT_TRUE(stream_api::clientReadyForWrite(client));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_availableForWrite_is_detected_when_present);
    RUN_TEST(test_availableForWriteOrUnknown_returns_client_capacity);
    RUN_TEST(test_availableForWriteOrUnknown_returns_unknown_when_absent);
    RUN_TEST(test_clientReadyForWrite_rejects_disconnected_clients);
    RUN_TEST(test_clientReadyForWrite_rejects_non_writable_clients);
    RUN_TEST(test_clientReadyForWrite_allows_clients_without_availableForWrite);
    exit(UNITY_END());
}

void loop() {}
