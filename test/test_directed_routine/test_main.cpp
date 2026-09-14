// Directed routine sends. Under test: portPolicyFlags(), replyPolicyAllows() and pkcAlwaysDestsHaveKeys()
// (src/mesh/PortPolicy.h); BaseTelemetryModule::
// routineDest(), wouldReplyToPoll() and pkcOnlyDestsHaveKeys() (src/modules/Telemetry/BaseTelemetryModule.h);
// hopLimitForDirected(), applyDirectedHopBudget() and wouldEncryptWithPKC() (src/mesh/Router.cpp).
//
// Contract: policy_flags == 0 and a zero destination are byte-for-byte today's behaviour - answer
// every poller, broadcast, PKC when the key is held. Every flag bit only restricts. A request carrying
// our own node number is the phone and is never refused. One policy_flags per port: position,
// telemetry, paxcounter and neighbour info, read by the same helpers in src/mesh/PortPolicy.h. A from-us unicast on the routine
// ports takes hops_away + 2 unless something already moved it off the configured default; an MQTT-learned distance is not a LoRa
// path. Telemetry and paxcounter to a destination with no stored key go channel-PSK unless PKC_ALWAYS, which the admin gate
// refuses without a key.
//
// Regressions guarded: the reply policy locking the phone out of its own node (81b019488); PKC_ALWAYS
// and UNSET being indistinguishable because the encoder had no fallback; a reply sized by
// getHopLimitForResponse() being trimmed a second time.
#include "Default.h"
#include "TestUtil.h"
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "mesh/PortPolicy.h"
#include "mesh/Router.h"
#include "modules/PositionModule.h"
#include "modules/Telemetry/BaseTelemetryModule.h"
#include <unity.h>

static NodeDB *testNodeDB = nullptr;

namespace
{
constexpr NodeNum LOCAL_NODE = 0x11111111;
constexpr NodeNum POLLER = 0x22222222;
constexpr NodeNum OTHER = 0x33333333;
constexpr NodeNum DEST = 0x44444444;

void clearFlags()
{
    moduleConfig.telemetry.policy_flags = 0;
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
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_NO_ADHOC_REPLY;
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
}

void test_reply_onlyToDest()
{
    clearFlags();
    ensureNode(POLLER);
    ensureNode(OTHER);
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_REPLY_ONLY_TO_DEST;
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
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_REPLY_TO_FAVOURITES_ONLY;
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
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_REPLY_ONLY_TO_DEST;
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
}

// The phone reaches its own node as a request from our own node number (MeshModule::callModules
// answers those on purpose). The flags govern mesh pollers, so none of them may lock the phone out.
void test_reply_ownNodeBypassesEveryFlag()
{
    clearFlags();
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_NO_ADHOC_REPLY |
                                          meshtastic_PortPolicyFlags_REPLY_ONLY_TO_DEST |
                                          meshtastic_PortPolicyFlags_REPLY_TO_FAVOURITES_ONLY;
    // Not in the DB, not a favourite, not the destination: every flag would refuse a mesh node here.
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(LOCAL_NODE, DEST));
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(OTHER, DEST));
}

// --- replyPolicyAllows: the same gate, read from each port's own config -----------------------
//
// Position, neighbour info and paxcounter each carry their own policy_flags; the telemetry wrapper
// above is the same function with moduleConfig.telemetry's flags. One port's policy must not
// leak into another's.

void test_policy_perPortFlagsAreIndependent()
{
    clearFlags();
    ensureNode(POLLER);
    config.position.policy_flags = meshtastic_PortPolicyFlags_NO_ADHOC_REPLY;
    TEST_ASSERT_FALSE(replyPolicyAllows(config.position.policy_flags, POLLER, config.position.position_dest));
    // Telemetry, neighbour info and paxcounter are still open.
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
    TEST_ASSERT_TRUE(replyPolicyAllows(moduleConfig.neighbor_info.policy_flags, POLLER, 0));
    TEST_ASSERT_TRUE(replyPolicyAllows(moduleConfig.paxcounter.policy_flags, POLLER, moduleConfig.paxcounter.paxcounter_dest));
}

