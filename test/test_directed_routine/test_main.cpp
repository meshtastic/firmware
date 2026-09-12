// Directed routine sends. Under test: BaseTelemetryModule::routineDest(), wouldReplyToPoll() and
// pkcOnlyDestsHaveKeys() (src/modules/Telemetry/BaseTelemetryModule.h); hopLimitForDirected(),
// applyDirectedHopBudget() and wouldEncryptWithPKC() (src/mesh/Router.cpp).
//
// Contract: telemetry_flags == 0 and a zero destination are byte-for-byte today's behaviour - answer
// every poller, broadcast, PKC when the key is held. Every flag bit only restricts. A request carrying
// our own node number is the phone and is never refused. A from-us unicast on the routine ports takes
// hops_away + 2 unless something already moved it off the configured default; an MQTT-learned
// distance is not a LoRa path. Telemetry and paxcounter to a destination with no stored key go
// channel-PSK unless ALWAYS_PKC, which the admin gate refuses without a key.
//
// Regressions guarded: the reply policy locking the phone out of its own node (81b019488); ALWAYS_PKC
// and UNSET being indistinguishable because the encoder had no fallback; a reply sized by
// getHopLimitForResponse() being trimmed a second time.
#include "Default.h"
#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/Telemetry/BaseTelemetryModule.h"
#include <unity.h>

static NodeDB *testNodeDB = nullptr;

namespace
{
constexpr NodeNum LOCAL_NODE = 0x11111111;
constexpr NodeNum POLLER = 0x22222222;
constexpr NodeNum OTHER = 0x33333333;
constexpr NodeNum DEST = 0x44444444;

using Flags = meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags;

void clearFlags()
{
    moduleConfig.telemetry.telemetry_flags = 0;
}

/// Put `num` in the node DB with the given attributes, returning the entry.
meshtastic_NodeInfoLite *ensureNode(NodeNum num, bool favorite = false, bool ignored = false)
{
    meshtastic_NodeInfoLite *n = nodeDB->getOrCreateMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_IS_FAVORITE_MASK, favorite);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_IS_IGNORED_MASK, ignored);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_VIA_MQTT_MASK, false);
    n->has_hops_away = false;
    n->hops_away = 0;
    return n;
}
} // namespace

// --- routineDest: 0 means broadcast, anything else addresses that node -------------------------

void test_routineDest_zeroIsBroadcast()
{
    TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, BaseTelemetryModule::routineDest(0));
}

void test_routineDest_nonZeroIsThatNode()
{
    TEST_ASSERT_EQUAL_UINT32(DEST, BaseTelemetryModule::routineDest(DEST));
}

// --- wouldReplyToPoll --------------------------------------------------------------------------

// Unset flags must behave exactly as the firmware did before this feature: answer everyone.
void test_reply_unsetAnswersEveryone()
{
    clearFlags();
    ensureNode(POLLER);
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
    // Also true for a node we have never heard of.
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(0x5a5a5a5a, 0));
}

void test_reply_noAdhocRefusesEveryone()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/true);
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_NO_ADHOC_REPLY;
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
}

void test_reply_onlyToDest()
{
    clearFlags();
    ensureNode(POLLER);
    ensureNode(OTHER);
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_ONLY_TO_DEST;
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(OTHER, POLLER));
    // With no destination configured the restriction cannot be satisfied by anyone.
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
}

void test_reply_favouritesOnly()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/true);
    ensureNode(OTHER, /*favorite=*/false);
    moduleConfig.telemetry.telemetry_flags =
        Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_TO_FAVOURITES_ONLY;
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(OTHER, 0));
    // A node not in the DB at all is not a favourite - isFavorite() must fail closed.
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(0x5a5a5a5a, 0));
}

// Ignoring a node is not a policy choice, so it must hold even with no flags set.
void test_reply_ignoredRefusedEvenWithFlagsUnset()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/false, /*ignored=*/true);
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
}

