// What this node carries for other nodes, per DeviceConfig.rebroadcast_mode - the one knob that
// governs relaying, for packets it can read and for packets it cannot (a PKI unicast between two
// other nodes, a channel it does not hold). The signature policy plays no part here and is varied
// across the cases to prove it. Each expected outcome is a contract, and the assertion messages
// say what breaks if it is not met.
//
//   opaque, per mode   ALL / ALL_SKIP_DECODING / CORE_PORTNUMS_ONLY carry; KNOWN_ONLY and LOCAL_ONLY
//                      carry a PKI-shaped unicast with one known party, and nothing else - not a
//                      unicast between two strangers, not an unreadable broadcast; NONE carries
//                      nothing. Both directions are load-bearing: dropping the modes from
//                      relayOpaquePacket()'s list fails the known-party cases, and dropping the
//                      identity/PKI-shape qualifier fails the stranger and foreign-mesh cases.
//   licensed node      never carries ciphertext; carries plaintext unless a party is known unlicensed
//   header gates       hop_limit 0, id 0, someone else's next_hop, CLIENT_MUTE each stop a relay
//
// Every case builds a real PKI frame, so the suite runs only where PKI is compiled in.

#include "support/AuthPipelineHarness.h"

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)

void setUp(void)
{
    pipelineHarnessSetUp();
}

void tearDown(void)
{
    pipelineHarnessTearDown();
}

// Remote admin from the operator's node to a node behind us, both of them known to us. This is
// what a router in the field does all day. Anyone who makes this assertion fail in any mode but
// NONE is turning a stock ROUTER (whose default is CORE_PORTNUMS_ONLY) into a black hole for
// remote administration, direct messages and key verification, and owes an explanation for it.
void test_remote_admin_between_other_nodes_relays_in_every_mode(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    mockNodeDB->addNode(ADMIN_NODE);
    mockNodeDB->setPublicKey(ADMIN_NODE, admin.pub);
    mockNodeDB->markHasUser(ADMIN_NODE);
    mockNodeDB->addNode(TARGET_NODE);
    mockNodeDB->setPublicKey(TARGET_NODE, target.pub);
    mockNodeDB->markHasUser(TARGET_NODE);
    const meshtastic_MeshPacket adminPacket =
        makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADA10001, /*wantAck=*/true);

    // Sanity: the frame really is opaque to us, not merely undecodable.
    meshtastic_MeshPacket probe = adminPacket;
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::OPAQUE_RELAY_ONLY), static_cast<int>(passesRoutingAuthGate(&probe)));

    for (const auto mode : ALL_MODES) {
        const bool expect = mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
        assertOpaqueRelay(adminPacket, mode, expect,
                          expect ? "a relay must carry remote admin it cannot read - justify any change to this"
                                 : "NONE relays nothing");
    }
    // NodeDB is untouched by all of it: no last_heard, no new entries.
    TEST_ASSERT_EQUAL(0, mockNodeDB->getMeshNode(ADMIN_NODE)->last_heard);
}

// The same admin packet between two nodes we have never heard of. Modes that key on identity
// (KNOWN_ONLY, LOCAL_ONLY) decline; the port-based and unconditional modes still carry it.
void test_pki_unicast_between_strangers_relays_unless_mode_needs_identity(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE);
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    const meshtastic_MeshPacket p = makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADA20002);

    for (const auto mode : ALL_MODES) {
        const bool needsIdentity = IS_ONE_OF(mode, meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY,
                                             meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY);
        const bool expect = mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE && !needsIdentity;
        assertOpaqueRelay(p, mode, expect,
                          expect ? "a PKI unicast between strangers is still carried by a port- or unconditional-mode relay"
                                 : "this mode only carries PKI traffic with a party we know");
    }
}

// One party known is enough for KNOWN_ONLY / LOCAL_ONLY. The known party is the destination here,
// so the sender is a stranger and KNOWN_ONLY's decode short-circuit fires; the packet must still
// reach the relay decision.
void test_known_destination_satisfies_known_only_and_local_only(void)
{
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    mockNodeDB->addNode(TARGET_NODE);
    mockNodeDB->setPublicKey(TARGET_NODE, target.pub);
    mockNodeDB->markHasUser(TARGET_NODE);
    const meshtastic_MeshPacket p = makePkiUnicastBetween(admin, target, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADA30003);

    assertOpaqueRelay(p, meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY, true,
                      "KNOWN_ONLY carries a PKI unicast to a node we know, whoever sent it");
    assertOpaqueRelay(p, meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY, true,
                      "LOCAL_ONLY carries a PKI unicast to a node we know, whoever sent it");
}

