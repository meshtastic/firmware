// Unit tests for the Modbus-RTU protocol code in src/modules/SerialModbus.{h,cpp}, used by SerialModule's
// MODBUS mode to poll a SenseCAP ONE weather sensor on RS485.
//
// What is pinned, and why:
//   - crc16() / buildRead() / checkResponse() against frames whose CRCs were recomputed with a reference
//     CRC-16/Modbus (poly 0xA001 reflected, init 0xFFFF). A wrong CRC or byte order makes every poll fail
//     on real hardware while the code still looks plausible. Three response examples in the Seeed manual
//     carry wrong CRCs; the vectors here are the recomputed ones.
//   - checkResponse() must reject a frame with a bad CRC, a foreign address, or the wrong register count,
//     and must report a partial frame as RESP_INCOMPLETE rather than RESP_INVALID: SerialModule polls it every 10 ms
//     while bytes are still arriving, and a host tunnel frame on the shared bus must never be decoded as a
//     sensor reading.
//   - storeRegisters() decodes big-endian signed int32 register pairs; a sign or byte-order slip turns
//     -1.0 C into a huge positive value.
//   - Aggregate scales raw milli-units to telemetry units (pressure Pa*1000 -> hPa) and takes the vector
//     mean of wind direction. An arithmetic mean of 350 and 10 degrees gives 180, the opposite direction.
//     The mean uses polynomial sin/atan2 (libm's cost 4 KB of STM32 flash); a whole degree in must come
//     back as the same degree, or the approximation has drifted.
#include "TestUtil.h"
#include <unity.h>

#if !MESHTASTIC_EXCLUDE_MODBUS

#include "modules/SerialModbus.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace modbus;

static std::vector<uint8_t> hex(const char *s)
{
    std::vector<uint8_t> out;
    unsigned v;
    int n;
    while (sscanf(s, " %2x%n", &v, &n) == 1) {
        out.push_back((uint8_t)v);
        s += n;
    }
    return out;
}

static void assertCrc(const char *frame)
{
    std::vector<uint8_t> f = hex(frame);
    uint16_t crc = crc16(f.data(), f.size() - 2);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(crc & 0xFF, f[f.size() - 2], frame);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(crc >> 8, f[f.size() - 1], frame);
}

static void assertRequest(const char *frame, uint8_t addr, uint16_t reg, uint16_t count)
{
    std::vector<uint8_t> want = hex(frame);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(8, buildRead(out, addr, READ_INPUT, reg, count));
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want.data(), out, 8, frame);
}

void setUp(void) {}
void tearDown(void) {}

// --- CRC and frame builder ---

void test_crc_reference_vectors()
{
    const char *frames[] = {"01 04 00 00 00 02 71 CB",
                            "01 04 04 00 00 6E 8C D6 41",
                            "01 04 04 FF FF FC 18 BA AA",
                            "0A 04 00 00 00 06 71 73",
                            "0A 04 00 08 00 0C 70 B6",
                            "0A 04 18 00 00 00 00 00 03 6E 84 00 03 C8 C0 00 00 00 00 00 00 04 BC 00 00 02 10 BC 78",
                            "14 04 00 00 00 20 F3 17",
                            "2B 04 00 00 00 20 F6 18",
                            "2B 04 00 30 00 04 F6 0C",
                            "2B 04 00 40 00 02 77 D5",
                            "2B 04 04 00 0C EC 98 FD 2F",
                            "01 84 01 82 C0",
                            "01 84 04 42 C3"};
    for (const char *f : frames)
        assertCrc(f);
}

void test_build_read_requests()
{
    assertRequest("01 04 00 00 00 02 71 CB", 1, 0x0000, 2);
    assertRequest("0A 04 00 00 00 06 71 73", 10, 0x0000, 6);
    assertRequest("0A 04 00 08 00 0C 70 B6", 10, 0x0008, 12);
    assertRequest("14 04 00 00 00 20 F3 17", 20, 0x0000, 0x20);
    assertRequest("2B 04 00 00 00 20 F6 18", 43, 0x0000, 0x20);
    assertRequest("2B 04 00 30 00 04 F6 0C", 43, 0x0030, 4);
    assertRequest("2B 04 00 40 00 02 77 D5", 43, 0x0040, 2);
}

// --- Response validation and register decode ---

void test_temperature_responses_decode_signed()
{
    int32_t raw[REG_SLOTS] = {};
    std::vector<uint8_t> pos = hex("01 04 04 00 00 6E 8C D6 41");
    TEST_ASSERT_EQUAL(RESP_OK, checkResponse(pos.data(), pos.size(), 1, READ_INPUT, 2));
    storeRegisters(raw, 0, pos.data() + 3, pos[2]);
    TEST_ASSERT_EQUAL_INT32(28300, raw[0]);

    std::vector<uint8_t> neg = hex("01 04 04 FF FF FC 18 BA AA");
    TEST_ASSERT_EQUAL(RESP_OK, checkResponse(neg.data(), neg.size(), 1, READ_INPUT, 2));
    storeRegisters(raw, 0, neg.data() + 3, neg[2]);
    TEST_ASSERT_EQUAL_INT32(-1000, raw[0]);
}

