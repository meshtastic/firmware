#include "MeshTypes.h"
#include "MessageStore.h"
#include "TestUtil.h"
#include "graphics/draw/MessageStatusText.h"
#include <unity.h>

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)

static StoredMessage makeMessage(AckStatus status, uint32_t dest, bool ackTrackable = true)
{
    StoredMessage message;
    message.ackStatus = status;
    message.dest = dest;
    message.ackTrackable = ackTrackable;
    return message;
}

static void assertAckStatus(AckStatus expected, AckStatus actual)
{
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual));
}

static void test_sending_text()
{
    StoredMessage message = makeMessage(AckStatus::NONE, NODENUM_BROADCAST);
    TEST_ASSERT_EQUAL_STRING("Sending...", graphics::MessageStatusText::inlineTextFor(message));
}

static void test_untracked_pending_status_has_no_inline_text()
{
    StoredMessage message = makeMessage(AckStatus::NONE, 0x12345678, false);
    TEST_ASSERT_EQUAL_STRING("", graphics::MessageStatusText::inlineTextFor(message));
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

static void test_routing_results_map_to_ack_status()
{
    assertAckStatus(AckStatus::ACKED, ackStatusForRoutingResult(true, false, true, meshtastic_Routing_Error_NONE));
    assertAckStatus(AckStatus::ACKED, ackStatusForRoutingResult(false, true, true, meshtastic_Routing_Error_NONE));
    assertAckStatus(AckStatus::RELAYED, ackStatusForRoutingResult(false, false, true, meshtastic_Routing_Error_NONE));

    assertAckStatus(AckStatus::TOO_LARGE, ackStatusForRoutingResult(false, false, false, meshtastic_Routing_Error_TOO_LARGE));
    assertAckStatus(AckStatus::TIMEOUT, ackStatusForRoutingResult(false, false, false, meshtastic_Routing_Error_MAX_RETRANSMIT));
    assertAckStatus(AckStatus::NO_CHANNEL, ackStatusForRoutingResult(false, false, false, meshtastic_Routing_Error_NO_CHANNEL));
    assertAckStatus(AckStatus::PKI_SEND_FAIL_PUBLIC_KEY,
                    ackStatusForRoutingResult(false, false, false, meshtastic_Routing_Error_PKI_SEND_FAIL_PUBLIC_KEY));
    assertAckStatus(AckStatus::PKI_UNKNOWN_PUBKEY,
                    ackStatusForRoutingResult(false, false, false, meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY));
    assertAckStatus(AckStatus::PKI_FAILED, ackStatusForRoutingResult(false, false, false, meshtastic_Routing_Error_PKI_FAILED));
    assertAckStatus(AckStatus::NACKED, ackStatusForRoutingResult(false, false, false, meshtastic_Routing_Error_GOT_NAK));
}

static void test_message_store_updates_matching_packet_id()
{
    constexpr NodeNum localNode = 0x11111111;
    constexpr PacketId targetPacket = 0xaaaa0001;
    constexpr PacketId newerPacket = 0xbbbb0002;

    MessageStore store("status_update_target");

    StoredMessage target;
    target.sender = localNode;
    target.packetId = targetPacket;
    target.ackStatus = AckStatus::NONE;
    target.ackTrackable = true;
    store.addLiveMessage(target);

    StoredMessage newer;
    newer.sender = localNode;
    newer.packetId = newerPacket;
    newer.ackStatus = AckStatus::NONE;
    newer.ackTrackable = true;
    store.addLiveMessage(newer);

    TEST_ASSERT_TRUE(store.updateAckStatus(localNode, targetPacket, AckStatus::ACKED));

    const auto &messages = store.getMessages();
    TEST_ASSERT_EQUAL_UINT32(2U, messages.size());
    assertAckStatus(AckStatus::ACKED, messages.front().ackStatus);
    assertAckStatus(AckStatus::NONE, messages.back().ackStatus);
}

static void test_message_store_does_not_update_untracked_pending_message()
{
    constexpr NodeNum localNode = 0x11111111;
    constexpr PacketId packetId = 0xaaaa0001;

    MessageStore store("status_update_untracked");

    StoredMessage message;
    message.sender = localNode;
    message.packetId = packetId;
    message.ackStatus = AckStatus::NONE;
    message.ackTrackable = false;
    store.addLiveMessage(message);

    TEST_ASSERT_FALSE(store.updateAckStatus(localNode, packetId, AckStatus::ACKED));

    const auto &messages = store.getMessages();
    TEST_ASSERT_EQUAL_UINT32(1U, messages.size());
    assertAckStatus(AckStatus::NONE, messages.front().ackStatus);
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
    RUN_TEST(test_untracked_pending_status_has_no_inline_text);
    RUN_TEST(test_channel_implicit_ack_text);
    RUN_TEST(test_dm_explicit_ack_text);
    RUN_TEST(test_dm_relayed_ack_text);
    RUN_TEST(test_failed_delivery_text);
    RUN_TEST(test_message_too_large_text);
    RUN_TEST(test_no_channel_text);
    RUN_TEST(test_pki_failure_text);
    RUN_TEST(test_banner_text_uses_canonical_status_wording);
    RUN_TEST(test_routing_results_map_to_ack_status);
    RUN_TEST(test_message_store_updates_matching_packet_id);
    RUN_TEST(test_message_store_does_not_update_untracked_pending_message);
#endif

    exit(UNITY_END());
}

void loop() {}