// An opaque *broadcast* (a channel we do not hold) is not PKI-shaped. CORE relays it - the
// port list cannot apply to a packet with no readable port - and only KNOWN/LOCAL/NONE decline.
void test_unknown_channel_broadcast_relays_in_core_portnums_only(void)
{
    const meshtastic_MeshPacket foreign = makeUnknownChannelBroadcast(0xADA40004);
    for (const auto mode : ALL_MODES) {
        const bool expect = IS_ONE_OF(mode, meshtastic_Config_DeviceConfig_RebroadcastMode_ALL,
                                      meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING,
                                      meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY);
        pipelineRadio->reset();
        config.device.rebroadcast_mode = mode;
        meshtastic_MeshPacket copy = foreign;
        copy.id += (uint32_t)mode;
        runPipelineIngress(copy);
        while (meshtastic_MeshPacket *queued = pipelineService->getForPhone()) // phone delivery: test_packet_signing C22
            packetPool.release(queued);
        char msg[120];
        snprintf(msg, sizeof(msg), "%s: %s", modeName(mode),
                 expect ? "an unreadable broadcast is carried" : "an unreadable broadcast is not PKI-shaped, so declined");
        TEST_ASSERT_EQUAL_MESSAGE(expect ? 1 : 0, pipelineRadio->sendCalls, msg);
        TEST_ASSERT_FALSE(pipelineRouter->historyContains(&copy));
    }
}

// A licensed (ham) node must not relay traffic it cannot read - encryption is not permitted
// on its band, and it cannot tell what it is carrying. Every mode, no exceptions; its own inbound
// handling (phone delivery) is unaffected because that is about what was sent *to* it.
void test_licensed_node_never_relays_opaque_traffic(void)
{
    owner.is_licensed = true;
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    mockNodeDB->addNode(ADMIN_NODE);
    mockNodeDB->markLicenseStatus(ADMIN_NODE, true);
    mockNodeDB->addNode(TARGET_NODE);
    mockNodeDB->markLicenseStatus(TARGET_NODE, true);
    const meshtastic_MeshPacket p = makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADAB000B);
    for (const auto mode : ALL_MODES)
        assertOpaqueRelay(p, mode, false, "a licensed node does not carry ciphertext, whoever the parties are");

    const meshtastic_MeshPacket foreign = makeUnknownChannelBroadcast(0xADAB001B);
    for (const auto mode : ALL_MODES) {
        pipelineRadio->reset();
        config.device.rebroadcast_mode = mode;
        meshtastic_MeshPacket copy = foreign;
        copy.id += (uint32_t)mode;
        runPipelineIngress(copy);
        drainPhoneQueue();
        TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "a licensed node does not carry an unreadable broadcast either");
    }
}

// What a licensed node does with traffic it *can* read: it will not relay for a party it knows to
// be unlicensed, in either direction; a licensed or unknown party is carried (RoutingModule).
void test_licensed_node_relays_decoded_unless_a_party_is_known_unlicensed(void)
{
    owner.is_licensed = true;
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE);
    constexpr NodeNum LICENSED_PEER = 0x0E0E0E0E, UNLICENSED_PEER = 0x0F0F0F0F, UNKNOWN_PEER = 0x01010101;
    mockNodeDB->addNode(LICENSED_PEER);
    mockNodeDB->markLicenseStatus(LICENSED_PEER, true);
    mockNodeDB->addNode(UNLICENSED_PEER);
    mockNodeDB->markLicenseStatus(UNLICENSED_PEER, false);

    struct Case {
        NodeNum from, to;
        bool relayed;
        const char *why;
    } cases[] = {
        {LICENSED_PEER, NODENUM_BROADCAST, true, "broadcast from a licensed peer is carried"},
        {UNKNOWN_PEER, NODENUM_BROADCAST, true, "broadcast from a peer of unknown status is carried"},
        {UNLICENSED_PEER, NODENUM_BROADCAST, false, "broadcast from a known-unlicensed peer is not carried"},
        {LICENSED_PEER, UNLICENSED_PEER, false, "unicast to a known-unlicensed peer is not carried"},
        {LICENSED_PEER, UNKNOWN_PEER, true, "unicast to a peer of unknown status is carried"},
    };
    uint32_t n = 0;
    for (const auto &c : cases) {
        pipelineRadio->reset();
        pipelineRouting->reset();
        const meshtastic_MeshPacket p = makeChannelBroadcastFrom(c.from, c.to, 0xADAC0100 + ++n);
        runPipelineIngress(p);
        drainPhoneQueue();
        TEST_ASSERT_EQUAL_MESSAGE(c.relayed ? 1 : 0, pipelineRadio->sendCalls, c.why);
    }
}