void test_s500_wind_block_decodes()
{
    int32_t raw[REG_SLOTS] = {};
    std::vector<uint8_t> f = hex("0A 04 18 00 00 00 00 00 03 6E 84 00 03 C8 C0 00 00 00 00 00 00 04 BC 00 00 02 10 BC 78");
    TEST_ASSERT_EQUAL(RESP_OK, checkResponse(f.data(), f.size(), 10, READ_INPUT, 12));
    storeRegisters(raw, 0x0008, f.data() + 3, f[2]);
    TEST_ASSERT_EQUAL_INT32(0, raw[0x08 / 2]);      // min direction
    TEST_ASSERT_EQUAL_INT32(224900, raw[0x0A / 2]); // max direction
    TEST_ASSERT_EQUAL_INT32(248000, raw[0x0C / 2]); // avg direction
    TEST_ASSERT_EQUAL_INT32(0, raw[0x0E / 2]);      // min speed
    TEST_ASSERT_EQUAL_INT32(1212, raw[0x10 / 2]);   // max speed
    TEST_ASSERT_EQUAL_INT32(528, raw[0x12 / 2]);    // avg speed
}

void test_co2_response_decodes()
{
    int32_t raw[REG_SLOTS] = {};
    std::vector<uint8_t> f = hex("2B 04 04 00 0C EC 98 FD 2F");
    TEST_ASSERT_EQUAL(RESP_OK, checkResponse(f.data(), f.size(), 43, READ_INPUT, 2));
    storeRegisters(raw, 0x0040, f.data() + 3, f[2]);
    TEST_ASSERT_EQUAL_INT32(847000, raw[0x40 / 2]);
}

void test_exception_frames_reported()
{
    std::vector<uint8_t> e1 = hex("01 84 01 82 C0");
    std::vector<uint8_t> e4 = hex("01 84 04 42 C3");
    TEST_ASSERT_EQUAL(RESP_EXCEPTION, checkResponse(e1.data(), e1.size(), 1, READ_INPUT, 2));
    TEST_ASSERT_EQUAL(RESP_EXCEPTION, checkResponse(e4.data(), e4.size(), 1, READ_INPUT, 2));
}

void test_crc_mismatch_rejected()
{
    std::vector<uint8_t> f = hex("01 04 04 00 00 6E 8C D6 41");
    f[5] ^= 0x01;
    TEST_ASSERT_EQUAL(RESP_INVALID, checkResponse(f.data(), f.size(), 1, READ_INPUT, 2));
}

void test_wrong_address_rejected()
{
    // A tunnel frame for address 240 arriving while a sensor answer is awaited.
    std::vector<uint8_t> f = hex("F0 42 00 41 53");
    TEST_ASSERT_EQUAL(RESP_INVALID, checkResponse(f.data(), f.size(), 1, READ_INPUT, 2));
    TEST_ASSERT_EQUAL(RESP_INVALID, checkResponse(f.data(), 1, 1, READ_INPUT, 2));
}

void test_wrong_register_count_rejected()
{
    std::vector<uint8_t> f = hex("01 04 04 00 00 6E 8C D6 41");
    TEST_ASSERT_EQUAL(RESP_INVALID, checkResponse(f.data(), f.size(), 1, READ_INPUT, 4));
}

void test_short_frame_incomplete()
{
    std::vector<uint8_t> f = hex("01 04 04 00 00 6E 8C D6 41");
    for (size_t len = 0; len < f.size(); len++)
        TEST_ASSERT_EQUAL_MESSAGE(RESP_INCOMPLETE, checkResponse(f.data(), len, 1, READ_INPUT, 2), "partial frame");
}

// --- Profiles and poll plan ---

void test_unknown_address_gets_s700_set()
{
    TEST_ASSERT_EQUAL_HEX8(CAP_THP | CAP_LIGHT | CAP_WIND | CAP_RAIN, capsForAddress(5));
    TEST_ASSERT_EQUAL_HEX8(CAP_WIND, capsForAddress(44));
}

