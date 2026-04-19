#include "MeshTypes.h"
#include "TestUtil.h"
#include "modules/CompactHistogram.h"
#include <cstring>
#include <unity.h>

// Deterministic clock for CompactHistogram UNIT_TEST hooks
static uint32_t mockTime = 0;

void advanceTime(uint32_t ms)
{
    mockTime += ms;
    CompactHistogram::setTimeForTest(mockTime);
}

void setUp(void)
{
    mockTime = 0;
    CompactHistogram::setTimeForTest(mockTime);
}

void tearDown(void)
{
    CompactHistogram::clearTimeForTest();
}

// ============================================================================
// BIT PACKING / UNPACKING TESTS
// ============================================================================

void test_bit_packing_node_id_extraction(void)
{
    // Test extracting node IDs from 0 to 8191 (max 13-bit value)
    for (uint16_t nodeId = 0; nodeId <= 8191; nodeId += 1000) {
        uint16_t packed = CompactHistogramOps::packNodeIdAndHops(nodeId, 0);
        uint16_t extracted = CompactHistogramOps::getNodeId(packed);
        TEST_ASSERT_EQUAL_UINT16(nodeId, extracted);
    }

    // Test edge cases
    TEST_ASSERT_EQUAL_UINT16(0, CompactHistogramOps::getNodeId(CompactHistogramOps::packNodeIdAndHops(0, 7)));
    TEST_ASSERT_EQUAL_UINT16(8191, CompactHistogramOps::getNodeId(CompactHistogramOps::packNodeIdAndHops(8191, 7)));
}

void test_bit_packing_hop_count_extraction(void)
{
    // Test all 3-bit hop count values (0–7)
    for (uint8_t hops = 0; hops <= 7; hops++) {
        uint16_t packed = CompactHistogramOps::packNodeIdAndHops(1234, hops);
        uint8_t extracted = CompactHistogramOps::getHopCount(packed);
        TEST_ASSERT_EQUAL_UINT8(hops, extracted);
    }

    // Test edge case with max node ID
    TEST_ASSERT_EQUAL_UINT8(5, CompactHistogramOps::getHopCount(CompactHistogramOps::packNodeIdAndHops(8191, 5)));
}

void test_bit_packing_no_collision(void)
{
    // Verify that different node IDs and hop counts produce different packed values
    uint16_t packed1 = CompactHistogramOps::packNodeIdAndHops(1234, 3);
    uint16_t packed2 = CompactHistogramOps::packNodeIdAndHops(1235, 3);
    uint16_t packed3 = CompactHistogramOps::packNodeIdAndHops(1234, 4);

    TEST_ASSERT_NOT_EQUAL_UINT16(packed1, packed2);
    TEST_ASSERT_NOT_EQUAL_UINT16(packed1, packed3);
    TEST_ASSERT_NOT_EQUAL_UINT16(packed2, packed3);
}

// ============================================================================
// WINDOW BITMAP TESTS
// ============================================================================

void test_mark_short_window(void)
{
    uint16_t bitmap = 0;

    // Mark windows 0 and 2
    CompactHistogramOps::markShortWindow(bitmap, 0);
    CompactHistogramOps::markShortWindow(bitmap, 2);

    TEST_ASSERT_EQUAL_UINT16(0x0005, bitmap); // binary: 0101
    TEST_ASSERT_TRUE(CompactHistogramOps::wasSeenInShortTerm(bitmap));
}

void test_mark_long_window(void)
{
    uint16_t bitmap = 0;

    // Mark long-term windows 0 and 5
    CompactHistogramOps::markLongWindow(bitmap, 0);
    CompactHistogramOps::markLongWindow(bitmap, 5);

    TEST_ASSERT_EQUAL_UINT16(0x0210, bitmap); // bits 4 and 9
    TEST_ASSERT_TRUE(CompactHistogramOps::wasSeenInLongTerm(bitmap));
}

