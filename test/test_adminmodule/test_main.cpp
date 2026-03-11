/**
 * Unit tests for AdminModule message handling optimization
 * Tests that messages from self are skipped in the default case
 */

#include "TestUtil.h"
#include <unity.h>

#ifdef ARCH_PORTDUINO

#include "mesh/MeshTypes.h"
#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

// Test helpers
static meshtastic_MeshPacket createPacket(NodeNum from, NodeNum to) {
  meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_default;
  mp.from = from;
  mp.to = to;
  mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  return mp;
}

void setUp(void) {
  // Initialize test environment
}

void tearDown(void) {
  // Cleanup
}

/**
 * Test: isFromUs() returns true when from == 0 (local request)
 */
void test_isFromUs_local_request(void) {
  meshtastic_MeshPacket mp = createPacket(0, NODENUM_BROADCAST);
  TEST_ASSERT_TRUE(isFromUs(&mp));
}

/**
 * Test: isFromUs() returns true when from == our node number
 */
void test_isFromUs_own_node(void) {
  NodeNum ourNode = nodeDB->getNodeNum();
  meshtastic_MeshPacket mp = createPacket(ourNode, NODENUM_BROADCAST);
  TEST_ASSERT_TRUE(isFromUs(&mp));
}

/**
 * Test: isFromUs() returns false when from is a different node
 */
void test_isFromUs_other_node(void) {
  NodeNum ourNode = nodeDB->getNodeNum();
  NodeNum otherNode = ourNode + 1; // Different node
  meshtastic_MeshPacket mp = createPacket(otherNode, ourNode);
  TEST_ASSERT_FALSE(isFromUs(&mp));
}

/**
 * Test: isToUs() returns true when to == our node number
 */
void test_isToUs_addressed_to_us(void) {
  NodeNum ourNode = nodeDB->getNodeNum();
  meshtastic_MeshPacket mp = createPacket(0x12345678, ourNode);
  TEST_ASSERT_TRUE(isToUs(&mp));
}

/**
 * Test: isToUs() returns false when to is broadcast
 */
void test_isToUs_broadcast(void) {
  meshtastic_MeshPacket mp = createPacket(0x12345678, NODENUM_BROADCAST);
  TEST_ASSERT_FALSE(isToUs(&mp));
}

/**
 * Test: Verify fromOthers logic used in AdminModule
 * This simulates the check: bool fromOthers = !isFromUs(&mp);
 */
void test_fromOthers_logic(void) {
  NodeNum ourNode = nodeDB->getNodeNum();

  // Message from local (from=0)
  meshtastic_MeshPacket localMsg = createPacket(0, NODENUM_BROADCAST);
  bool fromOthers1 = !isFromUs(&localMsg);
  TEST_ASSERT_FALSE(fromOthers1); // Should be false (not from others)

  // Message from ourselves
  meshtastic_MeshPacket ownMsg = createPacket(ourNode, NODENUM_BROADCAST);
  bool fromOthers2 = !isFromUs(&ownMsg);
  TEST_ASSERT_FALSE(fromOthers2); // Should be false (not from others)

  // Message from another node
  meshtastic_MeshPacket otherMsg = createPacket(ourNode + 1, ourNode);
  bool fromOthers3 = !isFromUs(&otherMsg);
  TEST_ASSERT_TRUE(fromOthers3); // Should be true (from others)
}

/**
 * Test: Verify the optimization behavior
 * When !fromOthers (message from self), we should skip processing
 */
void test_skip_self_messages_optimization(void) {
  NodeNum ourNode = nodeDB->getNodeNum();

  // Create a message from ourselves with an unhandled admin message type
  meshtastic_MeshPacket mp = createPacket(ourNode, NODENUM_BROADCAST);
  bool fromOthers = !isFromUs(&mp);

  // This is the optimization check in AdminModule default case
  bool shouldSkip = !fromOthers;
  TEST_ASSERT_TRUE(shouldSkip);

  // Now test with a message from another node
  meshtastic_MeshPacket otherMp = createPacket(ourNode + 1, ourNode);
  bool fromOthers2 = !isFromUs(&otherMp);
  bool shouldSkip2 = !fromOthers2;
  TEST_ASSERT_FALSE(shouldSkip2); // Should NOT skip messages from others
}

void setup() {
  delay(2000);

  initializeTestEnvironment();
  UNITY_BEGIN();

  RUN_TEST(test_isFromUs_local_request);
  RUN_TEST(test_isFromUs_own_node);
  RUN_TEST(test_isFromUs_other_node);
  RUN_TEST(test_isToUs_addressed_to_us);
  RUN_TEST(test_isToUs_broadcast);
  RUN_TEST(test_fromOthers_logic);
  RUN_TEST(test_skip_self_messages_optimization);

  exit(UNITY_END());
}

void loop() {}

#else
// Stub for non-Portduino builds
void setup() { exit(0); }
void loop() {}
#endif
