// StoreForwardModule's history buffer, the part the module owns before the phone or a
// STORE_FORWARD request reads it. NodeDB, routing and the mesh path are covered elsewhere;
// this suite only pins historyAdd()'s admission and getNumAvailablePackets()'s filters.
//
// Regression guarded: a message addressed to the requester or broadcast is counted, our own
// outgoing copy is not, and a full ring overwrites from the start instead of growing.
#include "MeshTypes.h" // before TestUtil.h
#include "TestUtil.h"
#include <cstring>
#include <unity.h>

#include "modules/StoreForwardModule.h"

#if defined(ARCH_PORTDUINO)

namespace
{
constexpr NodeNum LOCAL = 0x11111111;
constexpr NodeNum PEER = 0x22222222;
constexpr NodeNum OTHER = 0x33333333;

meshtastic_MeshPacket makeText(NodeNum from, NodeNum to, PacketId id)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = to;
    p.id = id;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p.decoded.payload.size = 4;
    memcpy(p.decoded.payload.bytes, "msg", 4);
    return p;
}
} // namespace

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    moduleConfig.store_forward.enabled = true;
    moduleConfig.store_forward.records = 4;
}

void tearDown(void) {}

void test_historyAdd_counts_requester_and_broadcast_only(void)
{
    StoreForwardModule module;
    TEST_ASSERT_TRUE_MESSAGE(module.isServer(), "native PSRAM should let the test module initialise as a server");

    auto toPeer = makeText(OTHER, PEER, 0x1001);
    auto broadcast = makeText(LOCAL, NODENUM_BROADCAST, 0x1002);
    auto toOther = makeText(OTHER, OTHER, 0x1003);
    auto fromPeer = makeText(PEER, PEER, 0x1004);
    module.historyAdd(toPeer);
    module.historyAdd(broadcast);
    module.historyAdd(toOther);
    module.historyAdd(fromPeer);

    TEST_ASSERT_EQUAL_UINT32(2, module.getNumAvailablePackets(PEER, 0));  // toPeer + broadcast
    TEST_ASSERT_EQUAL_UINT32(1, module.getNumAvailablePackets(OTHER, 0)); // broadcast only
}

void test_historyAdd_full_ring_overwrites_from_the_start(void)
{
    StoreForwardModule module;
    TEST_ASSERT_TRUE(module.isServer());

    for (PacketId id = 1; id <= 4; id++) {
        auto p = makeText(OTHER, PEER, id);
        module.historyAdd(p);
    }
    // A different destination, so a count that resets without the write still shows slot 0's stale PEER packet.
    auto fifth = makeText(PEER, OTHER, 5);
    module.historyAdd(fifth);

    // records=4: the fifth insert resets the write index, overwrites slot 0, and leaves one entry.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.getNumAvailablePackets(PEER, 0), "slot 0 kept the overwritten packet");
    TEST_ASSERT_EQUAL_UINT32(1, module.getNumAvailablePackets(OTHER, 0));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_historyAdd_counts_requester_and_broadcast_only);
    RUN_TEST(test_historyAdd_full_ring_overwrites_from_the_start);
    exit(UNITY_END());
}

void loop() {}

#else

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

void loop() {}

#endif
