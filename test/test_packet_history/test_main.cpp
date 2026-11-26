/*
 * Unit tests for PacketHistory - the packet deduplication engine
 * used by the mesh routing stack.
 *
 * PacketHistory maintains a fixed-size array of PacketRecords with an
 * optional hash table for O(1) lookup. It tracks which nodes relayed
 * each packet, supports LRU-style eviction, and detects fallback-to-
 * flooding and hop-limit upgrades.
 */

#include "PacketHistory.h"

#include "SerialConsole.h"
#include "TestUtil.h"
#include <string>
#include <unity.h>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr uint32_t OUR_NODE_NUM = 0xDEAD1234;
static constexpr uint8_t OUR_RELAY_ID = 0x34; // getLastByteOfNodeNum(OUR_NODE_NUM)
static constexpr uint32_t SMALL_CAPACITY = 8;

// ---------------------------------------------------------------------------
// Per-test state
// ---------------------------------------------------------------------------
static PacketHistory *ph = nullptr;

class RecordingPrint : public Print
{
  public:
    size_t write(uint8_t value) override
    {
        output.push_back(value);
        return 1;
    }

    std::vector<uint8_t> output;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static meshtastic_MeshPacket makePacket(uint32_t from, uint32_t id, uint8_t hop_limit = 3,
                                        uint8_t next_hop = NO_NEXT_HOP_PREFERENCE, uint8_t relay_node = 0)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.id = id;
    p.hop_limit = hop_limit;
    p.next_hop = next_hop;
    p.relay_node = relay_node;
    return p;
}

// ---------------------------------------------------------------------------
// setUp / tearDown - called before and after every test
// ---------------------------------------------------------------------------
void setUp(void)
{
    myNodeInfo.my_node_num = OUR_NODE_NUM;
    ph = new PacketHistory(SMALL_CAPACITY);
}

void tearDown(void)
{
    delete ph;
    ph = nullptr;
}

// ===========================================================================
// Group 1 - Initialization
// ===========================================================================

void test_init_default_size_does_not_warn(void)
{
    RecordingPrint sink;
    console->setDestination(&sink);

    PacketHistory h;

    console->setDestination(&Serial);
    const std::string output(sink.output.begin(), sink.output.end());
    TEST_ASSERT_TRUE(h.initOk());
    TEST_ASSERT_NULL(strstr(output.c_str(), "Packet History - Invalid size"));
}

void test_init_minimum_size(void)
{
    RecordingPrint sink;
    console->setDestination(&sink);

    PacketHistory h(4);

    console->setDestination(&Serial);
    const std::string output(sink.output.begin(), sink.output.end());
    TEST_ASSERT_TRUE(h.initOk());
    TEST_ASSERT_NULL(strstr(output.c_str(), "Packet History - Invalid size")); // 4 is the smallest accepted size
}

void test_init_too_small_falls_back(void)
{
    // Sizes < 4 or > PACKETHISTORY_MAX are clamped to PACKETHISTORY_MAX inside the constructor
    RecordingPrint sink;
    console->setDestination(&sink);

    PacketHistory h(2);

    console->setDestination(&Serial);
    const std::string output(sink.output.begin(), sink.output.end());
    TEST_ASSERT_NOT_NULL(strstr(output.c_str(), "Packet History - Invalid size"));

    // A 2-slot history would already have evicted by the third insert.
    for (uint32_t i = 1; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        h.wasSeenRecently(&p);
    }
    for (uint32_t i = 1; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_TRUE(h.wasSeenRecently(&p, false));
    }
}

// ===========================================================================
// Group 2 - Basic Deduplication
// ===========================================================================

void test_first_packet_not_seen(void)
{
    auto p = makePacket(0x1111, 100);
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p));
}

void test_same_packet_seen_twice(void)
{
    auto p = makePacket(0x1111, 100);
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p)); // first time
    TEST_ASSERT_TRUE(ph->wasSeenRecently(&p));  // duplicate
}

