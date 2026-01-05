/**
 * Unit tests for AckBatcher - ACK batching feature
 *
 * Tests the batched ACK encoding and parsing logic.
 */
#include "mesh/AckBatcher.h"

#include "../TestUtil.h"
#include <unity.h>

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

/**
 * Test that isBatchedAckPacket correctly identifies batched ACK packets
 */
void test_isBatchedAckPacket_ValidPacket(void) {
  // Valid batched ACK packet: MAGIC (0xBA) + VERSION (0x01) + COUNT (1) + ENTRY
  // (5 bytes)
  uint8_t payload[] = {0xBA, 0x01, 0x01, 0x12, 0x34, 0x56, 0x78, 0x00};

  TEST_ASSERT_TRUE(AckBatcher::isBatchedAckPacket(payload, sizeof(payload)));
}

void test_isBatchedAckPacket_WrongMagic(void) {
  // Wrong magic byte
  uint8_t payload[] = {0xBB, 0x01, 0x01, 0x12, 0x34, 0x56, 0x78, 0x00};

  TEST_ASSERT_FALSE(AckBatcher::isBatchedAckPacket(payload, sizeof(payload)));
}

void test_isBatchedAckPacket_WrongVersion(void) {
  // Wrong version byte
  uint8_t payload[] = {0xBA, 0x02, 0x01, 0x12, 0x34, 0x56, 0x78, 0x00};

  TEST_ASSERT_FALSE(AckBatcher::isBatchedAckPacket(payload, sizeof(payload)));
}

void test_isBatchedAckPacket_TooSmall(void) {
  // Packet too small (less than 8 bytes minimum)
  uint8_t payload[] = {0xBA, 0x01, 0x01};

  TEST_ASSERT_FALSE(AckBatcher::isBatchedAckPacket(payload, sizeof(payload)));
}

/**
 * Test batched ACK parsing
 */
void test_parseBatchedAck_SingleEntry(void) {
  // Build a valid packet
  meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
  p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  p.from = 0x1234;

  // Payload: MAGIC + VERSION + COUNT(1) + PACKET_ID(LE) + ERROR
  uint8_t payload[] = {
      0xBA,                   // Magic
      0x01,                   // Version
      0x01,                   // Count = 1
      0xEF, 0xBE, 0xAD, 0xDE, // PacketId = 0xDEADBEEF (little-endian)
      0x00                    // Error = NONE
  };
  memcpy(p.decoded.payload.bytes, payload, sizeof(payload));
  p.decoded.payload.size = sizeof(payload);

  std::vector<AckBatcher::BatchedAckEntry> entries;
  TEST_ASSERT_TRUE(AckBatcher::parseBatchedAck(&p, entries));
  TEST_ASSERT_EQUAL(1, entries.size());
  TEST_ASSERT_EQUAL(0xDEADBEEF, entries[0].id);
  TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, entries[0].error);
}

void test_parseBatchedAck_MultipleEntries(void) {
  meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
  p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  p.from = 0x1234;

  // Payload with 3 ACKs
  uint8_t payload[] = {
      0xBA,                   // Magic
      0x01,                   // Version
      0x03,                   // Count = 3
      0x01, 0x00, 0x00, 0x00, // PacketId = 1
      0x00,                   // Error = NONE
      0x02, 0x00, 0x00, 0x00, // PacketId = 2
      0x00,                   // Error = NONE
      0x03, 0x00, 0x00, 0x00, // PacketId = 3
      0x01,                   // Error = TOO_LARGE (1)
  };
  memcpy(p.decoded.payload.bytes, payload, sizeof(payload));
  p.decoded.payload.size = sizeof(payload);

  std::vector<AckBatcher::BatchedAckEntry> entries;
  TEST_ASSERT_TRUE(AckBatcher::parseBatchedAck(&p, entries));
  TEST_ASSERT_EQUAL(3, entries.size());
  TEST_ASSERT_EQUAL(1, entries[0].id);
  TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, entries[0].error);
  TEST_ASSERT_EQUAL(2, entries[1].id);
  TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, entries[1].error);
  TEST_ASSERT_EQUAL(3, entries[2].id);
  TEST_ASSERT_EQUAL(meshtastic_Routing_Error_TOO_LARGE, entries[2].error);
}

void test_parseBatchedAck_MalformedPayload(void) {
  meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
  p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;

  // Payload claims 2 entries but only has data for 1
  uint8_t payload[] = {
      0xBA,                   // Magic
      0x01,                   // Version
      0x02,                   // Count = 2 (but we only provide 1)
      0x01, 0x00, 0x00, 0x00, // PacketId = 1
      0x00                    // Error = NONE
  };
  memcpy(p.decoded.payload.bytes, payload, sizeof(payload));
  p.decoded.payload.size = sizeof(payload);

  std::vector<AckBatcher::BatchedAckEntry> entries;
  TEST_ASSERT_FALSE(AckBatcher::parseBatchedAck(&p, entries));
}

/**
 * Test AckBatcher queuing and counts
 */
void test_AckBatcher_QueueAndCount(void) {
  AckBatcher batcher;
  batcher.setEnabled(true);

  TEST_ASSERT_EQUAL(0, batcher.getPendingCount());

  batcher.queueAck(0x1234, 100, 0, 3, meshtastic_Routing_Error_NONE);
  TEST_ASSERT_EQUAL(1, batcher.getPendingCount());

  batcher.queueAck(0x1234, 101, 0, 3, meshtastic_Routing_Error_NONE);
  TEST_ASSERT_EQUAL(2, batcher.getPendingCount());

  batcher.queueAck(0x5678, 200, 0, 3, meshtastic_Routing_Error_NONE);
  TEST_ASSERT_EQUAL(3, batcher.getPendingCount());
}

void test_AckBatcher_EnableDisable(void) {
  AckBatcher batcher;

  // Should be disabled by default
  TEST_ASSERT_FALSE(batcher.isEnabled());

  batcher.setEnabled(true);
  TEST_ASSERT_TRUE(batcher.isEnabled());

  batcher.setEnabled(false);
  TEST_ASSERT_FALSE(batcher.isEnabled());
}

void setup() {
  // NOTE!!! Wait for >2 secs
  // if board doesn't support software reset via Serial.DTR/RTS
  delay(10);
  delay(2000);

  initializeTestEnvironment();
  UNITY_BEGIN();

  // isBatchedAckPacket tests
  RUN_TEST(test_isBatchedAckPacket_ValidPacket);
  RUN_TEST(test_isBatchedAckPacket_WrongMagic);
  RUN_TEST(test_isBatchedAckPacket_WrongVersion);
  RUN_TEST(test_isBatchedAckPacket_TooSmall);

  // parseBatchedAck tests
  RUN_TEST(test_parseBatchedAck_SingleEntry);
  RUN_TEST(test_parseBatchedAck_MultipleEntries);
  RUN_TEST(test_parseBatchedAck_MalformedPayload);

  // AckBatcher instance tests
  RUN_TEST(test_AckBatcher_QueueAndCount);
  RUN_TEST(test_AckBatcher_EnableDisable);

  exit(UNITY_END());
}

void loop() {}