// Neighbour info has no routine destination, so REPLY_ONLY_TO_DEST there admits nobody but the phone.
void test_policy_onlyToDestWithoutDestRefusesMesh()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/true);
    moduleConfig.neighbor_info.policy_flags = meshtastic_PortPolicyFlags_REPLY_ONLY_TO_DEST;
    TEST_ASSERT_FALSE(replyPolicyAllows(moduleConfig.neighbor_info.policy_flags, POLLER, 0));
    TEST_ASSERT_TRUE(replyPolicyAllows(moduleConfig.neighbor_info.policy_flags, LOCAL_NODE, 0));
}

void test_policy_positionFollowsItsOwnDest()
{
    clearFlags();
    ensureNode(POLLER);
    ensureNode(OTHER);
    config.position.position_dest = POLLER;
    config.position.policy_flags = meshtastic_PortPolicyFlags_REPLY_ONLY_TO_DEST;
    TEST_ASSERT_TRUE(replyPolicyAllows(config.position.policy_flags, POLLER, config.position.position_dest));
    TEST_ASSERT_FALSE(replyPolicyAllows(config.position.policy_flags, OTHER, config.position.position_dest));
}

// --- directedSendChannel: PKC_ALWAYS makes the destination's channel set position precision -------

void test_position_directedChannelOnlyUnderAlwaysPkc()
{
    clearFlags();
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->channel = 2;
    // No policy: the first sharing channel the caller found stays in force.
    TEST_ASSERT_EQUAL_UINT8(1, PositionModule::directedSendChannel(DEST, 1));
    config.position.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
    TEST_ASSERT_EQUAL_UINT8(2, PositionModule::directedSendChannel(DEST, 1));
    // Broadcast and an unknown destination keep the fallback even under the policy.
    TEST_ASSERT_EQUAL_UINT8(1, PositionModule::directedSendChannel(NODENUM_BROADCAST, 1));
    TEST_ASSERT_EQUAL_UINT8(1, PositionModule::directedSendChannel(0x5a5a5a5a, 1));
}

// --- wouldEncryptWithPKC: the policy_flags crypto table ------------------------------------
//
// The encoder has no channel-PSK fallback of its own: a PKC candidate without a destination key is
// refused outright (perhapsEncode, PKI_SEND_FAIL_PUBLIC_KEY). The fallback lives in this function,
// scoped to the ports that carry a policy_flags, so the table below is the whole policy. Rows: flags x
// key-known. A "true" here with no key means the send will fail - that is what PKC_ALWAYS buys.

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

// A plain unicast the client composed is not ours to downgrade: no key still means the upstream
// PKI refusal, as it does for a text DM. The fallback is for our own routine traffic only.
void test_pkc_unsetKeepsTheRefusalForAClientUnicast()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// A reply to whoever polled us carries their request_id, and goes channel-PSK rather than nowhere.
void test_pkc_fallbackForReply()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    p.decoded.request_id = 0x1234;
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// So does a routine send to the destination the user configured for that port. Telemetry's sub-types
// share the port, so any of the five destinations counts.
void test_pkc_fallbackForRoutineDest()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    moduleConfig.telemetry.device_dest = DEST;
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
    moduleConfig.telemetry.device_dest = 0;
    moduleConfig.telemetry.power_dest = DEST;
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
    moduleConfig.telemetry.power_dest = OTHER; // a destination, but not this packet's
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// Position keeps its own destination field, read from config rather than moduleConfig.
void test_pkc_positionDestFallsBack()
{
    clearFlags();
    armPki();
    config.position.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS; // else POSITION is never a PKC candidate
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_POSITION_APP);
    config.position.position_dest = DEST;
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false)); // PKC_ALWAYS forbids the downgrade
    config.position.policy_flags = 0;
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

void test_pkc_alwaysPkcRefusesToDowngrade()
{
    clearFlags();
    armPki();
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false)); // will fail at encode, by design
}