void test_different_id_not_confused(void)
{
    auto p1 = makePacket(0x1111, 100);
    auto p2 = makePacket(0x1111, 200);
    ph->wasSeenRecently(&p1);
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p2));
}

void test_different_sender_not_confused(void)
{
    auto p1 = makePacket(0x1111, 100);
    auto p2 = makePacket(0x2222, 100);
    ph->wasSeenRecently(&p1);
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p2));
}

void test_withUpdate_false_no_insert(void)
{
    auto p = makePacket(0x1111, 100);
    // First call with withUpdate=false: should not store
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p, /*withUpdate=*/false));
    // Second call with withUpdate=true: still not found because first didn't store
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p, /*withUpdate=*/true));
}

void test_withUpdate_true_inserts(void)
{
    auto p = makePacket(0x1111, 100);
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p, /*withUpdate=*/true));
    TEST_ASSERT_TRUE(ph->wasSeenRecently(&p, /*withUpdate=*/false)); // found without inserting again
}

// ===========================================================================
// Group 3 - LRU Eviction
// ===========================================================================

void test_fill_capacity_all_found(void)
{
    for (uint32_t i = 1; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        ph->wasSeenRecently(&p);
    }
    // All 8 should be found
    for (uint32_t i = 1; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_TRUE(ph->wasSeenRecently(&p, false));
    }
}

void test_eviction_oldest_replaced(void)
{
    // Fill all 8 slots, 1 ms apart so every record has a distinct age and id 1 is the oldest.
    // insert() picks the victim with (now_millis - rxTimeMsec) > OldtrxTimeMsec, strict >.
    for (uint32_t i = 1; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        ph->wasSeenRecently(&p);
        delay(1);
    }

    // Insert a 9th packet - must evict the oldest, and only it
    auto p9 = makePacket(0xAAAA, 9);
    ph->wasSeenRecently(&p9);

    TEST_ASSERT_TRUE(ph->wasSeenRecently(&p9, false));
    auto p1 = makePacket(0xAAAA, 1);
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p1, false));
    for (uint32_t i = 2; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_TRUE(ph->wasSeenRecently(&p, false));
    }
}

void test_matching_slot_reused(void)
{
    // Fill every slot, 1 ms apart, with p1 inserted last so it is the newest record. Re-observing
    // p1 must update its own slot; without the matching-slot branch it would overwrite the oldest
    // slot (id 2) instead, dropping a packet that should still be found.
    for (uint32_t i = 2; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        ph->wasSeenRecently(&p);
        delay(1);
    }
    auto p1 = makePacket(0xBBBB, 1);
    ph->wasSeenRecently(&p1); // 8th record: every slot is now in use
    delay(1);

    ph->wasSeenRecently(&p1); // re-observe (merge path)

    TEST_ASSERT_TRUE(ph->wasSeenRecently(&p1, false));
    for (uint32_t i = 2; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_TRUE(ph->wasSeenRecently(&p, false));
    }
}

void test_evict_all_old_packets(void)
{
    // Fill with packets 1..8
    for (uint32_t i = 1; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        ph->wasSeenRecently(&p);
    }

    // Advance time so the replacement batch can evict the originals
    delay(1);

    // Replace all with packets 101..108
    for (uint32_t i = 101; i <= 100 + SMALL_CAPACITY; i++) {
        auto p = makePacket(0xBBBB, i);
        ph->wasSeenRecently(&p);
    }
    // None of the originals should be found
    for (uint32_t i = 1; i <= SMALL_CAPACITY; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_FALSE(ph->wasSeenRecently(&p, false));
    }
    // All new ones should be found
    for (uint32_t i = 101; i <= 100 + SMALL_CAPACITY; i++) {
        auto p = makePacket(0xBBBB, i);
        TEST_ASSERT_TRUE(ph->wasSeenRecently(&p, false));
    }
}