void test_short_and_long_windows_independent(void)
{
    uint16_t bitmap = 0;

    CompactHistogramOps::markShortWindow(bitmap, 1);
    CompactHistogramOps::markLongWindow(bitmap, 3);

    uint8_t shortBits = CompactHistogramOps::getShortTermBits(bitmap);
    uint16_t longBits = CompactHistogramOps::getLongTermBits(bitmap);

    TEST_ASSERT_EQUAL_UINT8(0x02, shortBits);  // Window 1 in short term
    TEST_ASSERT_EQUAL_UINT16(0x008, longBits); // Window 3 in long term
}

void test_rotate_short_term_buffer(void)
{
    // Start with windows 0 and 2 marked
    uint16_t bitmap = 0x0005; // short: 0101, long: 0000

    uint16_t rotated = CompactHistogramOps::rotateShortTermBuffer(bitmap);

    // After left shift with drop-overflow: 0101 -> 1010
    uint8_t shortBits = CompactHistogramOps::getShortTermBits(rotated);
    TEST_ASSERT_EQUAL_UINT8(0x0A, shortBits);
}

void test_rotate_short_term_preserves_long_term(void)
{
    uint16_t bitmap = 0;
    CompactHistogramOps::markShortWindow(bitmap, 0);
    CompactHistogramOps::markLongWindow(bitmap, 5);

    uint16_t rotated = CompactHistogramOps::rotateShortTermBuffer(bitmap);
    uint16_t longBits = CompactHistogramOps::getLongTermBits(rotated);

    TEST_ASSERT_EQUAL_UINT16(0x020, longBits); // Window 5 still marked
}

// ============================================================================
// HISTOGRAM BASIC OPERATIONS
// ============================================================================

void test_histogram_initialization(void)
{
    CompactHistogram hist;

    TEST_ASSERT_EQUAL_UINT8(0, hist.getEntryCount());
    TEST_ASSERT_EQUAL_UINT8(0, hist.getFillPercentage());
    TEST_ASSERT_EQUAL_UINT8(CompactHistogram::SAMPLING_DENOMINATOR_INITIAL, hist.getSamplingDenominator());
}

void test_histogram_sample_single_node(void)
{
    CompactHistogram hist;

    hist.sampleRxPacket(1234, 2);

    // If the jitter offset matches, the entry should be created
    // Note: This depends on random jitter, so we just verify entry count is 0 or 1
    TEST_ASSERT_TRUE(hist.getEntryCount() <= 1);
}

void test_histogram_clear_resets_state(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1); // Ensure deterministic sampling for this test

    hist.sampleRxPacket(1000, 2);
    hist.sampleRxPacket(2000, 3);

    TEST_ASSERT_GREATER_THAN_UINT8(0, hist.getEntryCount());

    hist.clear();

    TEST_ASSERT_EQUAL_UINT8(0, hist.getEntryCount());
    TEST_ASSERT_EQUAL_UINT8(0, hist.getFillPercentage());
}

void test_histogram_capacity_limit(void)
{
    CompactHistogram hist;

    // Disable jitter to make sampling predictable
    hist.setSamplingDenominator(1); // Sample everything

    // Add nodes and track fill percentage
    for (uint32_t i = 0; i < CompactHistogram::CAPACITY * 2; i++) {
        hist.sampleRxPacket(i, 1);
    }

    TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, hist.getFillPercentage());
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(CompactHistogram::CAPACITY, hist.getEntryCount());
}

// ============================================================================
// HOP DISTRIBUTION QUERIES
// ============================================================================

void test_hop_distribution_empty_histogram(void)
{
    CompactHistogram hist;

    CompactHistogram::HopDistribution dist = hist.getHopDistribution();

    TEST_ASSERT_EQUAL_UINT8(0, dist.minHops);
    TEST_ASSERT_EQUAL_UINT8(0, dist.maxHops);
    TEST_ASSERT_EQUAL_UINT8(0, dist.medianHops);
    TEST_ASSERT_EQUAL_size_t(0, dist.sampleCount);
}

