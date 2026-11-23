// Unit tests for VeDirectSource in src/modules/Telemetry/VeDirectSource.cpp - the decoder for the
// Victron VE.Direct text protocol that SerialModule feeds in VE_DIRECT mode and PowerTelemetryModule
// reads as PowerMetrics (ch1 = battery, ch2 = solar panel, ch3 = load output).
//
// The contract: only a block whose bytes sum to 0 mod 256 may change the readings; currents are
// milliamps, the unit every other PowerMetrics source uses (INA3221Sensor.cpp, the device screen and
// the apps all label them mA); and readings are withheld once the device has been silent for
// STALE_AFTER_MS.
//
// The regressions these pin:
// - Without the checksum gate a garbled UART line (or a node joining mid-block) puts invented
//   voltages on the mesh.
// - Reporting amps instead of milliamps shows a 1.5 A charge current as "1.5 mA" in every client.
// - Without the staleness latch a node whose Victron cable has fallen out keeps broadcasting its last
//   readings forever, which reads as a healthy battery. The latch also has to survive the 32-bit
//   millisecond wrap, or a link dead for 49.7 days briefly reads as fresh again.
// - The checksum byte can be any value. Treating a ':' there as the start of a HEX frame drops that
//   block and swallows the head of the next one.
#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "modules/Telemetry/VeDirectSource.h"
#include <cstdint>
#include <string>
#include <unity.h>
#include <utility>
#include <vector>

#if HAS_VEDIRECT_TELEMETRY

using Fields = std::vector<std::pair<std::string, std::string>>;

// One VE.Direct text block: each field as "\r\n<label>\t<value>", closed by the Checksum field whose
// byte brings the sum of the whole block to 0 mod 256.
static std::string makeBlock(const Fields &fields)
{
    std::string block;
    for (const auto &field : fields) {
        block += "\r\n";
        block += field.first;
        block += '\t';
        block += field.second;
    }
    block += "\r\nChecksum\t";
    uint8_t sum = 0;
    for (unsigned char c : block)
        sum += c;
    block += static_cast<char>(static_cast<uint8_t>(0u - sum));
    return block;
}

// What a SmartSolar MPPT sends once a second.
static const Fields kMpptFields = {
    {"PID", "0xA053"}, {"FW", "159"}, {"SER#", "HQ2132ABCDE"}, {"V", "13210"}, {"I", "1500"},  {"VPV", "18500"}, {"PPV", "37"},
    {"CS", "3"},       {"MPPT", "2"}, {"OR", "0x00000000"},    {"ERR", "0"},   {"LOAD", "ON"}, {"IL", "300"},    {"H19", "1234"},
    {"H20", "12"},     {"H21", "45"}, {"H22", "10"},           {"H23", "40"},  {"HSDS", "123"}};

struct FeedCounts {
    int valid = 0;
    int corrupt = 0;
};

static FeedCounts feedAll(VeDirectSource &source, const std::string &bytes)
{
    FeedCounts counts;
    for (unsigned char c : bytes) {
        switch (source.feed(c)) {
        case VeDirectSource::FeedResult::BLOCK_VALID:
            counts.valid++;
            break;
        case VeDirectSource::FeedResult::BLOCK_CORRUPT:
            counts.corrupt++;
            break;
        case VeDirectSource::FeedResult::PENDING:
            break;
        }
    }
    return counts;
}

// The packet as PowerTelemetryModule::getPowerTelemetry() hands it over: zeroed, variant selected.
static meshtastic_Telemetry emptyPowerTelemetry()
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    m.which_variant = meshtastic_Telemetry_power_metrics_tag;
    return m;
}

void setUp(void)
{
    Time::setTestMillis(60 * 1000);
}
void tearDown(void)
{
    Time::useRealClock();
}

// --- decoding ---