// ===========================================================================
// Group 4 - Relayer Tracking
// ===========================================================================

void test_wasRelayer_true(void)
{
    // Non-us relay_nodes only enter relayed_by[] through the "heard-back" merge path:
    // we must have relayed first, then observe the packet return at hop_limit-1.
    auto p1 = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);

    // Heard-back from 0xCC at hop_limit=2 (ourTxHopLimit-1) triggers the merge
    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, 0xCC);
    ph->wasSeenRecently(&p2);

    TEST_ASSERT_TRUE(ph->wasRelayer(0xCC, 100, 0x1111));
}

void test_wasRelayer_false(void)
{
    auto p = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, 0xAA);
    ph->wasSeenRecently(&p);
    // 0xCC was never a relayer
    TEST_ASSERT_FALSE(ph->wasRelayer(0xCC, 100, 0x1111));
}

void test_wasRelayer_zero_returns_false(void)
{
    auto p = makePacket(0x1111, 100);
    ph->wasSeenRecently(&p);
    TEST_ASSERT_FALSE(ph->wasRelayer(0, 100, 0x1111));
}

void test_wasRelayer_not_found(void)
{
    // Packet not in history at all
    TEST_ASSERT_FALSE(ph->wasRelayer(0xAA, 999, 0x9999));
}

void test_wasRelayer_wasSole_true(void)
{
    // relay_node = ourRelayID → relayed_by[0] = ourRelayID
    auto p = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p);

    bool wasSole = false;
    bool result = ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111, &wasSole);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(wasSole);
}

void test_wasRelayer_wasSole_false(void)
{
    // First observation: we relay
    auto p1 = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);

    // Second observation: different relayer adds to record
    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, 0xBB);
    ph->wasSeenRecently(&p2);

    bool wasSole = true;
    bool result = ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111, &wasSole);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(wasSole);
}

void test_wasRelayer_all_six_slots(void)
{
    // First observation: we relay with hop_limit=3 (fills slot 0, ourTxHopLimit=3)
    auto p = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p);

    // Each heard-back must satisfy: hop_limit == ourTxHopLimit OR ourTxHopLimit-1.
    // Using hop_limit=2 (ourTxHopLimit-1) for all, which triggers the heard-back
    // merge path each time. Each new relay_node pushes to slot 0 and shifts existing
    // relayers right, eventually filling all NUM_RELAYERS(6) slots.
    uint8_t relayers[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    for (int i = 0; i < 5; i++) {
        auto pn = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, relayers[i]);
        ph->wasSeenRecently(&pn);
    }

    // All 6 should be detected
    TEST_ASSERT_TRUE(ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111));
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(ph->wasRelayer(relayers[i], 100, 0x1111));
    }
}

// ===========================================================================
// Group 5 - removeRelayer
// ===========================================================================

void test_removeRelayer_removes(void)
{
    auto p1 = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);
    TEST_ASSERT_TRUE(ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111));

    ph->removeRelayer(OUR_RELAY_ID, 100, 0x1111);
    TEST_ASSERT_FALSE(ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111));
}

void test_removeRelayer_compacts(void)
{
    // We relay first
    auto p1 = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);
    // Second relayer
    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, 0xBB);
    ph->wasSeenRecently(&p2);

    // Remove us, 0xBB should still be found
    ph->removeRelayer(OUR_RELAY_ID, 100, 0x1111);
    TEST_ASSERT_FALSE(ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111));
    TEST_ASSERT_TRUE(ph->wasRelayer(0xBB, 100, 0x1111));
}

void test_removeRelayer_nonexistent_safe(void)
{
    auto p = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p);
    // Removing a relayer that doesn't exist should not crash
    ph->removeRelayer(0xFF, 100, 0x1111);
    // Original should still be there
    TEST_ASSERT_TRUE(ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111));
}

