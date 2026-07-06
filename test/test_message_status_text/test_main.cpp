#include "MeshTypes.h"
#include "MessageStore.h"
#include "TestUtil.h"
#include "graphics/draw/MessageStatusText.h"
#include <unity.h>

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)

static StoredMessage makeMessage(AckStatus status, uint32_t dest)
{
    StoredMessage message;
    message.ackStatus = status;
    message.dest = dest;
    return message;
}

static void test_sending_text()
{
    StoredMessage message = makeMessage(AckStatus::NONE, NODENUM_BROADCAST);
    TEST_ASSERT_EQUAL_STRING("Sending...", graphics::MessageStatusText::inlineTextFor(message));
}

static void test_channel_implicit_ack_text()
{
    StoredMessage message = makeMessage(AckStatus::ACKED, NODENUM_BROADCAST);
    TEST_ASSERT_EQUAL_STRING("Delivered to mesh", graphics::MessageStatusText::inlineTextFor(message));
}

static void test_dm_explicit_ack_text()
{
    StoredMessage message = makeMessage(AckStatus::ACKED, 0x12345678);
    TEST_ASSERT_EQUAL_STRING("Delivered to recipient", graphics::MessageStatusText::inlineTextFor(message));
}

static void test_dm_relayed_ack_text()
{
    StoredMessage message = makeMessage(AckStatus::RELAYED, 0x12345678);
    TEST_ASSERT_EQUAL_STRING("Relayed, not confirmed by recipient", graphics::MessageStatusText::inlineTextFor(message));
}

static void test_failed_delivery_text()
{
    StoredMessage nacked = makeMessage(AckStatus::NACKED, 0x12345678);
    StoredMessage timeout = makeMessage(AckStatus::TIMEOUT, NODENUM_BROADCAST);
    TEST_ASSERT_EQUAL_STRING("Failed to deliver to mesh", graphics::MessageStatusText::inlineTextFor(nacked));
    TEST_ASSERT_EQUAL_STRING("Failed to deliver to mesh", graphics::MessageStatusText::inlineTextFor(timeout));
}

static void test_message_too_large_text()
{
    StoredMessage message = makeMessage(AckStatus::TOO_LARGE, 0x12345678);
    TEST_ASSERT_EQUAL_STRING("Message is too large to send", graphics::MessageStatusText::inlineTextFor(message));
}

static void test_no_channel_text()
{
    StoredMessage message = makeMessage(AckStatus::NO_CHANNEL, NODENUM_BROADCAST);
    TEST_ASSERT_EQUAL_STRING("No channel selected", graphics::MessageStatusText::inlineTextFor(message));
}

static void test_pki_failure_text()
{
    StoredMessage generic = makeMessage(AckStatus::PKI_FAILED, 0x12345678);
    StoredMessage missingRecipientKey = makeMessage(AckStatus::PKI_SEND_FAIL_PUBLIC_KEY, 0x12345678);
    StoredMessage recipientMissingSenderKey = makeMessage(AckStatus::PKI_UNKNOWN_PUBKEY, 0x12345678);

    TEST_ASSERT_EQUAL_STRING("Could not send encrypted message", graphics::MessageStatusText::inlineTextFor(generic));
    TEST_ASSERT_EQUAL_STRING("Recipient key unavailable", graphics::MessageStatusText::inlineTextFor(missingRecipientKey));
    TEST_ASSERT_EQUAL_STRING("Recipient needs your key", graphics::MessageStatusText::inlineTextFor(recipientMissingSenderKey));
}

static void test_banner_text_uses_canonical_status_wording()
{
    StoredMessage deliveredMesh = makeMessage(AckStatus::ACKED, NODENUM_BROADCAST);
    StoredMessage deliveredRecipient = makeMessage(AckStatus::ACKED, 0x12345678);
    StoredMessage relayed = makeMessage(AckStatus::RELAYED, 0x12345678);
    StoredMessage tooLarge = makeMessage(AckStatus::TOO_LARGE, 0x12345678);
    StoredMessage pkiFailed = makeMessage(AckStatus::PKI_FAILED, 0x12345678);
    StoredMessage missingRecipientKey = makeMessage(AckStatus::PKI_SEND_FAIL_PUBLIC_KEY, 0x12345678);
    StoredMessage recipientMissingSenderKey = makeMessage(AckStatus::PKI_UNKNOWN_PUBKEY, 0x12345678);

    TEST_ASSERT_EQUAL_STRING("Delivered to mesh", graphics::MessageStatusText::bannerTextFor(deliveredMesh));
    TEST_ASSERT_EQUAL_STRING("Delivered to recipient", graphics::MessageStatusText::bannerTextFor(deliveredRecipient));
    TEST_ASSERT_EQUAL_STRING("Relayed, not confirmed\nby recipient", graphics::MessageStatusText::bannerTextFor(relayed));
    TEST_ASSERT_EQUAL_STRING("Message is too large\nto send", graphics::MessageStatusText::bannerTextFor(tooLarge));
    TEST_ASSERT_EQUAL_STRING("Could not send\nencrypted message", graphics::MessageStatusText::bannerTextFor(pkiFailed));
    TEST_ASSERT_EQUAL_STRING("Recipient key\nunavailable", graphics::MessageStatusText::bannerTextFor(missingRecipientKey));
    TEST_ASSERT_EQUAL_STRING("Recipient needs\nyour key", graphics::MessageStatusText::bannerTextFor(recipientMissingSenderKey));
}

#endif

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    RUN_TEST(test_sending_text);
    RUN_TEST(test_channel_implicit_ack_text);
    RUN_TEST(test_dm_explicit_ack_text);
    RUN_TEST(test_dm_relayed_ack_text);
    RUN_TEST(test_failed_delivery_text);
    RUN_TEST(test_message_too_large_text);
    RUN_TEST(test_no_channel_text);
    RUN_TEST(test_pki_failure_text);
    RUN_TEST(test_banner_text_uses_canonical_status_wording);
#endif

    exit(UNITY_END());
}

void loop() {}