void test_mppt_block_fills_battery_panel_and_load_channels(void)
{
    VeDirectSource source;
    FeedCounts counts = feedAll(source, makeBlock(kMpptFields));
    TEST_ASSERT_EQUAL_INT(1, counts.valid);
    TEST_ASSERT_EQUAL_INT(0, counts.corrupt);

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    const meshtastic_PowerMetrics &pm = m.variant.power_metrics;

    TEST_ASSERT_TRUE(pm.has_ch1_voltage && pm.has_ch1_current);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.21f, pm.ch1_voltage);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 1500.0f, pm.ch1_current, "battery current is milliamps, not amps");

    // The MPPT reports panel power, so the current is derived: 37 W / 18.5 V = 2 A.
    TEST_ASSERT_TRUE(pm.has_ch2_voltage && pm.has_ch2_current);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.5f, pm.ch2_voltage);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 2000.0f, pm.ch2_current, "panel current is milliamps, not amps");

    // The load output hangs off the battery, so it reports the battery voltage.
    TEST_ASSERT_TRUE(pm.has_ch3_voltage && pm.has_ch3_current);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.21f, pm.ch3_voltage);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 300.0f, pm.ch3_current, "load current is milliamps, not amps");

    TEST_ASSERT_FALSE_MESSAGE(pm.has_ch4_voltage || pm.has_ch4_current, "only ch1-ch3 are ours to fill");
}

void test_discharge_current_stays_negative(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock({{"V", "12400"}, {"I", "-2500"}}));

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -2500.0f, m.variant.power_metrics.ch1_current);
}

// At night VPV sits near zero; dividing by it would turn a rounding watt into kiloamps.
void test_panel_current_is_zero_without_panel_voltage(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock({{"V", "12400"}, {"VPV", "400"}, {"PPV", "1"}}));

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    TEST_ASSERT_TRUE(m.variant.power_metrics.has_ch2_current);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.variant.power_metrics.ch2_current);
}

// BMV-series monitors send their record as two alternating blocks, so a block must add to the
// readings rather than replace them.
void test_fields_merge_across_split_blocks(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock({{"V", "12800"}, {"I", "-700"}}));
    feedAll(source, makeBlock({{"H1", "-45000"}, {"H2", "-1200"}}));

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    TEST_ASSERT_TRUE_MESSAGE(m.variant.power_metrics.has_ch1_voltage, "the second block must not erase the first");
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.8f, m.variant.power_metrics.ch1_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -700.0f, m.variant.power_metrics.ch1_current);
}

// A device that sends none of the fields we map (an inverter, say) is alive but has nothing to report.
void test_block_without_power_fields_reports_no_metrics(void)
{
    VeDirectSource source;
    FeedCounts counts = feedAll(source, makeBlock({{"PID", "0xA053"}, {"FW", "159"}}));
    TEST_ASSERT_EQUAL_INT(1, counts.valid);
    TEST_ASSERT_TRUE(source.isFresh());

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_FALSE(source.getMetrics(&m));
}

// --- the checksum gate ---

void test_corrupt_block_is_rejected_and_reported(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock(kMpptFields)); // sync, so the next block starts cleanly

    std::string corrupt = makeBlock({{"V", "99990"}, {"I", "1"}});
    corrupt[corrupt.find("99990")] = '1'; // one flipped digit, checksum no longer matches

    FeedCounts counts = feedAll(source, corrupt);
    TEST_ASSERT_EQUAL_INT(0, counts.valid);
    TEST_ASSERT_EQUAL_INT(1, counts.corrupt);

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 13.21f, m.variant.power_metrics.ch1_voltage,
                                     "a corrupt block must leave the last good reading in place");
}

// The node boots in the middle of the device's once-a-second burst. That first partial block cannot
// pass its checksum and is no fault of the wiring, so it must not be reported as corruption.
void test_block_joined_mid_stream_is_dropped_without_an_error(void)
{
    VeDirectSource source;
    const std::string block = makeBlock(kMpptFields);

    FeedCounts counts = feedAll(source, block.substr(block.size() / 2));
    TEST_ASSERT_EQUAL_INT(0, counts.valid);
    TEST_ASSERT_EQUAL_INT(0, counts.corrupt);
    TEST_ASSERT_FALSE(source.isFresh());

    counts = feedAll(source, block);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, counts.valid, "the first complete block after the partial one must decode");
}