void test_removeRelayer_packet_not_found_safe(void)
{
    // Packet not in history - should not crash
    ph->removeRelayer(0xAA, 999, 0x9999);
}

// ===========================================================================
// Group 6 - checkRelayers
// ===========================================================================

void test_checkRelayers_both_found(void)
{
    // We relay first
    auto p1 = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);
    // Second relayer
    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, 0xBB);
    ph->wasSeenRecently(&p2);

    bool r1 = false, r2 = false;
    ph->checkRelayers(OUR_RELAY_ID, 0xBB, 100, 0x1111, &r1, &r2);
    TEST_ASSERT_TRUE(r1);
    TEST_ASSERT_TRUE(r2);
}

void test_checkRelayers_one_found(void)
{
    auto p = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p);

    bool r1 = false, r2 = false;
    ph->checkRelayers(OUR_RELAY_ID, 0xCC, 100, 0x1111, &r1, &r2);
    TEST_ASSERT_TRUE(r1);
    TEST_ASSERT_FALSE(r2);
}

void test_checkRelayers_r2WasSole(void)
{
    auto p = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p);

    bool r1 = false, r2 = false, r2Sole = false;
    // relayer1=0xCC (not found), relayer2=OUR_RELAY_ID (sole relayer)
    ph->checkRelayers(0xCC, OUR_RELAY_ID, 100, 0x1111, &r1, &r2, &r2Sole);
    TEST_ASSERT_FALSE(r1);
    TEST_ASSERT_TRUE(r2);
    TEST_ASSERT_TRUE(r2Sole);
}

// ===========================================================================
// Group 7 - wasSeenRecently Merge Logic
// ===========================================================================

void test_merge_preserves_original_next_hop(void)
{
    // First observation names us as next_hop; the re-observation names someone else. weWereNextHop is
    // read from the stored record BEFORE the merge, so only a third copy shows what the merge kept.
    auto p1 = makePacket(0x1111, 200, 3, OUR_RELAY_ID, 0xAA);
    ph->wasSeenRecently(&p1);
    auto p2 = makePacket(0x1111, 200, 2, 0x99, 0xBB);
    ph->wasSeenRecently(&p2);

    auto p3 = makePacket(0x1111, 200, 1, 0x77, 0xCC);
    bool weWereNextHop = false;
    ph->wasSeenRecently(&p3, true, nullptr, &weWereNextHop);
    TEST_ASSERT_TRUE(weWereNextHop);
}

void test_merge_preserves_highest_hop_limit(void)
{
    // First observation with hop_limit=5
    auto p1 = makePacket(0x1111, 100, 5);
    ph->wasSeenRecently(&p1);

    // Re-observation with hop_limit=2 (lower)
    auto p2 = makePacket(0x1111, 100, 2);
    ph->wasSeenRecently(&p2);

    // Third observation with hop_limit=3 should not trigger upgrade (highest was 5)
    bool wasUpgraded = true;
    auto p3 = makePacket(0x1111, 100, 3);
    ph->wasSeenRecently(&p3, true, nullptr, nullptr, &wasUpgraded);
    TEST_ASSERT_FALSE(wasUpgraded);
}

void test_merge_heard_back_stores_relay_node(void)
{
    // First: we relay (hop_limit=3)
    auto p1 = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);

    // Second: we hear the packet back with hop_limit=2 (one less), from relay_node=0xCC
    // This triggers the "heard back" logic: weWereRelayer && hop_limit == ourTxHopLimit-1
    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, 0xCC);
    ph->wasSeenRecently(&p2);

    TEST_ASSERT_TRUE(ph->wasRelayer(OUR_RELAY_ID, 100, 0x1111));
    TEST_ASSERT_TRUE(ph->wasRelayer(0xCC, 100, 0x1111));
}

// ===========================================================================
// Group 8 - Fallback-to-Flooding Detection
// ===========================================================================