// An ignored node stays refused even when it is also the configured destination.
void test_reply_ignoredBeatsDest()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/true, /*ignored=*/true);
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_ONLY_TO_DEST;
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
}

// The phone reaches its own node as a request from our own node number (MeshModule::callModules
// answers those on purpose). The flags govern mesh pollers, so none of them may lock the phone out.
void test_reply_ownNodeBypassesEveryFlag()
{
    clearFlags();
    moduleConfig.telemetry.telemetry_flags =
        Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_NO_ADHOC_REPLY |
        Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_ONLY_TO_DEST |
        Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_TO_FAVOURITES_ONLY;
    // Not in the DB, not a favourite, not the destination: every flag would refuse a mesh node here.
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(LOCAL_NODE, DEST));
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(OTHER, DEST));
}

// --- wouldEncryptWithPKC: the telemetry_flags crypto table ------------------------------------
//
// The encoder has no channel-PSK fallback of its own: a PKC candidate without a destination key is
// refused outright (perhapsEncode, PKI_SEND_FAIL_PUBLIC_KEY). The fallback lives in this function,
// scoped to TELEMETRY_APP and PAXCOUNTER_APP, so the table below is the whole policy. Rows: flags x
// key-known. A "true" here with no key means the send will fail - that is what ALWAYS_PKC buys.

meshtastic_MeshPacket unicastOnPort(meshtastic_PortNum port, bool clientAskedPki = false)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = LOCAL_NODE;
    p.to = DEST;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = port;
    p.pki_encrypted = clientAskedPki;
    return p;
}

void armPki()
{
    config.security.private_key.size = 32;
    owner.is_licensed = false;
}

void test_pkc_unsetUsesKeyWhenKnown()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
}

// The row that used to be a silent failure: no key and no flags now means PSK, not nothing.
void test_pkc_unsetFallsBackToPskWithoutKey()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

void test_pkc_alwaysPkcRefusesToDowngrade()
{
    clearFlags();
    armPki();
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_ALWAYS_PKC;
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false)); // will fail at encode, by design
}

void test_pkc_neverPkcUsesPskEvenWithKey()
{
    clearFlags();
    armPki();
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_NEVER_PKC;
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

void test_pkc_alwaysBeatsNever()
{
    clearFlags();
    armPki();
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_ALWAYS_PKC |
                                             Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_NEVER_PKC;
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// A client that set pki_encrypted itself keeps the upstream refusal: never downgrade an explicit ask.
void test_pkc_clientExplicitPkiIsNotDowngraded()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP, /*clientAskedPki=*/true);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// Paxcounter has no flags field of its own; it follows telemetry_flags.
void test_pkc_paxcounterFollowsFlags()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_PAXCOUNTER_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_ALWAYS_PKC;
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// The fallback is scoped: a text DM without a key is still a PKC candidate, so the encoder refuses it.
void test_pkc_fallbackScopedToTelemetryPorts()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TEXT_MESSAGE_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// --- pkcOnlyDestsHaveKeys: the admin gate ------------------------------------------------------
//
// ALWAYS_PKC with a destination we hold no key for is a node that goes silent on every interval. The
// gate refuses the config instead. Same lookup as the encoder (copyPublicKey), so gate and send agree.

void giveKey(NodeNum num)
{
    meshtastic_NodeInfoLite *n = ensureNode(num);
    n->public_key.size = 32;
    memset(n->public_key.bytes, 0x5a, 32);
}

void test_gate_noAlwaysPkcAcceptsAnything()
{
    meshtastic_ModuleConfig_TelemetryConfig t = meshtastic_ModuleConfig_TelemetryConfig_init_zero;
    t.device_dest = 0x5a5a5a5a; // not in the DB at all
    TEST_ASSERT_TRUE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t, 0x5a5a5a5b));
}

