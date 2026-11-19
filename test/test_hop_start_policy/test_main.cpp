#include "MeshTypes.h" // Include BEFORE TestUtil.h (provides NodeNum, isFromUs)
#include "TestUtil.h"
#include <unity.h>

#include "configuration.h" // MESHTASTIC_PREHOP_DROP
#include "mesh/NodeDB.h"   // classifyHopStart, shouldDropPacketForPreHop, HopStartStatus
#include <cstdio>
#include <cstring>

// TEST_MESSAGE emits file:line:INFO lines visible at -vv; printf lines appear un-prefixed.
// TEST_MSG_FMT wraps TEST_MESSAGE for formatted per-case diagnostics.
#define MSG_BUF_LEN 200
#define TEST_MSG_FMT(fmt, ...)                                                                                                   \
    do {                                                                                                                         \
        char _buf[MSG_BUF_LEN];                                                                                                  \
        snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__);                                                                          \
        TEST_MESSAGE(_buf);                                                                                                      \
    } while (0)

static constexpr NodeNum kLocalNode = 0x11111111;
static constexpr NodeNum kRemoteNode = 0x22222222;

// shouldDropPacketForPreHop -> isFromUs -> nodeDB->getNodeNum(), so a real NodeDB must be live.
static NodeDB *testNodeDB = nullptr;

// ---------------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------------

// A still-encrypted packet as Router::perhapsHandleReceived sees it (Router.cpp:1598): the
// channel-encrypted bitfield is unreadable, so the union's decoded half is untouched garbage.
static meshtastic_MeshPacket makeEncrypted(NodeNum from, uint8_t hopStart, uint8_t hopLimit)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = NODENUM_BROADCAST;
    p.id = 0x1000u + (uint32_t)hopStart * 16u + hopLimit;
    p.hop_start = hopStart;
    p.hop_limit = hopLimit;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = 16; // opaque ciphertext; contents irrelevant to hop classification
    return p;
}

// A decoded packet as Router::handleReceived sees it post-decrypt (Router.cpp:1450).
static meshtastic_MeshPacket makeDecoded(NodeNum from, uint8_t hopStart, uint8_t hopLimit, bool hasBitfield)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = NODENUM_BROADCAST;
    p.id = 0x2000u + (uint32_t)hopStart * 16u + hopLimit;
    p.hop_start = hopStart;
    p.hop_limit = hopLimit;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.has_bitfield = hasBitfield;
    p.decoded.bitfield = hasBitfield ? 1 : 0;
    return p;
}

static void assertClassify(const meshtastic_MeshPacket &p, HopStartStatus expected, const char *label)
{
    HopStartStatus got = classifyHopStart(p);
    TEST_MSG_FMT("%-44s hop_start=%u hop_limit=%u -> %d (expect %d)", label, (unsigned)p.hop_start, (unsigned)p.hop_limit,
                 (int)got, (int)expected);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected, (int)got, label);
}

// The shared predicate Router::dispatchReceived uses to set skipHandle, so gate drift fails here.
// (The cancelSending side effect stays uncovered.)
static bool routerPostDecodeWouldSkip(const meshtastic_MeshPacket &p)
{
    return shouldSkipHandleForPostDecodeHop(p);
}

// ---------------------------------------------------------------------------
// classifyHopStart truth table
// ---------------------------------------------------------------------------

void test_classify_invalid_when_hop_start_below_hop_limit()
{
    TEST_MESSAGE("=== hop_start < hop_limit is provably corrupt on any payload variant ===");

    assertClassify(makeEncrypted(kRemoteNode, 2, 5), HopStartStatus::INVALID, "encrypted 2/5");
    assertClassify(makeEncrypted(kRemoteNode, 0, 1), HopStartStatus::INVALID, "encrypted 0/1");
    assertClassify(makeEncrypted(kRemoteNode, 0, 3), HopStartStatus::INVALID, "encrypted 0/3 (not UNKNOWN: limit > 0)");
    // The bitfield cannot rescue an inconsistent pair - the guard runs before the zero-hop probe.
    assertClassify(makeDecoded(kRemoteNode, 2, 5, true), HopStartStatus::INVALID, "decoded+bitfield 2/5");
    assertClassify(makeDecoded(kRemoteNode, 0, 3, true), HopStartStatus::INVALID, "decoded+bitfield 0/3");
    assertClassify(makeDecoded(kRemoteNode, 0, 3, false), HopStartStatus::INVALID, "decoded no-bitfield 0/3");
}