void test_fallback_detected(void)
{
    // The fallback condition requires wasRelayer(relay_node) && !wasRelayer(ourRelayID).
    // Non-us relayers only enter relayed_by[] via the heard-back merge path, which
    // also stores ourRelayID. So we must removeRelayer(ourRelayID) to satisfy both.
    //
    // Scenario: we relay a directed packet, hear it back from 0xAA, then the router
    // removes us from the relayer list. Later the sender falls back to flooding.

    // Step 1: We relay (directed to next_hop=0x55)
    auto p1 = makePacket(0x1111, 100, 3, 0x55, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);

    // Step 2: Heard-back from 0xAA at hop_limit-1 → stores 0xAA in relayed_by
    auto p2 = makePacket(0x1111, 100, 2, 0x55, 0xAA);
    ph->wasSeenRecently(&p2);

    // Step 3: Router removes us from the relayer list
    ph->removeRelayer(OUR_RELAY_ID, 100, 0x1111);

    // Step 4: Sender falls back to flooding - same packet, NO_NEXT_HOP_PREFERENCE, from 0xAA
    auto p3 = makePacket(0x1111, 100, 1, NO_NEXT_HOP_PREFERENCE, 0xAA);
    bool wasFallback = false;
    ph->wasSeenRecently(&p3, true, &wasFallback);
    TEST_ASSERT_TRUE(wasFallback);
}

void test_fallback_not_when_we_relayed(void)
{
    // First observation: directed, we relayed it
    auto p1 = makePacket(0x1111, 100, 3, 0x55, OUR_RELAY_ID);
    ph->wasSeenRecently(&p1);

    // Second observation: fallback to flooding from same relayer (us)
    // But since we already relayed, wasFallback should be false
    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, OUR_RELAY_ID);
    bool wasFallback = false;
    ph->wasSeenRecently(&p2, true, &wasFallback);
    TEST_ASSERT_FALSE(wasFallback);
}

void test_fallback_not_on_first_observation(void)
{
    // First time seen - can't be a fallback
    auto p = makePacket(0x1111, 100, 3, NO_NEXT_HOP_PREFERENCE, 0xAA);
    bool wasFallback = false;
    ph->wasSeenRecently(&p, true, &wasFallback);
    TEST_ASSERT_FALSE(wasFallback);
}

// ===========================================================================
// Group 9 - Next-Hop and Upgrade Detection
// ===========================================================================

void test_weWereNextHop_true(void)
{
    // Packet directed to us (next_hop = ourRelayID)
    auto p1 = makePacket(0x1111, 100, 3, OUR_RELAY_ID, 0xAA);
    ph->wasSeenRecently(&p1);

    // Re-observe: check if we were the original next_hop
    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, 0xBB);
    bool weWereNextHop = false;
    ph->wasSeenRecently(&p2, true, nullptr, &weWereNextHop);
    TEST_ASSERT_TRUE(weWereNextHop);
}

void test_weWereNextHop_false(void)
{
    // Packet directed to someone else
    auto p1 = makePacket(0x1111, 100, 3, 0x99, 0xAA);
    ph->wasSeenRecently(&p1);

    auto p2 = makePacket(0x1111, 100, 2, NO_NEXT_HOP_PREFERENCE, 0xBB);
    bool weWereNextHop = false;
    ph->wasSeenRecently(&p2, true, nullptr, &weWereNextHop);
    TEST_ASSERT_FALSE(weWereNextHop);
}

void test_wasUpgraded_true(void)
{
    // First observation with hop_limit=3 → stored as highestHopLimit bits 0-2 = 3
    auto p1 = makePacket(0x1111, 100, 3);
    ph->wasSeenRecently(&p1);

    // Re-observation with hop_limit=5
    // The upgrade check on line 122 compares the raw packed byte found->hop_limit against p->hop_limit.
    // found->hop_limit has highestHopLimit=3 in bits 0-2 (and possibly ourTxHopLimit in bits 3-5).
    // So the packed byte value is 3 (or more if ourTxHopLimit was set), and p->hop_limit is 5.
    // Since 3 < 5 (with no ourTxHopLimit set), this should detect an upgrade.
    auto p2 = makePacket(0x1111, 100, 5);
    bool wasUpgraded = false;
    ph->wasSeenRecently(&p2, true, nullptr, nullptr, &wasUpgraded);
    TEST_ASSERT_TRUE(wasUpgraded);
}