void test_gate_alwaysPkcAcceptsKeyedDest()
{
    giveKey(DEST);
    meshtastic_ModuleConfig_TelemetryConfig t = meshtastic_ModuleConfig_TelemetryConfig_init_zero;
    t.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_ALWAYS_PKC;
    t.device_dest = DEST;
    t.environment_dest = 0; // unset destinations are not checked
    TEST_ASSERT_TRUE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t, DEST));
}

void test_gate_alwaysPkcRefusesKeylessDest()
{
    meshtastic_NodeInfoLite *n = ensureNode(OTHER);
    n->public_key.size = 0;
    meshtastic_ModuleConfig_TelemetryConfig t = meshtastic_ModuleConfig_TelemetryConfig_init_zero;
    t.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_ALWAYS_PKC;
    t.health_dest = OTHER; // in the DB, no key
    TEST_ASSERT_FALSE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t, 0));
    t.health_dest = 0;
    t.power_dest = 0x5a5a5a5a; // not in the DB
    TEST_ASSERT_FALSE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t, 0));
}

// Paxcounter arrives in its own admin message, so its destination is checked against the stored flags.
void test_gate_alwaysPkcCoversPaxcounterDest()
{
    meshtastic_ModuleConfig_TelemetryConfig t = meshtastic_ModuleConfig_TelemetryConfig_init_zero;
    t.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_ALWAYS_PKC;
    TEST_ASSERT_FALSE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t, 0x5a5a5a5a));
    giveKey(DEST);
    TEST_ASSERT_TRUE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t, DEST));
}

// --- hopLimitForDirected -----------------------------------------------------------------------

void test_hop_unknownDistanceUsesConfigured()
{
    ensureNode(DEST); // has_hops_away stays false
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 3));
    // A node absent from the DB likewise has no basis to trim.
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(0x5a5a5a5a, 3));
}

void test_hop_directNeighbourKeepsReturnMargin()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    // 0 + 2 < 5, so trim to 2 - the margin survives even for a direct neighbour.
    TEST_ASSERT_EQUAL_UINT8(2, hopLimitForDirected(DEST, 5));
}

// The boundary an off-by-one would flip: 1 + 2 is not < 3, so the configured cap applies.
void test_hop_boundaryAtConfiguredLimit()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 1;
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 3));
    // With more room the same distance does trim.
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 7));
}

void test_hop_neverExceedsConfigured()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 6;
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 3));
}

// hops_away is stored without a via_mqtt guard, so a distance learned over MQTT may not describe a
// LoRa path at all. It must not be used to size a LoRa hop budget.
void test_hop_mqttDistanceIsNotTrusted()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_VIA_MQTT_MASK, true);
    TEST_ASSERT_EQUAL_UINT8(5, hopLimitForDirected(DEST, 5));
}

// --- applyDirectedHopBudget: the guard around hopLimitForDirected() in Router::send() ---------
//
// hopLimitForDirected() is tested above; these pin what send() does with it. The trap is the
// "still at the configured default" guard: a reply that getHopLimitForResponse() already sized, or a
// client that chose its own hop_limit, must not be trimmed a second time.

meshtastic_MeshPacket packetAtDefaultHops(meshtastic_PortNum port, NodeNum to)
{
    meshtastic_MeshPacket p = unicastOnPort(port);
    p.to = to;
    p.hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
    return p;
}

void test_budget_trimsUnicastAtDefault()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    config.lora.hop_limit = 5;
    meshtastic_MeshPacket p = packetAtDefaultHops(meshtastic_PortNum_TELEMETRY_APP, DEST);
    applyDirectedHopBudget(&p);
    TEST_ASSERT_EQUAL_UINT8(2, p.hop_limit);
}

void test_budget_leavesAlreadySizedPacketAlone()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    config.lora.hop_limit = 5;
    meshtastic_MeshPacket p = packetAtDefaultHops(meshtastic_PortNum_TELEMETRY_APP, DEST);
    p.hop_limit = 4; // off the default: a reply or a client choice
    applyDirectedHopBudget(&p);
    TEST_ASSERT_EQUAL_UINT8(4, p.hop_limit);
}