void test_pkc_neverPkcUsesPskEvenWithKey()
{
    clearFlags();
    armPki();
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_PKC_NEVER;
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

void test_pkc_alwaysBeatsNever()
{
    clearFlags();
    armPki();
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS | meshtastic_PortPolicyFlags_PKC_NEVER;
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// Paxcounter has its own policy now; telemetry's must not leak onto it, nor the reverse.
void test_pkc_paxcounterHasOwnPolicy()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_PAXCOUNTER_APP);
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_PKC_NEVER;
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    moduleConfig.paxcounter.policy_flags = meshtastic_PortPolicyFlags_PKC_NEVER;
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
}

// Upstream never PKCs POSITION_APP (Router's port exclusion), so with no policy a directed position goes
// channel-PSK even when the key is held. PKC_ALWAYS on the position port overrides that exclusion.
void test_pkc_positionIsPskUnlessAlwaysPkc()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_POSITION_APP);
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    config.position.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false)); // fails at encode, by design
}

// A client that set pki_encrypted itself keeps the upstream refusal: never downgrade an explicit ask.
void test_pkc_clientExplicitPkiIsNotDowngraded()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_TELEMETRY_APP, /*clientAskedPki=*/true);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
}

// Paxcounter with no policy of its own set behaves like any other port.
void test_pkc_paxcounterFollowsFlags()
{
    clearFlags();
    armPki();
    meshtastic_MeshPacket p = unicastOnPort(meshtastic_PortNum_PAXCOUNTER_APP);
    TEST_ASSERT_TRUE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/true));
    moduleConfig.paxcounter.paxcounter_dest = DEST; // its own routine destination, so the fallback applies
    TEST_ASSERT_FALSE(wouldEncryptWithPKC(&p, 0, /*haveDestKey=*/false));
    moduleConfig.paxcounter.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
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

// --- perhapsEncode: the policy applied to a real send ------------------------------------------
//
// wouldEncryptWithPKC decides; pkcRequiredByPolicy is the other half, refusing a PKC_ALWAYS packet
// that cannot go PKI rather than letting it out under the channel key. Neither may touch a packet we
// are relaying: a relay re-encodes the decoded copy through the same call, and refusing there NAKs
// the original sender over the air.

meshtastic_MeshPacket decodedOnPort(meshtastic_PortNum port, NodeNum from, NodeNum to)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = to;
    p.id = 0x4242;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = port;
    p.decoded.payload.size = 4;
    memset(p.decoded.payload.bytes, 0xA5, p.decoded.payload.size);
    return p;
}

void test_encode_relayCopyIsNotPolicyGated()
{
    channels.initDefaults();
    channels.onConfigChanged();
    armPki();
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
    meshtastic_MeshPacket p = decodedOnPort(meshtastic_PortNum_TELEMETRY_APP, OTHER, DEST);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, perhapsEncode(&p));
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, p.which_payload_variant);
}

void test_encode_pkcAlwaysWithoutLocalKeyIsPkiFailed()
{
    channels.initDefaults();
    channels.onConfigChanged();
    armPki();
    config.security.private_key.size = 0; // no PKI of our own, so the policy cannot be honoured
    moduleConfig.telemetry.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
    meshtastic_MeshPacket p = decodedOnPort(meshtastic_PortNum_TELEMETRY_APP, LOCAL_NODE, DEST);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_PKI_FAILED, perhapsEncode(&p));
}

// --- pkcOnlyDestsHaveKeys: the admin gate ------------------------------------------------------
//
// PKC_ALWAYS with a destination we hold no key for is a node that goes silent on every interval. The
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
    TEST_ASSERT_TRUE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t));
}

void test_gate_alwaysPkcAcceptsKeyedDest()
{
    giveKey(DEST);
    meshtastic_ModuleConfig_TelemetryConfig t = meshtastic_ModuleConfig_TelemetryConfig_init_zero;
    t.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
    t.device_dest = DEST;
    t.environment_dest = 0; // unset destinations are not checked
    TEST_ASSERT_TRUE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t));
}