void test_poll_plan_full_and_split()
{
    uint8_t step = 0;
    uint16_t reg, count;
    uint8_t s1000 = capsForAddress(43);
    TEST_ASSERT_TRUE(nextRead(s1000, false, step, reg, count));
    TEST_ASSERT_EQUAL(0x00, reg);
    TEST_ASSERT_EQUAL(0x20, count);
    step++;
    TEST_ASSERT_TRUE(nextRead(s1000, false, step, reg, count));
    TEST_ASSERT_EQUAL(0x30, reg);
    step++;
    TEST_ASSERT_TRUE(nextRead(s1000, false, step, reg, count));
    TEST_ASSERT_EQUAL(0x40, reg);
    step++;
    TEST_ASSERT_FALSE(nextRead(s1000, false, step, reg, count));

    step = 0;
    uint8_t s500 = capsForAddress(10);
    TEST_ASSERT_TRUE(nextRead(s500, true, step, reg, count));
    TEST_ASSERT_EQUAL(0x00, reg);
    TEST_ASSERT_EQUAL(6, count);
    step++;
    TEST_ASSERT_TRUE(nextRead(s500, true, step, reg, count));
    TEST_ASSERT_EQUAL(0x08, reg);
    TEST_ASSERT_EQUAL(12, count);
    step++;
    TEST_ASSERT_FALSE(nextRead(s500, true, step, reg, count));
}

// --- Aggregation ---

void test_pressure_scaled_to_hpa()
{
    int32_t raw[REG_SLOTS] = {};
    raw[0x04 / 2] = 101160000;
    Aggregate agg;
    agg.add(raw);
    meshtastic_EnvironmentMetrics e = meshtastic_EnvironmentMetrics_init_zero;
    agg.environment(e, CAP_THP);
    TEST_ASSERT_TRUE(e.has_barometric_pressure);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1011.6f, e.barometric_pressure);
}

void test_wind_direction_vector_mean_wraps()
{
    int32_t raw[REG_SLOTS] = {};
    Aggregate agg;
    raw[0x0C / 2] = 350000;
    agg.add(raw);
    raw[0x0C / 2] = 10000;
    agg.add(raw);
    meshtastic_EnvironmentMetrics e = meshtastic_EnvironmentMetrics_init_zero;
    agg.environment(e, CAP_WIND);
    TEST_ASSERT_TRUE(e.has_wind_direction);
    TEST_ASSERT_EQUAL_UINT16(0, e.wind_direction);
}

void test_wind_direction_single_sample_round_trips()
{
    // The mean uses polynomial sin/atan2 instead of libm; every whole degree must come back unchanged.
    int32_t raw[REG_SLOTS] = {};
    for (uint16_t deg = 0; deg < 360; deg++) {
        Aggregate agg;
        raw[0x0C / 2] = deg * 1000;
        agg.add(raw);
        meshtastic_EnvironmentMetrics e = meshtastic_EnvironmentMetrics_init_zero;
        agg.environment(e, CAP_WIND);
        TEST_ASSERT_EQUAL_UINT16(deg, e.wind_direction);
    }
}

void test_wind_gust_and_lull_are_extremes()
{
    int32_t raw[REG_SLOTS] = {};
    Aggregate agg;
    raw[0x0E / 2] = 500; // min speed
    raw[0x10 / 2] = 3000;
    raw[0x12 / 2] = 1000;
    agg.add(raw);
    raw[0x0E / 2] = 200;
    raw[0x10 / 2] = 2000;
    raw[0x12 / 2] = 2000;
    agg.add(raw);
    meshtastic_EnvironmentMetrics e = meshtastic_EnvironmentMetrics_init_zero;
    agg.environment(e, CAP_WIND);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, e.wind_lull);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, e.wind_gust);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, e.wind_speed);
}

void test_rain_24h_survives_counter_reset()
{
    // Accumulated counter 10.0 -> 12.5 mm, then the sensor restarts from 0 and reaches 1.0 mm.
    int32_t raw[REG_SLOTS] = {};
    Aggregate agg;
    raw[0x14 / 2] = 10000;
    agg.add(raw);
    raw[0x14 / 2] = 12500;
    agg.add(raw);
    agg.nextHour();
    raw[0x14 / 2] = 0;
    agg.add(raw);
    raw[0x14 / 2] = 1000;
    agg.add(raw);
    meshtastic_EnvironmentMetrics e = meshtastic_EnvironmentMetrics_init_zero;
    agg.environment(e, CAP_RAIN);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, e.rainfall_24h);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_crc_reference_vectors);
    RUN_TEST(test_build_read_requests);
    RUN_TEST(test_temperature_responses_decode_signed);
    RUN_TEST(test_s500_wind_block_decodes);
    RUN_TEST(test_co2_response_decodes);
    RUN_TEST(test_exception_frames_reported);
    RUN_TEST(test_crc_mismatch_rejected);
    RUN_TEST(test_wrong_address_rejected);
    RUN_TEST(test_wrong_register_count_rejected);
    RUN_TEST(test_short_frame_incomplete);
    RUN_TEST(test_unknown_address_gets_s700_set);
    RUN_TEST(test_poll_plan_full_and_split);
    RUN_TEST(test_pressure_scaled_to_hpa);
    RUN_TEST(test_wind_direction_vector_mean_wraps);
    RUN_TEST(test_wind_direction_single_sample_round_trips);
    RUN_TEST(test_wind_gust_and_lull_are_extremes);
    RUN_TEST(test_rain_24h_survives_counter_reset);
    exit(UNITY_END());
}

#else

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif

void loop() {}