void test_wasUpgraded_false(void)
{
    auto p1 = makePacket(0x1111, 100, 5);
    ph->wasSeenRecently(&p1);

    // Same or lower hop_limit
    auto p2 = makePacket(0x1111, 100, 3);
    bool wasUpgraded = false;
    ph->wasSeenRecently(&p2, true, nullptr, nullptr, &wasUpgraded);
    TEST_ASSERT_FALSE(wasUpgraded);
}

// ===========================================================================
// Group 10 - Edge Cases
// ===========================================================================

void test_packet_id_zero_not_stored(void)
{
    auto p = makePacket(0x1111, 0);
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p));
    TEST_ASSERT_FALSE(ph->wasSeenRecently(&p)); // still not found
}

void test_sender_zero_substituted(void)
{
    // from=0 means "from us" - getFrom() substitutes nodeDB->getNodeNum()
    auto p = makePacket(0, 100);
    ph->wasSeenRecently(&p);

    // Should be stored under our node num, not 0
    auto p2 = makePacket(OUR_NODE_NUM, 100);
    TEST_ASSERT_TRUE(ph->wasSeenRecently(&p2, false));
}

// NOTE: Tests for the !initOk() short-circuit in wasSeenRecently / wasRelayer
// were removed: the only way to drive that branch from a test is to destruct
// the object and then call methods on it, which is UB and trips ASAN under
// `pio test -e coverage`. In practice the guard can only fire if `new[]` throws
// (in which case the constructor doesn't return), so the lost coverage is
// purely defensive.

void test_multiple_instances_independent(void)
{
    PacketHistory h2(SMALL_CAPACITY);

    auto p = makePacket(0x1111, 100);
    ph->wasSeenRecently(&p);

    // h2 should NOT find it
    TEST_ASSERT_FALSE(h2.wasSeenRecently(&p, false));

    // ph should still find it
    TEST_ASSERT_TRUE(ph->wasSeenRecently(&p, false));
}

// ===========================================================================
// Group 11 - Hash Table Stress
// ===========================================================================

void test_many_packets_no_false_negatives(void)
{
    PacketHistory big(64);
    for (uint32_t i = 1; i <= 64; i++) {
        auto p = makePacket(0xAAAA, i);
        big.wasSeenRecently(&p);
    }
    for (uint32_t i = 1; i <= 64; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_TRUE_MESSAGE(big.wasSeenRecently(&p, false), "False negative in hash table");
    }
}

void test_many_packets_no_false_positives(void)
{
    PacketHistory big(64);
    for (uint32_t i = 1; i <= 64; i++) {
        auto p = makePacket(0xAAAA, i);
        big.wasSeenRecently(&p);
    }
    // IDs 65..128 were never inserted
    for (uint32_t i = 65; i <= 128; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_FALSE_MESSAGE(big.wasSeenRecently(&p, false), "False positive in hash table");
    }
}