// The header gates on the opaque path that never changed and must not: a spent hop budget, an
// id of 0, a next_hop naming someone else, and the CLIENT_MUTE role each stop a relay on their own.
void test_opaque_relay_header_gates_hold(void)
{
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    const meshtastic_MeshPacket base = makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADAD000D);
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;

    meshtastic_MeshPacket spent = base;
    spent.hop_limit = 0;
    runPipelineIngress(spent);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "hop_limit 0: nothing left to spend");

    pipelineRadio->reset();
    meshtastic_MeshPacket noId = base;
    noId.id = 0;
    runPipelineIngress(noId);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "id 0 cannot be deduplicated, so it is never relayed");

    pipelineRadio->reset();
    meshtastic_MeshPacket notOurHop = base;
    notOurHop.id++;
    notOurHop.next_hop = (uint8_t)(nodeDB->getLastByteOfNodeNum(LOCAL_NODE) ^ 0x01);
    runPipelineIngress(notOurHop);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "next_hop names another relay");

    pipelineRadio->reset();
    meshtastic_MeshPacket ourHop = base;
    ourHop.id += 2;
    ourHop.next_hop = nodeDB->getLastByteOfNodeNum(LOCAL_NODE);
    runPipelineIngress(ourHop);
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineRadio->sendCalls, "next_hop names us");

    pipelineRadio->reset();
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
    meshtastic_MeshPacket muted = base;
    muted.id += 3;
    runPipelineIngress(muted);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "CLIENT_MUTE relays nothing, opaque included");
}

// Hearing the same frame again is not a reason to carry it again - except from the originator, which
// only retransmits because it heard no rebroadcast of ours, and not even then while ours is queued.
void test_opaque_relay_carries_a_frame_once_unless_the_originator_repeats_it(void)
{
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    const meshtastic_MeshPacket base = makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADAE000E);
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;

    runPipelineIngress(base);
    runPipelineIngress(makeRelayedCopy(base));
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineRadio->sentCountFor(ADMIN_NODE, base.id),
                              "a neighbour's rebroadcast of a frame we carried is not carried again");

    meshtastic_MeshPacket retx = base;
    retx.hop_limit = retx.hop_start;
    runPipelineIngress(retx);
    TEST_ASSERT_EQUAL_MESSAGE(2, pipelineRadio->sentCountFor(ADMIN_NODE, base.id),
                              "the originator's own retransmission is carried again");

    // A third case - our own queued copy suppressing the repeat - needs relayOpaquePacket() to
    // consult the TX queue, which it does not do. Not this change's to add.
}

// Phone delivery of a frame we cannot read follows the same mode table as relay: LOCAL_ONLY and
// KNOWN_ONLY ignore a stranger's unknown-channel broadcast rather than hand it up. A DM to us is
// always one known party, so it reaches the phone in every mode - NONE included, which declines to
// relay but not to listen.
// None of the above depends on packet_signature_policy: the remote-admin case ran STRICT, the
// strangers case COMPATIBLE. Here the same admin packet goes under every policy in turn.
void test_relay_decision_ignores_signature_policy(void)
{
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    const meshtastic_MeshPacket p = makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADAA000A);
    const meshtastic_Config_SecurityConfig_PacketSignaturePolicy policies[] = {
        meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE,
        meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED,
        meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT,
    };
    uint32_t salt = 0;
    for (const auto policy : policies) {
        setPolicy(policy);
        meshtastic_MeshPacket copy = p;
        copy.id += 0x100 * ++salt;
        assertOpaqueRelay(copy, meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY, true,
                          "the signature policy governs what we admit, not what we carry");
    }
}

void setup()
{
    pipelineHarnessCreate();
    UNITY_BEGIN();
    RUN_TEST(test_remote_admin_between_other_nodes_relays_in_every_mode);
    RUN_TEST(test_pki_unicast_between_strangers_relays_unless_mode_needs_identity);
    RUN_TEST(test_known_destination_satisfies_known_only_and_local_only);
    RUN_TEST(test_unknown_channel_broadcast_relays_in_core_portnums_only);
    RUN_TEST(test_licensed_node_never_relays_opaque_traffic);
    RUN_TEST(test_licensed_node_relays_decoded_unless_a_party_is_known_unlicensed);
    RUN_TEST(test_opaque_relay_header_gates_hold);
    RUN_TEST(test_opaque_relay_carries_a_frame_once_unless_the_originator_repeats_it);
    RUN_TEST(test_relay_decision_ignores_signature_policy);
    const int result = UNITY_END();
    pipelineHarnessDestroy();
    exit(result);
}

void loop() {}

#else // XEdDSA or PKI excluded

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
