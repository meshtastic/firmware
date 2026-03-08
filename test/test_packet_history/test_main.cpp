#include "PacketHistory.h"
#include "TestUtil.h"
#include <memory>
#include <unity.h>

namespace
{
class MockNodeDB : public NodeDB
{
  public:
    meshtastic_NodeInfoLite *getMeshNode(NodeNum n) override
    {
        for (auto &node : nodes_) {
            if (node.num == n) {
                return &node;
            }
        }
        return nullptr;
    }

    void addNode(NodeNum n)
    {
        meshtastic_NodeInfoLite node = {};
        node.num = n;
        nodes_.push_back(node);
    }

  private:
    std::vector<meshtastic_NodeInfoLite> nodes_;
};

meshtastic_MeshPacket makePacket(NodeNum from, PacketId id, NodeNum to, uint8_t relayNode, uint8_t nextHop)
{
    meshtastic_MeshPacket packet = {};
    packet.from = from;
    packet.id = id;
    packet.to = to;
    packet.relay_node = relayNode;
    packet.next_hop = nextHop;
    packet.hop_start = 3;
    packet.hop_limit = 2;
    return packet;
}

constexpr NodeNum kOurNodeNum = 0x01020344;
constexpr NodeNum kSenderNodeNum = 0x01020311;
constexpr NodeNum kDestNodeNum = 0x01020399;
constexpr PacketId kPacketId = 0x12345678;
constexpr uint8_t kDirectedRelayId = 0x22;
constexpr uint8_t kDirectedNextHop = 0x55;
constexpr uint8_t kFallbackRelayId = 0x66;

std::unique_ptr<MockNodeDB> mockNodeDB;
} // namespace

void setUp(void)
{
    mockNodeDB.reset(new MockNodeDB());
    nodeDB = mockNodeDB.get();
    myNodeInfo.my_node_num = kOurNodeNum;
    mockNodeDB->addNode(kSenderNodeNum);
    mockNodeDB->addNode(kDestNodeNum);
}

void tearDown(void)
{
    nodeDB = nullptr;
    mockNodeDB.reset();
}

void test_packetHistory_offPathDirectedDuplicate_isSuppressed()
{
    PacketHistory history;
    meshtastic_MeshPacket directed = makePacket(kSenderNodeNum, kPacketId, kDestNodeNum, kDirectedRelayId, kDirectedNextHop);

    TEST_ASSERT_FALSE(history.wasSeenRecently(&directed, true));
    TEST_ASSERT_TRUE(history.wasSeenRecently(&directed, true));
}

void test_packetHistory_offPathDirectedRecord_allowsFloodFallback()
{
    PacketHistory history;
    meshtastic_MeshPacket directed = makePacket(kSenderNodeNum, kPacketId, kDestNodeNum, kDirectedRelayId, kDirectedNextHop);
    meshtastic_MeshPacket fallback =
        makePacket(kSenderNodeNum, kPacketId, kDestNodeNum, kFallbackRelayId, NO_NEXT_HOP_PREFERENCE);
    bool wasFallback = false;

    TEST_ASSERT_FALSE(history.wasSeenRecently(&directed, true));
    TEST_ASSERT_TRUE(history.wasSeenRecently(&fallback, true, &wasFallback));
    TEST_ASSERT_TRUE(wasFallback);
}

void test_packetHistory_destinationDirectedDuplicate_isStillSuppressed()
{
    PacketHistory history;
    meshtastic_MeshPacket directed = makePacket(kSenderNodeNum, kPacketId, kOurNodeNum, kDirectedRelayId, kDirectedNextHop);
    meshtastic_MeshPacket fallback = makePacket(kSenderNodeNum, kPacketId, kOurNodeNum, kFallbackRelayId, NO_NEXT_HOP_PREFERENCE);
    bool wasFallback = false;

    TEST_ASSERT_FALSE(history.wasSeenRecently(&directed, true));
    TEST_ASSERT_TRUE(history.wasSeenRecently(&fallback, true, &wasFallback));
    TEST_ASSERT_FALSE(wasFallback);
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_packetHistory_offPathDirectedDuplicate_isSuppressed);
    RUN_TEST(test_packetHistory_offPathDirectedRecord_allowsFloodFallback);
    RUN_TEST(test_packetHistory_destinationDirectedDuplicate_isStillSuppressed);
    exit(UNITY_END());
}

void loop() {}