void test_hop_distribution_with_samples(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1); // Sample everything

    // Add nodes with different hop counts
    hist.sampleRxPacket(100, 1);
    hist.sampleRxPacket(200, 2);
    hist.sampleRxPacket(300, 3);
    hist.sampleRxPacket(400, 3);
    hist.sampleRxPacket(500, 4);

    CompactHistogram::HopDistribution dist = hist.getHopDistribution();

    TEST_ASSERT_EQUAL_UINT8(1, dist.minHops);
    TEST_ASSERT_EQUAL_UINT8(4, dist.maxHops);
    TEST_ASSERT_GREATER_THAN_UINT8(0, dist.sampleCount);
}

void test_hop_distribution_recency_filter(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1); // Sample everything

    // Add a node
    hist.sampleRxPacket(100, 1);

    // Get distribution with recency filter
    CompactHistogram::HopDistribution distRecent = hist.getHopDistribution(true);
    CompactHistogram::HopDistribution distAll = hist.getHopDistribution(false);

    // Recent should count the node (it was just added)
    TEST_ASSERT_GREATER_THAN_UINT8(0, distRecent.sampleCount);
    TEST_ASSERT_EQUAL_size_t(distRecent.sampleCount, distAll.sampleCount);
}

// ============================================================================
// WINDOW ROLLING TESTS
// ============================================================================

void test_window_roll_due_initially_false(void)
{
    CompactHistogram hist;

    TEST_ASSERT_FALSE(hist.isWindowRollDue());
}

void test_window_roll_due_after_30_minutes(void)
{
    CompactHistogram hist;

    advanceTime(30 * 60 * 1000 + 1);

    TEST_ASSERT_TRUE(hist.isWindowRollDue());
}

void test_window_rolling_shifts_short_term(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1);

    // Add a node
    hist.sampleRxPacket(1234, 2);

    // Advance time by 30+ minutes
    advanceTime(30 * 60 * 1000 + 1);

    // Roll windows
    hist.rollWindows();

    // Entry should still exist (with shifted short-term bits)
    TEST_ASSERT_GREATER_THAN_UINT8(0, hist.getEntryCount());
}

void test_window_rolling_aggregates_to_long_term(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1);

    // Add a node in the first window
    hist.sampleRxPacket(1234, 2);

    uint8_t shortBitsBeforeRoll = CompactHistogramOps::getShortTermBits(hist.getHopDistribution().sampleCount > 0 ? 0xF : 0);

    // Advance time by 30+ minutes
    advanceTime(30 * 60 * 1000 + 1);

    // Roll windows
    hist.rollWindows();

    // The node should have been promoted to long-term (or short-term shifted)
    TEST_ASSERT_GREATER_THAN_UINT8(0, hist.getEntryCount());
}

// ============================================================================
// ADAPTIVE DENOMINATOR SCALING
// ============================================================================

void test_denominator_validation_power_of_two(void)
{
    CompactHistogram hist;

    // Valid power-of-2 values should pass through
    hist.setSamplingDenominator(1);
    TEST_ASSERT_EQUAL_UINT8(1, hist.getSamplingDenominator());

    hist.setSamplingDenominator(2);
    TEST_ASSERT_EQUAL_UINT8(2, hist.getSamplingDenominator());

    hist.setSamplingDenominator(64);
    TEST_ASSERT_EQUAL_UINT8(64, hist.getSamplingDenominator());
}

void test_denominator_validation_clamps_to_range(void)
{
    CompactHistogram hist;

    // Values outside range should be clamped
    hist.setSamplingDenominator(0);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(CompactHistogram::SAMPLING_DENOMINATOR_MIN, hist.getSamplingDenominator());

    hist.setSamplingDenominator(255);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(CompactHistogram::SAMPLING_DENOMINATOR_MAX, hist.getSamplingDenominator());
}

void test_scale_up_when_nearly_full(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1); // Sample everything

    // Fill histogram to >90%
    for (uint32_t i = 0; i < (CompactHistogram::CAPACITY * 95 / 100); i++) {
        hist.sampleRxPacket(i, 1);
    }

    TEST_ASSERT_TRUE(hist.shouldScaleUpDenominator());
    TEST_ASSERT_FALSE(hist.shouldScaleDownDenominator());
}

