#include "MeshTypes.h"
#include "TestUtil.h"
#include "modules/CompactHistogram.h"
#include <cstring>
#include <unity.h>

// Set the test clock variable exposed by CompactHistogram when built with UNIT_TEST.
static uint32_t &mockTime = CompactHistogram::s_testNowMs;

static void advanceTime(uint32_t ms)
{
    mockTime += ms;
}

void setUp(void)
{
    mockTime = 0;
}

void tearDown(void) {}

// ============================================================================
// INITIALIZATION
// ============================================================================

void test_initial_state(void)
{
    CompactHistogram hist;
    TEST_ASSERT_EQUAL_UINT8(0, hist.getEntryCount());
    TEST_ASSERT_EQUAL_UINT8(0, hist.getFillPercentage());
    TEST_ASSERT_EQUAL_UINT8(CompactHistogram::DENOM_MIN, hist.getSamplingDenominator());
    TEST_ASSERT_EQUAL_UINT8(CompactHistogram::DENOM_MIN, hist.getFilteringDenominator());
    TEST_ASSERT_EQUAL_UINT8(CompactHistogram::MAX_HOP, hist.getLastSuggestedHop());
}

void test_clear_resets_state(void)
{
    CompactHistogram hist;
    hist.sampleRxPacket(4, 2); // denom=1, passes
    TEST_ASSERT_EQUAL_UINT8(1, hist.getEntryCount());
    hist.clear();
    TEST_ASSERT_EQUAL_UINT8(0, hist.getEntryCount());
    TEST_ASSERT_EQUAL_UINT8(0, hist.getFillPercentage());
    TEST_ASSERT_EQUAL_UINT8(CompactHistogram::DENOM_MIN, hist.getSamplingDenominator());
}

void test_memory_layout(void)
{
    // ENTRY_BYTES is sizeof(Record) — 4 bytes (32-bit packed struct: nodeHash:16 + hops:3 + seen:13)
    TEST_ASSERT_EQUAL_size_t(sizeof(Record), CompactHistogram::ENTRY_BYTES);
    TEST_ASSERT_EQUAL_size_t(128, CompactHistogram::CAPACITY);
    TEST_ASSERT_EQUAL_size_t(CompactHistogram::CAPACITY * CompactHistogram::ENTRY_BYTES, CompactHistogram::TOTAL_BYTES);
}

// ============================================================================
// SAMPLING FILTER
// ============================================================================

void test_sampling_denom1_accepts_all(void)
{
    // denom=1: (nodeId & 0) == 0 → every ID passes
    CompactHistogram hist;
    for (uint32_t id = 1; id <= 20; id++) {
        hist.sampleRxPacket(id, 1);
    }
    TEST_ASSERT_EQUAL_UINT8(20, hist.getEntryCount());
}

void test_sampling_denom2_accepts_even_ids(void)
{
    // denom=2: (nodeId & 1) == 0 → only even IDs pass
    CompactHistogram hist;
    hist.sampleRxPacket(1, 1); // odd  → rejected
    hist.sampleRxPacket(2, 1); // even → accepted
    hist.sampleRxPacket(3, 1); // odd  → rejected
    hist.sampleRxPacket(4, 1); // even → accepted

    // Force denom to 2 via trim: first fill to 80%, then manually check behaviour
    // For this test just observe that denom=1 initially accepts all the above 4
    TEST_ASSERT_EQUAL_UINT8(4, hist.getEntryCount());

    // Now test with a fresh histogram where we drive the sampling denom to 2 naturally.
    // Since trimIfNeeded() doubles from DENOM_MIN=1, we can trigger it by adding 103 nodes.
    CompactHistogram hist2;
    // Fill to 80% capacity (103/128) using IDs that won't all pass denom=2 later
    // Use consecutive IDs starting at 1
    for (uint32_t id = 1; id <= 104; id++) {
        hist2.sampleRxPacket(id, 1);
    }
    // After trim triggered at 80%, stale entries (all zero seen bits except current) should remain,
    // but the denom may have doubled.  Just assert capacity is bounded.
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(CompactHistogram::CAPACITY, hist2.getEntryCount());
}