// --- HEX frames ---

// The device answers HEX commands (":A..."\n) on the same wire, in the middle of a text block if it
// has to. The interrupted block is lost; the stream must recover on the next one.
void test_hex_frame_voids_the_block_it_interrupts(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock(kMpptFields));

    const std::string block = makeBlock({{"V", "11000"}, {"I", "5"}});
    const size_t cut = block.find("\r\nI");
    FeedCounts counts = feedAll(source, block.substr(0, cut) + ":A0002000148\n" + block.substr(cut));
    TEST_ASSERT_EQUAL_INT(0, counts.valid);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, counts.corrupt, "a block cut by a HEX frame is expected loss, not corruption");

    counts = feedAll(source, makeBlock({{"V", "12000"}}));
    TEST_ASSERT_EQUAL_INT(1, counts.valid);

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 12.0f, m.variant.power_metrics.ch1_voltage,
                                     "the interrupted block's 11.0 V must never have been committed");
}

void test_hex_frame_between_blocks_costs_nothing(void)
{
    VeDirectSource source;
    FeedCounts counts = feedAll(source, makeBlock(kMpptFields) + ":A0002000148\n" + makeBlock({{"V", "12000"}}));
    TEST_ASSERT_EQUAL_INT(2, counts.valid);
    TEST_ASSERT_EQUAL_INT(0, counts.corrupt);
}

// A valid block whose checksum byte is `wanted`. A filler of repeated 'A' steers it: 65 is odd, so
// some length below 256 reaches every residue.
static std::string makeBlockWithChecksumByte(uint8_t wanted)
{
    for (size_t len = 0; len < 256; len++) {
        std::string block = makeBlock({{"V", "12000"}, {"SER#", std::string(len, 'A')}});
        if (static_cast<uint8_t>(block.back()) == wanted)
            return block;
    }
    return std::string();
}

// The regression: ':' is also a perfectly good checksum value, about once in 256 blocks.
void test_checksum_byte_that_looks_like_a_hex_start_is_still_a_checksum(void)
{
    VeDirectSource source;
    const std::string block = makeBlockWithChecksumByte(':');
    TEST_ASSERT_FALSE_MESSAGE(block.empty(), "test setup: no filler length produced a ':' checksum");

    FeedCounts counts = feedAll(source, block);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, counts.valid, "a ':' checksum byte must close the block, not open a HEX frame");

    counts = feedAll(source, makeBlock({{"V", "12500"}}));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, counts.valid, "and the block after it must not be swallowed");
}

// --- staleness ---

void test_readings_expire_when_blocks_stop(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock(kMpptFields));
    TEST_ASSERT_TRUE(source.isFresh());

    Time::advanceTestMillis(VeDirectSource::STALE_AFTER_MS - 1);
    TEST_ASSERT_TRUE_MESSAGE(source.isFresh(), "one millisecond short of the limit is still fresh");

    Time::advanceTestMillis(1);
    TEST_ASSERT_FALSE(source.isFresh());

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_FALSE_MESSAGE(source.getMetrics(&m), "a silent device must not keep reporting its last readings");
    TEST_ASSERT_FALSE(m.variant.power_metrics.has_ch1_voltage);
}

void test_a_new_block_keeps_the_readings_alive(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock(kMpptFields));

    Time::advanceTestMillis(VeDirectSource::STALE_AFTER_MS - 1);
    feedAll(source, makeBlock(kMpptFields));
    Time::advanceTestMillis(VeDirectSource::STALE_AFTER_MS - 1);

    TEST_ASSERT_TRUE(source.isFresh());
}