void test_churn_correctness(void)
{
    // Insert 3x capacity to force heavy eviction.
    // Advance time between each generation so eviction can distinguish old from new.
    PacketHistory big(32);
    uint32_t capacity = 32;
    uint32_t generations = 3;

    for (uint32_t gen = 0; gen < generations; gen++) {
        if (gen > 0)
            delay(1); // Ensure new generation has a newer timestamp than the old
        for (uint32_t i = 1; i <= capacity; i++) {
            auto p = makePacket(0xAAAA, gen * capacity + i);
            big.wasSeenRecently(&p);
        }
    }

    uint32_t total = capacity * generations;

    // Only the most recent 32 should be present (due to LRU eviction)
    for (uint32_t i = total - 31; i <= total; i++) {
        auto p = makePacket(0xAAAA, i);
        TEST_ASSERT_TRUE_MESSAGE(big.wasSeenRecently(&p, false), "Recent packet lost after churn");
    }
    // Older packets should be gone
    int found = 0;
    for (uint32_t i = 1; i <= total - capacity; i++) {
        auto p = makePacket(0xAAAA, i);
        if (big.wasSeenRecently(&p, false))
            found++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, found, "Evicted packets should not be found");
}

// ===========================================================================
// Test runner
// ===========================================================================

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    UNITY_BEGIN();

    // Group 1 - Initialization
    RUN_TEST(test_init_default_size_does_not_warn);
    RUN_TEST(test_init_minimum_size);
    RUN_TEST(test_init_too_small_falls_back);

    // Group 2 - Basic Deduplication
    RUN_TEST(test_first_packet_not_seen);
    RUN_TEST(test_same_packet_seen_twice);
    RUN_TEST(test_different_id_not_confused);
    RUN_TEST(test_different_sender_not_confused);
    RUN_TEST(test_withUpdate_false_no_insert);
    RUN_TEST(test_withUpdate_true_inserts);

    // Group 3 - LRU Eviction
    RUN_TEST(test_fill_capacity_all_found);
    RUN_TEST(test_eviction_oldest_replaced);
    RUN_TEST(test_matching_slot_reused);
    RUN_TEST(test_evict_all_old_packets);

    // Group 4 - Relayer Tracking
    RUN_TEST(test_wasRelayer_true);
    RUN_TEST(test_wasRelayer_false);
    RUN_TEST(test_wasRelayer_zero_returns_false);
    RUN_TEST(test_wasRelayer_not_found);
    RUN_TEST(test_wasRelayer_wasSole_true);
    RUN_TEST(test_wasRelayer_wasSole_false);
    RUN_TEST(test_wasRelayer_all_six_slots);

    // Group 5 - removeRelayer
    RUN_TEST(test_removeRelayer_removes);
    RUN_TEST(test_removeRelayer_compacts);
    RUN_TEST(test_removeRelayer_nonexistent_safe);
    RUN_TEST(test_removeRelayer_packet_not_found_safe);

    // Group 6 - checkRelayers
    RUN_TEST(test_checkRelayers_both_found);
    RUN_TEST(test_checkRelayers_one_found);
    RUN_TEST(test_checkRelayers_r2WasSole);

    // Group 7 - Merge Logic
    RUN_TEST(test_merge_preserves_original_next_hop);
    RUN_TEST(test_merge_preserves_highest_hop_limit);
    RUN_TEST(test_merge_heard_back_stores_relay_node);

    // Group 8 - Fallback-to-Flooding Detection
    RUN_TEST(test_fallback_detected);
    RUN_TEST(test_fallback_not_when_we_relayed);
    RUN_TEST(test_fallback_not_on_first_observation);

    // Group 9 - Next-Hop and Upgrade Detection
    RUN_TEST(test_weWereNextHop_true);
    RUN_TEST(test_weWereNextHop_false);
    RUN_TEST(test_wasUpgraded_true);
    RUN_TEST(test_wasUpgraded_false);

    // Group 10 - Edge Cases
    RUN_TEST(test_packet_id_zero_not_stored);
    RUN_TEST(test_sender_zero_substituted);
    RUN_TEST(test_multiple_instances_independent);

    // Group 11 - Hash Table Stress
    RUN_TEST(test_many_packets_no_false_negatives);
    RUN_TEST(test_many_packets_no_false_positives);
    RUN_TEST(test_churn_correctness);

    exit(UNITY_END());
}

void loop() {}