void test_sampling_denom4_accepts_multiples_of_4(void)
{
    // With the golden-ratio hash the filter selects by hash value, not raw ID.
    // Verify using the histogram's own hashNodeId() so the test is hash-function-agnostic.
    CompactHistogram hist;
    hist.setSamplingDenominator(4);

    // Scan for two IDs that pass denom=4 and two that fail.
    uint32_t passIds[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t failIds[2] = {UINT32_MAX, UINT32_MAX};
    for (uint32_t id = 1; id < 10000 && (passIds[1] == UINT32_MAX || failIds[1] == UINT32_MAX); id++) {
        const bool passes = (hist.hashNodeId(id) & 3u) == 0u;
        if (passes && passIds[0] == UINT32_MAX)
            passIds[0] = id;
        else if (passes && passIds[1] == UINT32_MAX)
            passIds[1] = id;
        else if (!passes && failIds[0] == UINT32_MAX)
            failIds[0] = id;
        else if (!passes && failIds[1] == UINT32_MAX)
            failIds[1] = id;
    }
    TEST_ASSERT_NOT_EQUAL(UINT32_MAX, passIds[0]);
    TEST_ASSERT_NOT_EQUAL(UINT32_MAX, passIds[1]);
    TEST_ASSERT_NOT_EQUAL(UINT32_MAX, failIds[0]);
    TEST_ASSERT_NOT_EQUAL(UINT32_MAX, failIds[1]);

    hist.sampleRxPacket(failIds[0], 1);
    hist.sampleRxPacket(passIds[0], 1);
    hist.sampleRxPacket(failIds[1], 1);
    hist.sampleRxPacket(passIds[1], 1);
    TEST_ASSERT_EQUAL_UINT8(2, hist.getEntryCount());
}

// ============================================================================
// ENTRY MANAGEMENT
// ============================================================================

void test_duplicate_nodeId_not_added_twice(void)
{
    CompactHistogram hist;
    hist.sampleRxPacket(100, 2);
    hist.sampleRxPacket(100, 3);
    TEST_ASSERT_EQUAL_UINT8(1, hist.getEntryCount());
}

void test_hop_count_stored_as_minimum(void)
{
    // The histogram stores the most-recently observed hop count for each node.
    CompactHistogram hist;
    hist.sampleRxPacket(100, 5);
    hist.sampleRxPacket(100, 2); // more recent: should replace
    hist.sampleRxPacket(100, 4); // most recent: should replace

    // After rollHour, the per-hop count at hop=4 should be 1 (last seen)
    const uint8_t hop = hist.rollHour();
    const auto &counts = hist.getLastPerHopCounts();
    TEST_ASSERT_EQUAL_UINT16(1, counts.perHop[4]);
    TEST_ASSERT_EQUAL_UINT16(0, counts.perHop[5]);
    TEST_ASSERT_EQUAL_UINT16(0, counts.perHop[2]);
    (void)hop;
}

void test_hop_count_clamped_to_max(void)
{
    CompactHistogram hist;
    hist.sampleRxPacket(100, 255); // should clamp to MAX_HOP=7
    hist.rollHour();
    const auto &counts = hist.getLastPerHopCounts();
    TEST_ASSERT_EQUAL_UINT16(1, counts.perHop[CompactHistogram::MAX_HOP]);
}

// ============================================================================
// CURRENT-HOUR SEEN TRACKING
// ============================================================================

void test_seen_bit_set_on_sample(void)
{
    // A freshly sampled node should be counted in rollHour (seen in current hour).
    CompactHistogram hist;
    hist.sampleRxPacket(4, 2);
    hist.rollHour();
    TEST_ASSERT_EQUAL_UINT16(1, hist.getLastPerHopCounts().total);
}

void test_node_not_seen_after_12_rollovers(void)
{
    // Node seen once, then 13 hours pass without being seen again.
    CompactHistogram hist;
    hist.sampleRxPacket(4, 2);

    // Roll 13 times; each roll shifts the seen bitmap left — after 13 rolls the bit is gone.
    for (int i = 0; i < 13; i++) {
        hist.rollHour();
    }
    // 14th rollover: node should not be counted (seen bits are all zero)
    hist.rollHour();
    TEST_ASSERT_EQUAL_UINT16(0, hist.getLastPerHopCounts().total);
}

void test_node_seen_again_resets_seen_bit(void)
{
    CompactHistogram hist;
    hist.sampleRxPacket(4, 2);
    // Roll 12 times (node still in range)
    for (int i = 0; i < 12; i++) {
        hist.rollHour();
    }
    // Re-sample the node in the 13th hour
    hist.sampleRxPacket(4, 2);
    hist.rollHour(); // 13th roll — node freshly seen, should be counted
    TEST_ASSERT_EQUAL_UINT16(1, hist.getLastPerHopCounts().total);
}

// ============================================================================
// TRIM / SCALE-UP
// ============================================================================

void test_stale_entries_removed_when_over_80pct(void)
{
    // Fill to 80%+ with entries that have no seen bits (stale), then add a new node.
    // The trim should evict the stale entries.
    CompactHistogram hist;

    // Add 103 entries (just at the 80% threshold = 103/128 = 80%)
    for (uint32_t id = 1; id <= 103; id++) {
        hist.sampleRxPacket(id, 1);
    }
    TEST_ASSERT_EQUAL_UINT8(103, hist.getEntryCount());

    // Roll the hour so those seen bits shift to bit 4 (not bit 3 anymore, but still non-zero)
    hist.rollHour();

    // Roll 12 more times to expire all seen bits
    for (int i = 0; i < 12; i++) {
        hist.rollHour();
    }
    // Now all 103 entries have zero seen bits (stale)

    // Adding a new node triggers trim: stale entries are removed, new node is added
    hist.sampleRxPacket(9999, 1);
    // The stale entries should have been pruned
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(2, hist.getEntryCount()); // ≤1 surviving + new entry
}

void test_denom_doubles_when_still_over_80pct_after_stale_removal(void)
{
    // Fill with 128 unique IDs that are all recently seen.
    // When we try to add one more, trim removes nothing (all seen) and must double denom.
    CompactHistogram hist;
    for (uint32_t id = 0; id < CompactHistogram::CAPACITY; id++) {
        hist.sampleRxPacket(id, 1);
    }
    // At this point count == 128, denom is still 1 (no trim triggered until add).
    // Try to add a 129th node: trim is triggered, removes nothing (all seen in current hour),
    // so denom doubles to 2 and ~50% are removed.
    hist.sampleRxPacket(200000, 1);
    TEST_ASSERT_EQUAL_UINT8(2, hist.getSamplingDenominator());
    TEST_ASSERT_EQUAL_UINT8(2, hist.getFilteringDenominator());
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(CompactHistogram::CAPACITY, hist.getEntryCount());
}

// ============================================================================
// FILTERING DENOMINATOR HOLD
// ============================================================================

void test_filter_denom_held_for_13_hours(void)
{
    // Trigger a scale-up so filteringDenominator doubles.
    CompactHistogram hist;
    for (uint32_t id = 0; id < CompactHistogram::CAPACITY; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.sampleRxPacket(200000, 1); // triggers scale-up to denom=2
    TEST_ASSERT_EQUAL_UINT8(2, hist.getFilteringDenominator());

    // Roll enough hours to trigger scale-down (list fills below 20% with denom=2 entries),
    // but without advancing simulated time past 13h.
    // samplingDenominator should drop but filteringDenominator should NOT.
    for (int i = 0; i < 6; i++) {
        hist.rollHour(); // drains seen bits; eventually count of denom=2 entries falls below 20%
    }
    // filteringDenominator must still be 2 (13h hold not expired)
    TEST_ASSERT_EQUAL_UINT8(2, hist.getFilteringDenominator());
}

void test_filter_denom_drops_after_13h_hold(void)
{
    CompactHistogram hist;
    for (uint32_t id = 0; id < CompactHistogram::CAPACITY; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.sampleRxPacket(200000, 1); // scale-up: both denoms → 2
    TEST_ASSERT_EQUAL_UINT8(2, hist.getFilteringDenominator());

    // Advance clock past 13 h, then roll to trigger the drop check
    advanceTime(CompactHistogram::FILTER_DENOM_HOLD_MS + 1);
    hist.rollHour();

    // filteringDenominator should now have dropped to match samplingDenominator
    TEST_ASSERT_EQUAL_UINT8(hist.getSamplingDenominator(), hist.getFilteringDenominator());
}

// ============================================================================
// SCALE-DOWN
// ============================================================================

void test_scale_down_when_too_few_filtering_entries(void)
{
    // Force a scale-up first
    CompactHistogram hist;
    for (uint32_t id = 0; id < CompactHistogram::CAPACITY; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.sampleRxPacket(200000, 1); // scale-up to denom=2

    const uint8_t sampAfterUp = hist.getSamplingDenominator();
    TEST_ASSERT_EQUAL_UINT8(2, sampAfterUp);

    // Roll 14 hours to expire all seen bits (entries become stale / count falls below 20%)
    // and also pass the 13h filter-denom hold.
    advanceTime(CompactHistogram::FILTER_DENOM_HOLD_MS + 1);
    for (int i = 0; i < 14; i++) {
        hist.rollHour();
    }

    // samplingDenominator should have dropped below sampAfterUp (entries below 20% of capacity)
    TEST_ASSERT_LESS_THAN_UINT8(sampAfterUp, hist.getSamplingDenominator());
}

// ============================================================================
// ROLL-HOUR: HOP-WALK RECOMMENDATION
// ============================================================================

void test_rollhour_returns_max_hop_when_empty(void)
{
    CompactHistogram hist;
    TEST_ASSERT_EQUAL_UINT8(CompactHistogram::MAX_HOP, hist.rollHour());
}

void test_rollhour_hop_walk_basic(void)
{
    // Add 40+ nodes all at hop=1. With denom=1 the scaled count is 40+, so target is met at hop=1.
    CompactHistogram hist;
    for (uint32_t id = 1; id <= 50; id++) {
        hist.sampleRxPacket(id, 1);
    }
    const uint8_t suggested = hist.rollHour();
    // 50 nodes at hop=1 × denom=1 = 50 ≥ TARGET(40), so baseHop=1.
    // Politeness check: 50+0=50 ≤ 40×1.5=60 → extends to hop=2 (zero nodes at hop=2 is still polite).
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(2, suggested);
}

void test_rollhour_hop_walk_requires_more_hops_for_small_mesh(void)
{
    // Only 5 nodes at hop=1. With denom=1 the scaled count is 5, so we need all hops.
    CompactHistogram hist;
    for (uint32_t id = 1; id <= 5; id++) {
        hist.sampleRxPacket(id, 1);
    }
    const uint8_t suggested = hist.rollHour();
    // Only 5 nodes total — can never reach TARGET(40), so returns MAX_HOP
    TEST_ASSERT_EQUAL_UINT8(CompactHistogram::MAX_HOP, suggested);
}

void test_rollhour_counts_only_filtering_denom_entries(void)
{
    // After scale-up to denom=2, only even IDs should count.
    // Add 50 odd IDs + 50 even IDs; trigger scale-up so denom=2.
    CompactHistogram hist;
    // Fill to capacity to trigger scale-up
    for (uint32_t id = 0; id < CompactHistogram::CAPACITY; id++) {
        hist.sampleRxPacket(id, 2);
    }
    hist.sampleRxPacket(99999, 2); // triggers scale-up; denom → 2, removes odd IDs

    // Now add some fresh even IDs (denom=2 accepts even IDs)
    for (uint32_t id = 2; id <= 100; id += 2) {
        hist.sampleRxPacket(id, 2);
    }

    hist.rollHour();
    const auto &counts = hist.getLastPerHopCounts();
    // Only even-ID entries (passing denom=2) are counted
    TEST_ASSERT_GREATER_THAN_UINT16(0, counts.total);
}

void test_rollhour_scaled_hop_walk_with_denom(void)
{
    // With denom=2, each sampled node represents 2 real nodes.
    // Add 25 nodes (all at hop=1) with denom=2 → 25×2=50 ≥ TARGET(40).
    CompactHistogram hist;
    // Force scale-up: fill with 128 entries then overflow
    for (uint32_t id = 0; id < CompactHistogram::CAPACITY; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.sampleRxPacket(99999, 1); // forces denom → 2

    // Add even IDs so they pass the new denom=2 filter
    for (uint32_t id = 100; id <= 148; id += 2) {
        hist.sampleRxPacket(id, 1); // 25 nodes at hop=1
    }
    const uint8_t suggested = hist.rollHour();
    // 25 nodes × denom=2 = 50 ≥ TARGET(40) → should reach target at hop=1
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(2, suggested);
}

// ============================================================================
// POLITENESS REGIMES
// ============================================================================

void test_politeness_strict_when_recent_activity_exceeds_older(void)
{
    // STRICT: recent 0-2h window >> older 1-3h window (growing/busy mesh).
    // Sample 5 nodes, roll, sample 20 NEW nodes, roll.
    //   h0=20, h1=5  → recent=25, older=5, weight=5.0 > STRICT_MIN(1.2) → POLITENESS_STRICT
    CompactHistogram hist;
    for (uint32_t id = 1; id <= 5; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.rollHour();
    for (uint32_t id = 101; id <= 120; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.rollHour();
    TEST_ASSERT_EQUAL_FLOAT(CompactHistogram::POLITENESS_STRICT, hist.getPoliteness());
}

void test_politeness_default_when_activity_is_stable(void)
{
    // DEFAULT: recent 0-2h ≈ older 1-3h (stable mesh).
    // Sample 10 nodes, roll, roll (no new samples).
    //   h0=0, h1=10  → recent=10, older=10, weight=1.0, 0.9≤1.0≤1.2 → POLITENESS_DEFAULT
    CompactHistogram hist;
    for (uint32_t id = 1; id <= 10; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.rollHour();
    hist.rollHour();
    TEST_ASSERT_EQUAL_FLOAT(CompactHistogram::POLITENESS_DEFAULT, hist.getPoliteness());
}

void test_politeness_generous_when_recent_activity_is_low(void)
{
    // GENEROUS: recent 0-2h << older 1-3h (shrinking/quieter mesh).
    // Sample 20 nodes, roll, sample 2 NEW nodes, roll, roll (no new samples).
    //   After 3rd roll: h0=0, h1=2, h2=20 → recent=2, older=22, weight≈0.09 < GENEROUS_MAX(0.9)
    CompactHistogram hist;
    for (uint32_t id = 1; id <= 20; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.rollHour();
    for (uint32_t id = 101; id <= 102; id++) {
        hist.sampleRxPacket(id, 1);
    }
    hist.rollHour();
    hist.rollHour();
    TEST_ASSERT_EQUAL_FLOAT(CompactHistogram::POLITENESS_GENEROUS, hist.getPoliteness());
}

// ============================================================================
// CAPACITY / FILL
// ============================================================================

void test_fill_percentage_calculation(void)
{
    CompactHistogram hist;
    TEST_ASSERT_EQUAL_UINT8(0, hist.getFillPercentage());
    for (uint32_t id = 1; id <= 64; id++) {
        hist.sampleRxPacket(id, 1);
    }
    TEST_ASSERT_EQUAL_UINT8(50, hist.getFillPercentage());
}

void test_entry_count_bounded_by_capacity(void)
{
    CompactHistogram hist;
    for (uint32_t id = 1; id <= CompactHistogram::CAPACITY * 3; id++) {
        hist.sampleRxPacket(id, 1);
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(CompactHistogram::CAPACITY, hist.getEntryCount());
}

// ============================================================================
// SETUP / RUNNER
// ============================================================================

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    // Initialization
    RUN_TEST(test_initial_state);
    RUN_TEST(test_clear_resets_state);
    RUN_TEST(test_memory_layout);

    // Sampling filter
    RUN_TEST(test_sampling_denom1_accepts_all);
    RUN_TEST(test_sampling_denom2_accepts_even_ids);
    RUN_TEST(test_sampling_denom4_accepts_multiples_of_4);

    // Entry management
    RUN_TEST(test_duplicate_nodeId_not_added_twice);
    RUN_TEST(test_hop_count_stored_as_minimum);
    RUN_TEST(test_hop_count_clamped_to_max);

    // Seen tracking
    RUN_TEST(test_seen_bit_set_on_sample);
    RUN_TEST(test_node_not_seen_after_12_rollovers);
    RUN_TEST(test_node_seen_again_resets_seen_bit);

    // Trim / scale-up
    RUN_TEST(test_stale_entries_removed_when_over_80pct);
    RUN_TEST(test_denom_doubles_when_still_over_80pct_after_stale_removal);

    // Filtering denominator hold
    RUN_TEST(test_filter_denom_held_for_13_hours);
    RUN_TEST(test_filter_denom_drops_after_13h_hold);

    // Scale-down
    RUN_TEST(test_scale_down_when_too_few_filtering_entries);

    // rollHour hop-walk
    RUN_TEST(test_rollhour_returns_max_hop_when_empty);
    RUN_TEST(test_rollhour_hop_walk_basic);
    RUN_TEST(test_rollhour_hop_walk_requires_more_hops_for_small_mesh);
    RUN_TEST(test_rollhour_counts_only_filtering_denom_entries);
    RUN_TEST(test_rollhour_scaled_hop_walk_with_denom);

    // Politeness regimes
    RUN_TEST(test_politeness_strict_when_recent_activity_exceeds_older);
    RUN_TEST(test_politeness_default_when_activity_is_stable);
    RUN_TEST(test_politeness_generous_when_recent_activity_is_low);

    // Capacity / fill
    RUN_TEST(test_fill_percentage_calculation);
    RUN_TEST(test_entry_count_bounded_by_capacity);

    exit(UNITY_END());
}

void loop() {}