void test_classify_valid_when_hop_start_covers_hop_limit()
{
    TEST_MESSAGE("=== hop_start > 0 and >= hop_limit is VALID regardless of variant or bitfield ===");

    assertClassify(makeEncrypted(kRemoteNode, 3, 3), HopStartStatus::VALID, "encrypted 3/3 (fresh broadcast)");
    assertClassify(makeEncrypted(kRemoteNode, 3, 0), HopStartStatus::VALID, "encrypted 3/0 (fully relayed)");
    assertClassify(makeEncrypted(kRemoteNode, 5, 2), HopStartStatus::VALID, "encrypted 5/2 (mid-relay)");
    assertClassify(makeDecoded(kRemoteNode, 3, 3, false), HopStartStatus::VALID, "decoded no-bitfield 3/3");
    assertClassify(makeDecoded(kRemoteNode, 1, 0, false), HopStartStatus::VALID, "decoded no-bitfield 1/0");
}

void test_classify_zero_hop_modern_beacon_valid()
{
    TEST_MESSAGE("=== 0/0 decoded with bitfield = modern zero-hop broadcast, VALID ===");

    assertClassify(makeDecoded(kRemoteNode, 0, 0, true), HopStartStatus::VALID, "decoded+bitfield 0/0 (beacon)");
}

void test_classify_zero_hop_decoded_without_bitfield_unknown()
{
    TEST_MESSAGE("=== 0/0 decoded without bitfield = pre-2.3.0 origin, MISSING_OR_UNKNOWN ===");

    assertClassify(makeDecoded(kRemoteNode, 0, 0, false), HopStartStatus::MISSING_OR_UNKNOWN, "decoded no-bitfield 0/0");
}

void test_classify_zero_hop_encrypted_is_unknown()
{
    TEST_MESSAGE("=== 0/0 encrypted: bitfield unreadable pre-decode, MISSING_OR_UNKNOWN ===");

    assertClassify(makeEncrypted(kRemoteNode, 0, 0), HopStartStatus::MISSING_OR_UNKNOWN, "encrypted 0/0");
}

void test_classify_encrypted_variant_ignores_stale_union_bitfield()
{
    TEST_MESSAGE("=== stale decoded-union bytes must not leak through the variant check ===");

    // Adversarial struct state: payload variant says encrypted, but the union's decoded half
    // still claims has_bitfield (e.g. a reused pool packet). The variant tag must gate the read.
    meshtastic_MeshPacket p = makeEncrypted(kRemoteNode, 0, 0);
    p.decoded.has_bitfield = true;
    p.decoded.bitfield = 1;
    assertClassify(p, HopStartStatus::MISSING_OR_UNKNOWN, "encrypted 0/0 w/ stale union bitfield");
}

void test_classify_hop_cap_boundaries()
{
    TEST_MESSAGE("=== boundaries at the 3-bit wire cap (HOP_MAX=7) and uint8 extremes ===");

    assertClassify(makeEncrypted(kRemoteNode, 7, 7), HopStartStatus::VALID, "encrypted 7/7 (max fresh)");
    assertClassify(makeEncrypted(kRemoteNode, 7, 0), HopStartStatus::VALID, "encrypted 7/0 (max relayed out)");
    assertClassify(makeEncrypted(kRemoteNode, 6, 7), HopStartStatus::INVALID, "encrypted 6/7 (one below limit)");
    // Above the wire cap: unreachable from radio (3-bit fields) but reachable via phone input,
    // where hop fields are plain uint8 in the struct.
    assertClassify(makeEncrypted(kRemoteNode, 7, 8), HopStartStatus::INVALID, "encrypted 7/8 (limit past cap)");
    assertClassify(makeEncrypted(kRemoteNode, 255, 255), HopStartStatus::VALID, "encrypted 255/255");
    assertClassify(makeEncrypted(kRemoteNode, 254, 255), HopStartStatus::INVALID, "encrypted 254/255");
}

// ---------------------------------------------------------------------------
// Pre-decode drop policy (Router.cpp:1598 gate) and post-decode re-check
// ---------------------------------------------------------------------------

#if MESHTASTIC_PREHOP_DROP

void test_predecode_drops_provably_corrupt_only()
{
    TEST_MESSAGE("=== pre-decode gate drops only INVALID; VALID passes ===");

    TEST_ASSERT_TRUE_MESSAGE(shouldDropPacketForPreHop(makeEncrypted(kRemoteNode, 2, 5)), "corrupt 2/5 must drop");
    TEST_ASSERT_TRUE_MESSAGE(shouldDropPacketForPreHop(makeEncrypted(kRemoteNode, 0, 3)), "corrupt 0/3 must drop");
    TEST_ASSERT_FALSE_MESSAGE(shouldDropPacketForPreHop(makeEncrypted(kRemoteNode, 3, 3)), "valid 3/3 must pass");
    TEST_ASSERT_FALSE_MESSAGE(shouldDropPacketForPreHop(makeEncrypted(kRemoteNode, 5, 2)), "valid 5/2 must pass");
}

