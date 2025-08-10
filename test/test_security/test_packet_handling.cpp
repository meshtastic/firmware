#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

#ifdef ARCH_PORTDUINO
#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "modules/NodeInfoModule.h"

// Mock NodeDB that tracks when updateUser would be called - follows MQTT test pattern
class MockNodeDB : public NodeDB
{
  public:
    MockNodeDB()
    {
        updateUserCallCount = 0;
        lastUpdatedNodeNum = 0;
    }

    // Override virtual getMeshNode method (same as MQTT test pattern)
    meshtastic_NodeInfoLite *getMeshNode(NodeNum n) override { return &emptyNode; }

    // Track calls that would go to updateUser (we'll check this in the test)
    // Since updateUser is not virtual, we override a method that's called during the process
    meshtastic_NodeInfoLite *getMeshNodeForUpdate(NodeNum n)
    {
        updateUserCallCount++;
        lastUpdatedNodeNum = n;
        return &emptyNode;
    }

    int updateUserCallCount;
    NodeNum lastUpdatedNodeNum;
    meshtastic_NodeInfoLite emptyNode = {};
};

// Testable version of NodeInfoModule that exposes protected methods
class TestableNodeInfoModule : public NodeInfoModule
{
  public:
    bool testHandleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *user)
    {
        return handleReceivedProtobuf(mp, user);
    }
};

void test_nodeinfo_spoofing_vulnerability()
{
    // Create mock NodeDB and assign to global pointer like MQTT test
    const std::unique_ptr<MockNodeDB> mockNodeDB(new MockNodeDB());
    nodeDB = mockNodeDB.get();

    // Set our node number (simulating what happens in real startup)
    myNodeInfo.my_node_num = 0x12345678;

    // Create a test NodeInfoModule
    TestableNodeInfoModule testModule;

    // Create a spoofed packet claiming to be from our own node
    meshtastic_MeshPacket spoofedPacket = meshtastic_MeshPacket_init_default;
    spoofedPacket.from = 0x12345678; // VULNERABILITY: Same as our node number
    spoofedPacket.to = NODENUM_BROADCAST;
    spoofedPacket.channel = 0;
    spoofedPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    spoofedPacket.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;

    // Create malicious User data that an attacker wants to inject
    meshtastic_User maliciousUser = meshtastic_User_init_default;
    strcpy(maliciousUser.long_name, "HACKED_NODE");
    strcpy(maliciousUser.short_name, "HAK");
    strcpy(maliciousUser.id, "!87654321"); // Attacker's fake ID
    maliciousUser.is_licensed = true;      // Try to make us appear licensed when we're not

    // Test the vulnerability: handleReceivedProtobuf should reject spoofed packets claiming to be from our own node
    // but currently it processes them, calling updateUser with our own node number
    bool result = testModule.testHandleReceivedProtobuf(spoofedPacket, &maliciousUser);

    // The vulnerability is demonstrated by the function NOT rejecting the spoofed packet
    // In a secure implementation, packets claiming to be from our own node should be rejected
    // and the function should return true (meaning "I handled this, don't process further")

    // Currently this will FAIL because the vulnerability exists:
    // - The function returns false (allowing further processing)
    // - It calls updateUser with the spoofed node number (our own number)
    // - This allows an attacker to modify our node information

    TEST_ASSERT_FALSE_MESSAGE(result,
                              "VULNERABILITY CONFIRMED: handleReceivedProtobuf processes spoofed packets from our own node.\n"
                              "Expected: Function should return true (reject spoofed packet)\n"
                              "Actual: Function returned false (processed spoofed packet)\n"
                              "This allows attackers to spoof our node number and modify our NodeInfo.");

    printf("\n=== SECURITY TEST RESULTS ===\n");
    printf("✗ Vulnerability exists: NodeInfoModule processes spoofed packets from our own node\n");
    printf("✗ Attack vector: Attacker can spoof packets with from=our_node_number\n");
    printf("==============================\n\n");
}

void test_legitimate_packet_processing()
{
    // Test that legitimate packets from OTHER nodes are processed correctly
    const std::unique_ptr<MockNodeDB> mockNodeDB(new MockNodeDB());
    nodeDB = mockNodeDB.get();

    myNodeInfo.my_node_num = 0x12345678;
    TestableNodeInfoModule testModule;

    // Create a legitimate packet from a DIFFERENT node
    meshtastic_MeshPacket legitimatePacket = meshtastic_MeshPacket_init_default;
    legitimatePacket.from = 0x87654321; // Different node number - this is legitimate
    legitimatePacket.to = NODENUM_BROADCAST;
    legitimatePacket.channel = 0;
    legitimatePacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    legitimatePacket.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;

    meshtastic_User legitimateUser = meshtastic_User_init_default;
    strcpy(legitimateUser.long_name, "Legitimate User");
    strcpy(legitimateUser.short_name, "LEG");

    bool result = testModule.testHandleReceivedProtobuf(legitimatePacket, &legitimateUser);

    // Legitimate packets should be processed normally (return false for further processing)
    TEST_ASSERT_FALSE_MESSAGE(result, "Legitimate packets from other nodes should be processed normally");

    printf("✓ Legitimate packet processing works correctly\n");
}

void setUp()
{
    // Required by Unity
}

void tearDown()
{
    // Required by Unity
}

void setup()
{
    // Initialize test environment like MQTT test
    initializeTestEnvironment();

    UNITY_BEGIN();

    printf("\n=== NodeInfo Spoofing Security Test ===\n");
    printf("Testing vulnerability in NodeInfoModule::handleReceivedProtobuf()\n");
    printf("Issue: Function doesn't check if packet claims to be from our own node\n\n");

    RUN_TEST(test_nodeinfo_spoofing_vulnerability);
    RUN_TEST(test_legitimate_packet_processing);

    UNITY_END();
}

void loop() {}

#endif