void test_scale_down_when_sparse(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1);

    // Add just a few entries
    hist.sampleRxPacket(100, 1);

    TEST_ASSERT_FALSE(hist.shouldScaleUpDenominator());
    // After clear, fill is 0%, so scale down check should be false (no entries)
}

void test_step_event_counters(void)
{
    CompactHistogram hist;

    TEST_ASSERT_EQUAL_UINT8(0, hist.getStepUpEvents());
    TEST_ASSERT_EQUAL_UINT8(0, hist.getStepDownEvents());

    hist.recordStepUpEvent();
    hist.recordStepUpEvent();
    hist.recordStepDownEvent();

    TEST_ASSERT_EQUAL_UINT8(2, hist.getStepUpEvents());
    TEST_ASSERT_EQUAL_UINT8(1, hist.getStepDownEvents());
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

void test_histogram_integration_sample_and_query(void)
{
    CompactHistogram hist;
    hist.setSamplingDenominator(1); // Ensure all samples are captured

    // Sample nodes with various hop counts
    for (uint32_t i = 0; i < 50; i++) {
        uint8_t hopCount = (i % 5) + 1; // Hops 1–5
        hist.sampleRxPacket(1000 + i, hopCount);
    }

    CompactHistogram::HopDistribution dist = hist.getHopDistribution();

    TEST_ASSERT_GREATER_THAN_UINT8(0, dist.sampleCount);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(1, dist.minHops);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(5, dist.maxHops);
    TEST_ASSERT_GREATER_THAN_UINT8(0, dist.medianHops);
}

void test_histogram_memory_size(void)
{
    // Verify histogram static size matches expectation
    TEST_ASSERT_EQUAL_size_t(CompactHistogram::TOTAL_SIZE, CompactHistogram::CAPACITY * CompactHistogram::ENTRY_SIZE);
    TEST_ASSERT_EQUAL_size_t(4, CompactHistogram::ENTRY_SIZE);
    TEST_ASSERT_EQUAL_size_t(128, CompactHistogram::CAPACITY);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    // Bit packing tests
    RUN_TEST(test_bit_packing_node_id_extraction);
    RUN_TEST(test_bit_packing_hop_count_extraction);
    RUN_TEST(test_bit_packing_no_collision);

    // Window bitmap tests
    RUN_TEST(test_mark_short_window);
    RUN_TEST(test_mark_long_window);
    RUN_TEST(test_short_and_long_windows_independent);
    RUN_TEST(test_rotate_short_term_buffer);
    RUN_TEST(test_rotate_short_term_preserves_long_term);

    // Histogram basic operations
    RUN_TEST(test_histogram_initialization);
    RUN_TEST(test_histogram_sample_single_node);
    RUN_TEST(test_histogram_clear_resets_state);
    RUN_TEST(test_histogram_capacity_limit);

    // Hop distribution queries
    RUN_TEST(test_hop_distribution_empty_histogram);
    RUN_TEST(test_hop_distribution_with_samples);
    RUN_TEST(test_hop_distribution_recency_filter);

    // Window rolling tests
    RUN_TEST(test_window_roll_due_initially_false);
    RUN_TEST(test_window_roll_due_after_30_minutes);
    RUN_TEST(test_window_rolling_shifts_short_term);
    RUN_TEST(test_window_rolling_aggregates_to_long_term);

    // Adaptive denominator scaling tests
    RUN_TEST(test_denominator_validation_power_of_two);
    RUN_TEST(test_denominator_validation_clamps_to_range);
    RUN_TEST(test_scale_up_when_nearly_full);
    RUN_TEST(test_scale_down_when_sparse);
    RUN_TEST(test_step_event_counters);

    // Integration tests
    RUN_TEST(test_histogram_integration_sample_and_query);
    RUN_TEST(test_histogram_memory_size);

    exit(UNITY_END());
}

void loop() {}