void test_budget_ignoresBroadcastAndOtherPorts()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    config.lora.hop_limit = 5;
    meshtastic_MeshPacket b = packetAtDefaultHops(meshtastic_PortNum_TELEMETRY_APP, NODENUM_BROADCAST);
    applyDirectedHopBudget(&b);
    TEST_ASSERT_EQUAL_UINT8(5, b.hop_limit);
    meshtastic_MeshPacket t = packetAtDefaultHops(meshtastic_PortNum_TEXT_MESSAGE_APP, DEST);
    applyDirectedHopBudget(&t);
    TEST_ASSERT_EQUAL_UINT8(5, t.hop_limit);
}

// Not only module sends: a phone-originated unicast on these ports at the default is trimmed too.
void test_budget_coversPhoneOriginatedSends()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    config.lora.hop_limit = 5;
    meshtastic_MeshPacket p = packetAtDefaultHops(meshtastic_PortNum_NODEINFO_APP, DEST);
    p.from = 0; // phone leaves from unset; isFromUs() treats 0 as us
    applyDirectedHopBudget(&p);
    TEST_ASSERT_EQUAL_UINT8(2, p.hop_limit);
}

void setUp(void)
{
    if (!testNodeDB)
        testNodeDB = new NodeDB();
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE;
    nodeDB = testNodeDB;
}

void tearDown(void) {}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_routineDest_zeroIsBroadcast);
    RUN_TEST(test_routineDest_nonZeroIsThatNode);
    RUN_TEST(test_reply_unsetAnswersEveryone);
    RUN_TEST(test_reply_noAdhocRefusesEveryone);
    RUN_TEST(test_reply_onlyToDest);
    RUN_TEST(test_reply_favouritesOnly);
    RUN_TEST(test_reply_ignoredRefusedEvenWithFlagsUnset);
    RUN_TEST(test_reply_ignoredBeatsDest);
    RUN_TEST(test_reply_ownNodeBypassesEveryFlag);
    RUN_TEST(test_hop_unknownDistanceUsesConfigured);
    RUN_TEST(test_hop_directNeighbourKeepsReturnMargin);
    RUN_TEST(test_hop_boundaryAtConfiguredLimit);
    RUN_TEST(test_hop_neverExceedsConfigured);
    RUN_TEST(test_hop_mqttDistanceIsNotTrusted);
    RUN_TEST(test_pkc_unsetUsesKeyWhenKnown);
    RUN_TEST(test_pkc_unsetFallsBackToPskWithoutKey);
    RUN_TEST(test_pkc_alwaysPkcRefusesToDowngrade);
    RUN_TEST(test_pkc_neverPkcUsesPskEvenWithKey);
    RUN_TEST(test_pkc_alwaysBeatsNever);
    RUN_TEST(test_pkc_clientExplicitPkiIsNotDowngraded);
    RUN_TEST(test_pkc_paxcounterFollowsFlags);
    RUN_TEST(test_pkc_fallbackScopedToTelemetryPorts);
    RUN_TEST(test_gate_noAlwaysPkcAcceptsAnything);
    RUN_TEST(test_gate_alwaysPkcAcceptsKeyedDest);
    RUN_TEST(test_gate_alwaysPkcRefusesKeylessDest);
    RUN_TEST(test_gate_alwaysPkcCoversPaxcounterDest);
    RUN_TEST(test_budget_trimsUnicastAtDefault);
    RUN_TEST(test_budget_leavesAlreadySizedPacketAlone);
    RUN_TEST(test_budget_ignoresBroadcastAndOtherPorts);
    RUN_TEST(test_budget_coversPhoneOriginatedSends);
    exit(UNITY_END());
}

void loop() {}