// After an outage the first block may carry fewer fields than the device sent before it. What it
// does not carry must stay unreported rather than be filled in from before the outage.
void test_expired_readings_are_dropped_not_reused(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock(kMpptFields));

    Time::advanceTestMillis(VeDirectSource::STALE_AFTER_MS);
    TEST_ASSERT_FALSE(source.isFresh());

    feedAll(source, makeBlock({{"V", "12000"}}));
    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, m.variant.power_metrics.ch1_voltage);
    TEST_ASSERT_FALSE_MESSAGE(m.variant.power_metrics.has_ch2_voltage || m.variant.power_metrics.has_ch3_current,
                              "panel and load readings from before the outage must not come back");
}

// Same outage, but nothing asked isFresh() while it lasted: the block that ends it must still not
// merge into the readings from before it.
void test_block_after_an_unobserved_outage_does_not_revive_old_readings(void)
{
    VeDirectSource source;
    feedAll(source, makeBlock(kMpptFields));

    Time::advanceTestMillis(VeDirectSource::STALE_AFTER_MS + 1);
    feedAll(source, makeBlock({{"V", "12000"}}));

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_TRUE(source.getMetrics(&m));
    TEST_ASSERT_FALSE_MESSAGE(m.variant.power_metrics.has_ch2_voltage || m.variant.power_metrics.has_ch3_current,
                              "the class must drop stale readings itself, not rely on a caller polling isFresh()");
}

// The freshness test is `now - stamp < limit` in unsigned 32-bit arithmetic, so 49.7 days after the
// last block the stamp looks recent again. The expiry has to latch for the answer to stay "stale".
void test_expired_link_stays_expired_across_the_clock_wrap(void)
{
    VeDirectSource source;
    const uint32_t lastBlockAt = Time::getMillis();
    feedAll(source, makeBlock(kMpptFields));

    Time::advanceTestMillis(VeDirectSource::STALE_AFTER_MS);
    TEST_ASSERT_FALSE(source.isFresh());

    Time::setTestMillis(lastBlockAt + 5000); // the clock has gone all the way round
    TEST_ASSERT_FALSE_MESSAGE(source.isFresh(), "a link dead for 49.7 days must not read as fresh at the wrap");

    meshtastic_Telemetry m = emptyPowerTelemetry();
    TEST_ASSERT_FALSE(source.getMetrics(&m));
}

// --- hand-over to PowerTelemetryModule ---

// PowerTelemetryModule consults this source only once SerialModule is really decoding. Until then a
// node with I2C power sensors must keep reporting those.
void test_source_is_inactive_until_the_serial_module_claims_it(void)
{
    VeDirectSource source;
    TEST_ASSERT_FALSE(source.isActive());

    source.setActive();
    TEST_ASSERT_TRUE(source.isActive());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_mppt_block_fills_battery_panel_and_load_channels);
    RUN_TEST(test_discharge_current_stays_negative);
    RUN_TEST(test_panel_current_is_zero_without_panel_voltage);
    RUN_TEST(test_fields_merge_across_split_blocks);
    RUN_TEST(test_block_without_power_fields_reports_no_metrics);
    RUN_TEST(test_corrupt_block_is_rejected_and_reported);
    RUN_TEST(test_block_joined_mid_stream_is_dropped_without_an_error);
    RUN_TEST(test_hex_frame_voids_the_block_it_interrupts);
    RUN_TEST(test_hex_frame_between_blocks_costs_nothing);
    RUN_TEST(test_checksum_byte_that_looks_like_a_hex_start_is_still_a_checksum);
    RUN_TEST(test_readings_expire_when_blocks_stop);
    RUN_TEST(test_a_new_block_keeps_the_readings_alive);
    RUN_TEST(test_expired_readings_are_dropped_not_reused);
    RUN_TEST(test_block_after_an_unobserved_outage_does_not_revive_old_readings);
    RUN_TEST(test_expired_link_stays_expired_across_the_clock_wrap);
    RUN_TEST(test_source_is_inactive_until_the_serial_module_claims_it);
    exit(UNITY_END());
}

void loop() {}

#else // !HAS_VEDIRECT_TELEMETRY

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