void test_gate_alwaysPkcRefusesKeylessDest()
{
    meshtastic_NodeInfoLite *n = ensureNode(OTHER);
    n->public_key.size = 0;
    meshtastic_ModuleConfig_TelemetryConfig t = meshtastic_ModuleConfig_TelemetryConfig_init_zero;
    t.policy_flags = meshtastic_PortPolicyFlags_PKC_ALWAYS;
    t.health_dest = OTHER; // in the DB, no key
    TEST_ASSERT_FALSE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t));
    t.health_dest = 0;
    t.power_dest = 0x5a5a5a5a; // not in the DB
    TEST_ASSERT_FALSE(BaseTelemetryModule::pkcOnlyDestsHaveKeys(t));
}

// Paxcounter and position carry their own policy and destination, checked with the generic gate.
void test_gate_alwaysPkcCoversPaxcounterAndPosition()
{
    const uint32_t unknown = 0x5a5a5a5a;
    TEST_ASSERT_FALSE(pkcAlwaysDestsHaveKeys(meshtastic_PortPolicyFlags_PKC_ALWAYS, &unknown, 1));
    TEST_ASSERT_TRUE(pkcAlwaysDestsHaveKeys(0, &unknown, 1));
    giveKey(DEST);
    const uint32_t keyed = DEST;
    TEST_ASSERT_TRUE(pkcAlwaysDestsHaveKeys(meshtastic_PortPolicyFlags_PKC_ALWAYS, &keyed, 1));
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
    RUN_TEST(test_policy_perPortFlagsAreIndependent);
    RUN_TEST(test_policy_onlyToDestWithoutDestRefusesMesh);
    RUN_TEST(test_policy_positionFollowsItsOwnDest);
    RUN_TEST(test_hop_unknownDistanceUsesConfigured);
    RUN_TEST(test_hop_directNeighbourKeepsReturnMargin);
    RUN_TEST(test_hop_boundaryAtConfiguredLimit);
    RUN_TEST(test_hop_neverExceedsConfigured);
    RUN_TEST(test_hop_mqttDistanceIsNotTrusted);
    RUN_TEST(test_position_directedChannelOnlyUnderAlwaysPkc);
    RUN_TEST(test_pkc_unsetUsesKeyWhenKnown);
    RUN_TEST(test_pkc_unsetKeepsTheRefusalForAClientUnicast);
    RUN_TEST(test_pkc_fallbackForReply);
    RUN_TEST(test_pkc_fallbackForRoutineDest);
    RUN_TEST(test_pkc_positionDestFallsBack);
    RUN_TEST(test_pkc_alwaysPkcRefusesToDowngrade);
    RUN_TEST(test_pkc_neverPkcUsesPskEvenWithKey);
    RUN_TEST(test_pkc_alwaysBeatsNever);
    RUN_TEST(test_pkc_paxcounterHasOwnPolicy);
    RUN_TEST(test_pkc_positionIsPskUnlessAlwaysPkc);
    RUN_TEST(test_pkc_clientExplicitPkiIsNotDowngraded);
    RUN_TEST(test_pkc_paxcounterFollowsFlags);
    RUN_TEST(test_pkc_fallbackScopedToTelemetryPorts);
    RUN_TEST(test_encode_relayCopyIsNotPolicyGated);
    RUN_TEST(test_encode_pkcAlwaysWithoutLocalKeyIsPkiFailed);
    RUN_TEST(test_gate_noAlwaysPkcAcceptsAnything);
    RUN_TEST(test_gate_alwaysPkcAcceptsKeyedDest);
    RUN_TEST(test_gate_alwaysPkcRefusesKeylessDest);
    RUN_TEST(test_gate_alwaysPkcCoversPaxcounterAndPosition);
    RUN_TEST(test_budget_trimsUnicastAtDefault);
    RUN_TEST(test_budget_leavesAlreadySizedPacketAlone);
    RUN_TEST(test_budget_ignoresBroadcastAndOtherPorts);
    RUN_TEST(test_budget_coversPhoneOriginatedSends);
    exit(UNITY_END());
}

void loop() {}