void test_predecode_keeps_unknown_encrypted()
{
    TEST_MESSAGE("=== REGRESSION (#10758): MISSING_OR_UNKNOWN must survive the pre-decode gate ===");
    TEST_MESSAGE("Pre-fix, every non-VALID verdict dropped here - silently discarding all encrypted");
    TEST_MESSAGE("traffic whose proving bitfield was still under the channel key.");

    meshtastic_MeshPacket p = makeEncrypted(kRemoteNode, 0, 0);
    TEST_ASSERT_EQUAL_INT((int)HopStartStatus::MISSING_OR_UNKNOWN, (int)classifyHopStart(p));
    TEST_ASSERT_FALSE_MESSAGE(shouldDropPacketForPreHop(p), "unknown-yet packet dropped before decryption");
}

void test_predecode_from_us_exempt()
{
    TEST_MESSAGE("=== local-origin packets are never pre-hop dropped, even when corrupt ===");

    TEST_ASSERT_FALSE_MESSAGE(shouldDropPacketForPreHop(makeEncrypted(kLocalNode, 2, 5)), "own node num exempt");
    // from == 0 also counts as us (isFromUs), e.g. phone-injected packets pre-numbering.
    TEST_ASSERT_FALSE_MESSAGE(shouldDropPacketForPreHop(makeEncrypted(0, 2, 5)), "from==0 exempt");
}

void test_postdecode_recheck_catches_unknown()
{
    TEST_MESSAGE("=== the pre/post-decode asymmetry: UNKNOWN passes the gate, then skipHandle ===");

    // Pre-decode the packet is opaque 0/0 -> kept; post-decode the absent bitfield proves a
    // pre-hop-firmware origin -> Router.cpp:1450 sets skipHandle. This split IS the fix; a
    // cleanup that collapses the two checks into one re-creates the mesh-wide drop.
    meshtastic_MeshPacket preHopOrigin = makeDecoded(kRemoteNode, 0, 0, false);
    TEST_ASSERT_FALSE(shouldDropPacketForPreHop(makeEncrypted(kRemoteNode, 0, 0)));
    TEST_ASSERT_TRUE_MESSAGE(routerPostDecodeWouldSkip(preHopOrigin), "post-decode must exclude pre-hop origin");

    meshtastic_MeshPacket modernBeacon = makeDecoded(kRemoteNode, 0, 0, true);
    TEST_ASSERT_FALSE_MESSAGE(routerPostDecodeWouldSkip(modernBeacon), "modern zero-hop beacon must be handled");

    meshtastic_MeshPacket ourOwn = makeDecoded(kLocalNode, 0, 0, false);
    TEST_ASSERT_FALSE_MESSAGE(routerPostDecodeWouldSkip(ourOwn), "local-origin exempt post-decode too");

    meshtastic_MeshPacket corrupt = makeDecoded(kRemoteNode, 2, 5, true);
    TEST_ASSERT_TRUE_MESSAGE(routerPostDecodeWouldSkip(corrupt), "corrupt still excluded post-decode");
}

#else // !MESHTASTIC_PREHOP_DROP

void test_prehop_disabled_never_drops()
{
    TEST_MESSAGE("=== MESHTASTIC_PREHOP_DROP=0: the gate is compiled out entirely ===");

    TEST_ASSERT_FALSE(shouldDropPacketForPreHop(makeEncrypted(kRemoteNode, 2, 5)));
    TEST_ASSERT_FALSE(shouldDropPacketForPreHop(makeEncrypted(kRemoteNode, 0, 0)));
}

#endif // MESHTASTIC_PREHOP_DROP

// ---------------------------------------------------------------------------
// Cross-check against getHopsAway
// ---------------------------------------------------------------------------

