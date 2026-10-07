// Tests that TraceRouteModule only learns next_hop from a traceroute response when the route array
// agrees with the node that actually relayed the packet. The route is unauthenticated payload, so
// without that check a forged response could point any node's next_hop anywhere.

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "mesh/NodeDB.h"
#include "modules/TraceRouteModule.h"
#include <vector>

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A; // us, the node that asked for the traceroute
static constexpr NodeNum RELAY_B = 0x0B0B0B0B;    // the honest first hop back towards us
static constexpr NodeNum NODE_C = 0x0C0C0C0C;
static constexpr NodeNum NODE_D = 0x0D0D0D0D;        // the traceroute target, which answers
static constexpr uint8_t ATTACKER_RELAY_BYTE = 0xEE; // some node that is not RELAY_B

class MockNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        meshNodes = &testNodes;
        numMeshNodes = 0;
    }

    void addNode(NodeNum num)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    uint8_t nextHopOf(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        return n->next_hop;
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

// alterReceivedProtobuf is the real entry point; updateNextHops is private behind it.
class TraceRouteModuleTestShim : public TraceRouteModule
{
  public:
    using TraceRouteModule::alterReceivedProtobuf;
};

static MockNodeDB *mockNodeDB = nullptr;
static TraceRouteModuleTestShim *shim = nullptr;

// A traceroute response addressed to us, claiming the forward route LOCAL -> RELAY_B -> NODE_C -> NODE_D,
// carried to us by whichever node `relayByte` names.
static meshtastic_MeshPacket makeResponse(uint8_t relayByte, meshtastic_RouteDiscovery *r)
{
    *r = meshtastic_RouteDiscovery_init_zero;
    r->route_count = 3;
    r->route[0] = RELAY_B;
    r->route[1] = NODE_C;
    r->route[2] = NODE_D;

    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = NODE_D; // the target answered
    p.to = LOCAL_NODE;
    p.id = 0x1234;
    p.relay_node = relayByte;
    p.hop_start = 3;
    p.hop_limit = 1;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
    p.decoded.request_id = 0x9999; // non-zero marks this a response, which is what drives updateNextHops
    return p;
}

void setUp(void)
{
    mockNodeDB = new MockNodeDB();
    mockNodeDB->clearTestNodes();
    nodeDB = mockNodeDB;

    config = meshtastic_LocalConfig_init_zero;
    owner = meshtastic_User_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE; // drives isToUs()

    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->addNode(RELAY_B);
    mockNodeDB->addNode(NODE_C);
    mockNodeDB->addNode(NODE_D);

    shim = new TraceRouteModuleTestShim();
}

void tearDown(void)
{
    delete shim;
    shim = nullptr;
    delete mockNodeDB;
    mockNodeDB = nullptr;
    nodeDB = nullptr;
}

// The honest case: the route names RELAY_B as our next hop, and RELAY_B is who handed us the packet.
void test_nexthop_learned_when_route_matches_relay(void)
{
    meshtastic_RouteDiscovery r;
    meshtastic_MeshPacket p = makeResponse(nodeDB->getLastByteOfNodeNum(RELAY_B), &r);

    shim->alterReceivedProtobuf(p, &r);

    const uint8_t expected = nodeDB->getLastByteOfNodeNum(RELAY_B);
    TEST_ASSERT_EQUAL_MESSAGE(expected, mockNodeDB->nextHopOf(RELAY_B), "next hop for the relay itself");
    TEST_ASSERT_EQUAL_MESSAGE(expected, mockNodeDB->nextHopOf(NODE_C), "next hop for a node beyond the relay");
    TEST_ASSERT_EQUAL_MESSAGE(expected, mockNodeDB->nextHopOf(NODE_D), "next hop for the target");
}

// A forged response: the attacker transmits it themselves, so relay_node is their byte, but the route
// claims RELAY_B. Believing the payload here would let them redirect traffic for every node listed.
void test_nexthop_ignored_when_route_contradicts_relay(void)
{
    meshtastic_RouteDiscovery r;
    meshtastic_MeshPacket p = makeResponse(ATTACKER_RELAY_BYTE, &r);

    shim->alterReceivedProtobuf(p, &r);

    TEST_ASSERT_EQUAL_MESSAGE(0, mockNodeDB->nextHopOf(RELAY_B), "forged route must not set a next hop");
    TEST_ASSERT_EQUAL_MESSAGE(0, mockNodeDB->nextHopOf(NODE_C), "forged route must not set a next hop");
    TEST_ASSERT_EQUAL_MESSAGE(0, mockNodeDB->nextHopOf(NODE_D), "forged route must not set a next hop");
}

// MQTT-sourced packets carry relay_node 0, so nothing corroborates the route and we must not learn.
void test_nexthop_ignored_without_a_relay(void)
{
    meshtastic_RouteDiscovery r;
    meshtastic_MeshPacket p = makeResponse(NO_RELAY_NODE, &r);

    shim->alterReceivedProtobuf(p, &r);

    TEST_ASSERT_EQUAL_MESSAGE(0, mockNodeDB->nextHopOf(RELAY_B), "no relay means no corroboration");
    TEST_ASSERT_EQUAL_MESSAGE(0, mockNodeDB->nextHopOf(NODE_C), "no relay means no corroboration");
    TEST_ASSERT_EQUAL_MESSAGE(0, mockNodeDB->nextHopOf(NODE_D), "no relay means no corroboration");
}

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_nexthop_learned_when_route_matches_relay);
    RUN_TEST(test_nexthop_ignored_when_route_contradicts_relay);
    RUN_TEST(test_nexthop_ignored_without_a_relay);
    exit(UNITY_END());
}

void loop() {}