void test_gethopsaway_agrees_with_classification()
{
    TEST_MESSAGE("=== getHopsAway yields a hop count iff classifyHopStart says VALID ===");

    struct Case {
        meshtastic_MeshPacket p;
        const char *label;
    };
    const Case cases[] = {
        {makeEncrypted(kRemoteNode, 2, 5), "encrypted 2/5"},
        {makeEncrypted(kRemoteNode, 0, 3), "encrypted 0/3"},
        {makeEncrypted(kRemoteNode, 0, 0), "encrypted 0/0"},
        {makeEncrypted(kRemoteNode, 3, 3), "encrypted 3/3"},
        {makeEncrypted(kRemoteNode, 5, 2), "encrypted 5/2"},
        {makeEncrypted(kRemoteNode, 7, 0), "encrypted 7/0"},
        {makeDecoded(kRemoteNode, 0, 0, true), "decoded+bitfield 0/0"},
        {makeDecoded(kRemoteNode, 0, 0, false), "decoded no-bitfield 0/0"},
        {makeDecoded(kRemoteNode, 0, 3, true), "decoded+bitfield 0/3"},
        {makeDecoded(kRemoteNode, 4, 1, false), "decoded no-bitfield 4/1"},
    };

    for (const Case &c : cases) {
        const bool valid = classifyHopStart(c.p) == HopStartStatus::VALID;
        const int8_t hops = getHopsAway(c.p, -1);
        TEST_MSG_FMT("%-28s valid=%d hopsAway=%d", c.label, (int)valid, (int)hops);
        if (valid) {
            TEST_ASSERT_EQUAL_INT8_MESSAGE((int8_t)(c.p.hop_start - c.p.hop_limit), hops, c.label);
        } else {
            TEST_ASSERT_EQUAL_INT8_MESSAGE(-1, hops, c.label);
        }
    }
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

// Printed row and checked expectation come from one struct, so the summary cannot narrate a table
// the predicates no longer implement. Was TEST_MESSAGE-only, i.e. a case that could not fail.
void test_truth_table_summary()
{
#if MESHTASTIC_PREHOP_DROP
    constexpr bool kGate = true;
#else
    constexpr bool kGate = false;
#endif

    struct Row {
        meshtastic_MeshPacket p;
        HopStartStatus expected;
        bool preDrop;  // shouldDropPacketForPreHop, gate compiled in
        bool postSkip; // shouldSkipHandleForPostDecodeHop, ditto
        const char *label;
    };
    const Row rows[] = {
        {makeDecoded(kRemoteNode, 2, 5, true), HopStartStatus::INVALID, true, true,
         "hop_start<hop_limit | any variant           | INVALID  | drop pre-decode + post-decode"},
        {makeDecoded(kRemoteNode, 3, 3, false), HopStartStatus::VALID, false, false,
         "start>0, >=limit    | any variant           | VALID    | handled normally"},
        {makeDecoded(kRemoteNode, 0, 0, true), HopStartStatus::VALID, false, false,
         "0/0                 | decoded + bitfield    | VALID    | modern zero-hop beacon"},
        {makeDecoded(kRemoteNode, 0, 0, false), HopStartStatus::MISSING_OR_UNKNOWN, false, true,
         "0/0                 | decoded, no bitfield  | UNKNOWN  | kept pre-decode, skipHandle post-decode"},
        {makeEncrypted(kRemoteNode, 0, 0), HopStartStatus::MISSING_OR_UNKNOWN, false, true,
         "0/0                 | encrypted             | UNKNOWN  | kept pre-decode (bitfield unreadable)"},
        {makeDecoded(kLocalNode, 2, 5, true), HopStartStatus::INVALID, false, false,
         "isFromUs            | any                   | any      | never dropped by pre-hop policy"},
    };

    TEST_MESSAGE("=== classifyHopStart truth table ===");
    for (const Row &r : rows) {
        TEST_MESSAGE(r.label);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)r.expected, (int)classifyHopStart(r.p), r.label);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)(kGate && r.preDrop), (int)shouldDropPacketForPreHop(r.p), r.label);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)(kGate && r.postSkip), (int)routerPostDecodeWouldSkip(r.p), r.label);
    }
}

// ---------------------------------------------------------------------------
// Unity lifecycle
// ---------------------------------------------------------------------------

void setUp(void)
{
    if (!testNodeDB)
        testNodeDB = new NodeDB();

    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    myNodeInfo.my_node_num = kLocalNode;
    nodeDB = testNodeDB;
}

void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();

    printf("\n=== classifyHopStart truth table ===\n");
    RUN_TEST(test_classify_invalid_when_hop_start_below_hop_limit);
    RUN_TEST(test_classify_valid_when_hop_start_covers_hop_limit);
    RUN_TEST(test_classify_zero_hop_modern_beacon_valid);
    RUN_TEST(test_classify_zero_hop_decoded_without_bitfield_unknown);
    RUN_TEST(test_classify_zero_hop_encrypted_is_unknown);
    RUN_TEST(test_classify_encrypted_variant_ignores_stale_union_bitfield);
    RUN_TEST(test_classify_hop_cap_boundaries);

    printf("\n=== Pre-hop drop policy ===\n");
#if MESHTASTIC_PREHOP_DROP
    RUN_TEST(test_predecode_drops_provably_corrupt_only);
    RUN_TEST(test_predecode_keeps_unknown_encrypted);
    RUN_TEST(test_predecode_from_us_exempt);
    RUN_TEST(test_postdecode_recheck_catches_unknown);
#else
    RUN_TEST(test_prehop_disabled_never_drops);
#endif

    printf("\n=== Cross-checks ===\n");
    RUN_TEST(test_gethopsaway_agrees_with_classification);

    printf("\n=== Summary ===\n");
    RUN_TEST(test_truth_table_summary);

    exit(UNITY_END());
}

void loop() {}
